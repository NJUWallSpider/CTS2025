// PairingGenerator.cpp
#include "PairingGenerator.hpp"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <thread>
#include <vector>
#include <fstream>

void write_results(const std::string& airport, const Date& date, const std::vector<FDP>& found_fdps) {
    // 尝试打开文件
    std::ofstream out_file("/home/bhz/CTS-2025/Pair-and-Assign/FDP_result.txt", std::ios::app);

    // 关键检查：确认文件是否成功打开
    if (out_file.is_open()) {
        // 文件打开成功，执行写入操作
        out_file << "--------------------------------" << std::endl;
        out_file << "Airport: " << airport << " Date: " << std::to_string(int(date.year())) + "-" +
            std::to_string(unsigned(date.month())) + "-" +
            std::to_string(unsigned(date.day())) << std::endl;
        out_file << "--------------------------------" << std::endl;    
        out_file << "Total FDPs: " << found_fdps.size() << std::endl;
        for (const auto& fdp : found_fdps) {
            out_file << fdp.to_string() << std::endl;
        }
        out_file << std::endl;
        out_file.close(); // 虽然析构函数会自动关闭，但显式关闭是好习惯
    } else {
        // 文件打开失败，向标准错误流报告问题
        std::cerr << "Error: Unable to open file for writing at /home/bhz/CTS-2025/Pair-and-Assign/FDP_result.txt" << std::endl;
        std::cerr << "Please check if the directory exists and you have write permissions." << std::endl;
    }
}

// --- 构造函数 ---
PairingGenerator::PairingGenerator(SchedulingData& data, Date start_date, Date end_date) : 
    data_(data), 
    start_date_(start_date),
    end_date_(end_date),
    MIN_CONNECTION_TIME_BUS(std::chrono::hours(2)),
    MIN_CONNECTION_TIME_FLIGHT(std::chrono::hours(3)),
    MAX_FLIGHT_TASKS_PER_FDP(4),
    MAX_TOTAL_TASKS_PER_FDP(6),
    MAX_FLY_TIME_PER_FDP(std::chrono::hours(8)),
    MAX_DUTY_TIME_PER_FDP(std::chrono::hours(12)){
    _prepare_tasks();
}

// --- 任务预处理 ---
// 该函数负责预处理所有任务，包括:
// 1. 将航班任务转换为两种类型:
//    - flight: 正常执飞航班任务
//    - positioning_flight: 机组作为乘客的定位飞行任务
// 2. 添加巴士任务
// 3. 按出发机场对任务进行分组，并按起飞时间排序
void PairingGenerator::_prepare_tasks() {
    std::cout << "预处理所有任务..." << std::endl;

    // 1. 添加所有航班，每个航班生成两个任务：'flight' 和 'positioning_flight'
    for (const auto& pair : data_.get_all_flights()) {
        const Flight& flt = pair.second;
        
        // 直接在map的vector中构造Task，比先存入临时vector再分组更高效
        tasks_by_airport_[flt.depa_airport].emplace_back(Task{
            flt.id, "flight", flt.depa_airport, flt.arri_airport,
            flt.std, flt.sta, std::chrono::minutes(flt.fly_time), flt.aircraft_no
        });

        // tasks_by_airport_[flt.depa_airport].emplace_back(Task{
        //     flt.id, "deadhead_flight", flt.depa_airport, flt.arri_airport,
        //     flt.std, flt.sta, std::chrono::minutes(flt.fly_time), flt.aircraft_no
        // });
    }

    // 2. 添加所有巴士任务
    for (const auto& pair : data_.get_all_buses()) {
        const Bus& bus = pair.second;
        tasks_by_airport_[bus.depa_airport].emplace_back(Task{
            bus.id, "bus", bus.depa_airport, bus.arri_airport,
            bus.td, bus.ta, std::chrono::minutes(0), ""
        });
    }
    
    // 3. 按出发机场分组后，对每个机场的任务列表按起飞时间排序
    for (auto& pair : tasks_by_airport_) {
        std::sort(pair.second.begin(), pair.second.end(), [](const Task& a, const Task& b) {
            return a.start_time < b.start_time;
        });
    }
    std::cout << "任务预处理完成。" << std::endl;
}

// --- 单线程版本的构建FDP函数 ---
void PairingGenerator::build_all_valid_fdps_single_thread() {
    using namespace std::chrono;
    
    Date start_date{year(2025)/May/day(27)};
    Date end_date{year(2025)/June/day(4)};

    // 生成所有(机场, 日期)组合的任务列表
    std::vector<std::pair<std::string, Date>> jobs;
    for (const auto& pair : tasks_by_airport_) {
        for (auto d = sys_days(start_date); d < sys_days(end_date); d += days(1)) {
            jobs.emplace_back(pair.first, Date(d));
        }
    }

    std::cout << "使用单线程模式进行计算..." << std::endl;
    std::cout << "总任务数: " << jobs.size() << std::endl;

    // 顺序处理每个任务
    int count = 0;
    for (const auto& job : jobs) {
        // 直接调用处理函数，不使用异步
        std::vector<FDP> result_fdps = _process_airport_date(job.first, job.second);
        
        if (!result_fdps.empty()) {
            data_.all_valid_fdps_[job] = std::move(result_fdps);
        }
        
        // 打印进度
        if (++count % 10 == 0) {
            std::cout << "已完成 " << count << " / " << jobs.size() << " 个任务..." << std::endl;
        }
    }

    // 对所有结果按分数排序
    for (auto& pair : data_.all_valid_fdps_) {
        std::sort(pair.second.begin(), pair.second.end(), [](const FDP& a, const FDP& b) {
            return a.get_score() > b.get_score();
        });
    }

    // 输出结果到文件
    for (const auto& [job, fdps] : data_.all_valid_fdps_) {
        write_results(job.first, job.second, fdps);
    }

    std::cout << "所有合法FDP构建完成。" << std::endl;
    
    // 输出示例FDP
    if (!data_.all_valid_fdps_.empty()) {
        auto it = data_.all_valid_fdps_.begin();
        std::cout << "示例机场 " << it->first.first << " 日期 " << std::to_string(int(it->first.second.year())) + "-" +
            std::to_string(unsigned(it->first.second.month())) + "-" +
            std::to_string(unsigned(it->first.second.day())) << " 的FDP数量: " << it->second.size() << std::endl;
        
        // 输出前5个FDP的详细信息
        int fdp_count = 0;
        for (const auto& fdp : it->second) {
            if (fdp_count++ >= 5) break;
            std::cout << fdp.to_string() << std::endl;
        }
    }
}

// --- 并发构建FDP ---
void PairingGenerator::build_all_valid_fdps() {
    using namespace std::chrono;
    
    std::vector<std::pair<std::string, Date>> jobs;
    for (const auto& pair : tasks_by_airport_) {
        for (auto d = sys_days(start_date_); d < sys_days(end_date_); d += days(1)) {
            jobs.emplace_back(pair.first, Date(d));
        }
    }

    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "使用 " << num_threads << " 个线程进行并行计算..." << std::endl;

    // 存储所有异步任务的 future
    std::vector<std::future<std::vector<FDP>>> futures;
    futures.reserve(jobs.size()); // 预分配空间以提高效率

    for (const auto& job : jobs) {
        // std::async 启动一个异步任务
        futures.push_back(std::async(std::launch::async, 
            &PairingGenerator::_process_airport_date, this,
            job.first, job.second));
    }
    
    std::cout << "所有任务已提交，等待计算结果..." << std::endl;
    int count = 0;
    for (size_t i = 0; i < futures.size(); ++i) {
        std::vector<FDP> result_fdps = futures[i].get(); // .get()会阻塞直到该任务完成
        if (!result_fdps.empty()) {
            data_.all_valid_fdps_[jobs[i]] = std::move(result_fdps);
        }
        // 打印进度
        if (++count % 100 == 0) {
            std::cout << "已完成 " << count << " / " << jobs.size() << " 个任务..." << std::endl;
        }
    }

    // 对所有结果按分数排序
    for (auto& pair : data_.all_valid_fdps_) {
        std::sort(pair.second.begin(), pair.second.end(), [](const FDP& a, const FDP& b) {
            return a.get_score() > b.get_score();
        });
    }

    // for (const auto& [job, fdps] : data_.all_valid_fdps_) {
    //     write_results(job.first, job.second, fdps);
    // }

    std::cout << "所有合法FDP构建完成。" << std::endl;
    // ... 可以在这里添加示例输出的代码 ...
}

// --- 单个(机场, 日期)的处理函数 ---
std::vector<FDP> PairingGenerator::_process_airport_date(const std::string& airport, const Date& date) {
    std::vector<FDP> found_fdps;
    auto it = tasks_by_airport_.find(airport);
    if (it == tasks_by_airport_.end()) {
        return found_fdps;
    }

    const auto& tasks_at_airport = it->second;
    for (const auto& initial_task : tasks_at_airport) {
        bool flag = initial_task.id == "Flt_101694";
        Date task_date = std::chrono::floor<std::chrono::days>(initial_task.start_time);
        if (task_date == date) {
            std::vector<Task> current_path = {initial_task};
            std::unordered_set<std::string> used_task_ids = {initial_task.id};
            _dfs_fdp_builder(current_path, used_task_ids, found_fdps);
        }
    }
    
    // 过滤掉只有bus任务且起始终点机场相同的FDP
    found_fdps.erase(
        std::remove_if(found_fdps.begin(), found_fdps.end(),
            [](const FDP& fdp) {
                // 检查是否所有任务都是bus
                bool all_bus = std::all_of(fdp.tasks.begin(), fdp.tasks.end(),
                    [](const Task& task) { return task.task_type == "bus"; });
                
                // 如果全是bus任务，检查起始终点机场是否相同
                if (all_bus) {
                    return fdp.get_start_airport() == fdp.get_end_airport();
                }
                return false;  // 不是全bus的FDP保留
            }
        ),
        found_fdps.end()
    );
    
    return found_fdps;
}

// --- 核心DFS算法 ---
void PairingGenerator::_dfs_fdp_builder(
    std::vector<Task>& current_path,
    std::unordered_set<std::string>& used_task_ids,
    std::vector<FDP>& found_fdps) 
{
    bool is_end = false;
    // std::vector<FDP> no_good_fdps;
    // --- 规则检查与剪枝---
    Task& last_task = current_path.back();
    
    // 规则1: 任务数量检查
    long flight_task_count = std::count_if(current_path.begin(), current_path.end(), 
        [](const Task& t){ return t.task_type != "bus"; });
    
    if (flight_task_count == MAX_FLIGHT_TASKS_PER_FDP) {
        is_end = true;
    } 

    // 规则2，总任务数限制
    if (current_path.size() == MAX_TOTAL_TASKS_PER_FDP) {
        is_end = true;
    }
    
    // 规则4: 置位规则检查
    if (current_path.size() >= 3) {
        // 检查中间任务是否有置位任务
        for (size_t i = 1; i < current_path.size() - 1; ++i) {
            const auto& task_type = current_path[i].task_type;
            if (task_type != "flight") {
                return; // 剪枝
            }
        }
    }

    // --- 记录合法的FDP ---
    // 必须在可过夜机场结束
    // if (data_.get_layover_stations().count(current_path.back().end_airport) > 0) {

        FDP new_fdp{current_path};
        found_fdps.push_back(std::move(new_fdp));
    // }

    // 如果已满足终止条件，则不再向下搜索
    if (is_end) {
        return;
    }
    
    // --- 扩展搜索 ---
    auto it = tasks_by_airport_.find(last_task.end_airport);
    if (it == tasks_by_airport_.end()) return;
    std::vector<Task>& next_tasks = it->second;

    for (const auto& next_task : next_tasks) {
        bool flag = next_task.id == "Flt_100324";
        // 衔接时间检查
        if (next_task.start_time < last_task.end_time) continue;
        
        // 避免环路
        if (used_task_ids.count(next_task.id)) continue;
        
        // 最小衔接时间检查
        if (last_task.aircraft_no != next_task.aircraft_no) {
            bool is_bus_involved = (last_task.task_type == "bus" || next_task.task_type == "bus");
            auto min_connection = is_bus_involved ? MIN_CONNECTION_TIME_BUS : MIN_CONNECTION_TIME_FLIGHT;
            if (next_task.start_time < last_task.end_time + min_connection) {
                continue;
            }
        }

        // 把飞行时间检查移动到此处，因为需要先检查是否满足条件，再递归
        if (next_task.task_type != "bus") {
            auto total_fly_time = std::accumulate(current_path.begin(), current_path.end(), std::chrono::seconds(0), 
                [](std::chrono::seconds sum, const Task& t){ return sum + t.fly_time; });
            total_fly_time += next_task.fly_time;
            if (total_fly_time > MAX_FLY_TIME_PER_FDP) {
                continue; // 剪枝
            }

            // 因为执勤时间计算的是第一个任务到最后一个飞行任务
            auto duty_time = next_task.end_time - current_path.front().start_time;
            if (duty_time > MAX_DUTY_TIME_PER_FDP) {
                continue;
            }
        }


        // 递归
        current_path.push_back(next_task);
        used_task_ids.insert(next_task.id);
        _dfs_fdp_builder(current_path, used_task_ids, found_fdps);
        current_path.pop_back();
        used_task_ids.erase(next_task.id);
    }
}