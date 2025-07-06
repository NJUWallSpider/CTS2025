#include "SubproblemSolver.h"
#include "../gurobi_solvers/MasterProblem.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <chrono>
#include <iostream>
#include <thread>
#include <mutex>
#include <future>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <random> // Added for random number generation

namespace fs = std::filesystem;

SubproblemSolver::SubproblemSolver(const SchedulingData& data, const MasterProblem& master, std::string fdp_path, 
                                 double non_base_rejection_prob, int beam_width)
    : data_(data), master_(master), reduced_cost_(0.0), 
      non_base_rejection_prob_(non_base_rejection_prob), beam_width_(beam_width) {
    // 初始化时预缓存所有航班的对偶值
    network_directory_ = fdp_path;
    updateDuals();
    
    // 尝试加载保存的FDP网络
    // loadAllFDPNetworks(network_directory_);
}

void SubproblemSolver::updateDuals() {
    flight_duals_cache_.clear();
    for (const auto& flight : data_.get_all_flights()) {
        flight_duals_cache_[flight.first] = master_.getFlightDual(flight.first);
    }
}

void SubproblemSolver::clearCache() {
    crew_valid_fdps_cache_.clear();
    flight_duals_cache_.clear();
    fdp_flight_ids_cache_.clear();
    connectivity_cache_.clear();
}

bool SubproblemSolver::solveForCrew(const std::string& crew_id) {
    // 使用已缓存的对偶值
    return solveForCrewWithDuals(crew_id, flight_duals_cache_);
}

bool SubproblemSolver::solveForCrewWithDuals(const std::string& crew_id, 
                                           const std::unordered_map<std::string, double>& flight_duals) {
    // 获取机长的对偶价值（机会成本）
    double crew_dual = master_.getCrewDual(crew_id);
    
    // 筛选该机长可执行的合法执勤日（使用缓存）
    std::vector<FDP> valid_fdps = filterValidFDPs(crew_id);
    
    // 求解最长路问题，获取最优飞行周期
    std::vector<FDP> best_path = solveLongestPath(crew_id, valid_fdps, crew_dual);
    
    // 如果没有找到有价值的路径
    if (best_path.empty()) {
        return false;
    }
    
    // 如果只有一个FDP，直接使用它
    if (best_path.size() == 1) {
        best_fdp_ = best_path[0];
    } else {
        // 如果有多个FDP，需要将它们合并成一个飞行周期
        FDP combined_fdp;
        for (const auto& fdp : best_path) {
            combined_fdp.tasks.insert(combined_fdp.tasks.end(), fdp.tasks.begin(), fdp.tasks.end());
        }
        best_fdp_ = combined_fdp;
    }
    
    // 计算最终的检验数（盈利能力）
    reduced_cost_ = 0.0;
    for (const auto& fdp : best_path) {
        reduced_cost_ += calculateFDPReward(fdp, flight_duals);
    }
    reduced_cost_ -= crew_dual;

    //输出求解信息
    std::cout << "机长 " << crew_id << " 求解子问题，求解结果：" << reduced_cost_ << std::endl;
    
    return reduced_cost_ > 0.0;
}


const FDP& SubproblemSolver::getBestFDP() const {
    return best_fdp_;
}

double SubproblemSolver::getReducedCost() const {
    return reduced_cost_;
}

std::vector<FDP> SubproblemSolver::filterValidFDPs(const std::string& crew_id) {
    // 检查缓存
    auto cache_it = crew_valid_fdps_cache_.find(crew_id);
    if (cache_it != crew_valid_fdps_cache_.end()) {
        return cache_it->second;
    }
    
    std::vector<FDP> valid_fdps;
    
    // 获取机长信息
    const Crew* crew = data_.get_crew(crew_id);
    if (!crew) {
        std::cerr << "找不到机长: " << crew_id << std::endl;
        return valid_fdps;
    }
    
    // 获取机长的基地
    std::string base = crew->base;
    
    // 获取机长的资质（可执行的航班）
    const auto& qualified_flights = crew->qualified_flights;
    
    // 获取机长的地面任务（占位任务）
    const auto& ground_duties = crew->ground_duties;
    
    // 创建随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);
    
    // 遍历所有可能的执勤日，筛选出该机长可执行的
    for (const auto& [key, fdps] : data_.all_valid_fdps_) {
        const auto& [airport, date] = key;
        
        for (const auto& fdp : fdps) {
            bool is_valid = true;
            
            // 检查机长是否有资质执行FDP中的所有航班
            for (const auto& task : fdp.tasks) {
                if (task.task_type == "flight") {
                    if (qualified_flights.find(task.id) == qualified_flights.end()) {
                        is_valid = false;
                        break;
                    }
                }
            }
            
            // 检查FDP是否在非基地机场结束，如果是，则根据概率决定是否拒绝
            if (fdp.get_end_airport() != base) {
                // 生成一个随机数，如果小于拒绝概率，则拒绝该FDP
                if (dis(gen) < non_base_rejection_prob_) {
                    is_valid = false;
                }
            }
            
            // 检查占位任务限制
            const auto fdp_start = fdp.get_start_time();
            const auto fdp_end = fdp.get_end_time();
            
            for (size_t i = 0; i < ground_duties.size(); ++i) {
                const auto& duty = ground_duties[i];
                
                // 检查占位任务是否与FDP时间有重叠
                if (duty.start_time < fdp_end && duty.end_time > fdp_start) {
                    // 情况1：占位任务与FDP时间有重叠
                    
                    // 检查是否与FDP中的任务有时间重叠
                    for (const auto& task : fdp.tasks) {
                        if (task.start_time < duty.end_time && task.end_time > duty.start_time) {
                            is_valid = false;
                            break;
                        }
                    }
                    
                    if (!is_valid) break;
                    
                    // 找到占位任务前后的FDP任务
                    const Task* prev_task = nullptr;
                    const Task* next_task = nullptr;
                    
                    for (size_t j = 0; j < fdp.tasks.size(); ++j) {
                        if (fdp.tasks[j].end_time <= duty.start_time) {
                            prev_task = &fdp.tasks[j];
                        }
                        if (fdp.tasks[j].start_time >= duty.end_time) {
                            next_task = &fdp.tasks[j];
                            break;
                        }
                    }
                    
                    // 检查前后任务的机场是否为Base
                    if ((prev_task && prev_task->end_airport != base) || 
                        (next_task && next_task->start_airport != base)) {
                        is_valid = false;
                        break;
                    }
                } else {
                    // 情况2：占位任务在FDP时间范围外
                    
                    // 检查是否在FDP开始前12小时内
                    auto hours_before_fdp = std::chrono::duration_cast<std::chrono::hours>(
                        fdp_start - duty.end_time).count();
                    
                    if (hours_before_fdp >= 0 && hours_before_fdp <= 12 && duty.is_duty) {
                        is_valid = false;
                        break;
                    }
                    
                    // 检查是否在FDP结束后12小时内
                    auto hours_after_fdp = std::chrono::duration_cast<std::chrono::hours>(
                        duty.start_time - fdp_end).count();
                    
                    if (hours_after_fdp >= 0 && hours_after_fdp <= 12) {
                        if (fdp.get_end_airport() != base) {
                            is_valid = false;
                            break;
                        }
                    }
                }
            }
            
            
            if (is_valid) {
                valid_fdps.push_back(fdp);
            }
        }
    }
    
    // 存入缓存
    crew_valid_fdps_cache_[crew_id] = valid_fdps;
    return valid_fdps;
}

std::unordered_set<std::string> SubproblemSolver::getCachedFlightIds(const FDP& fdp) const {
    // 使用FDP的任务列表作为键
    std::string fdp_key;
    for (const auto& task : fdp.tasks) {
        fdp_key += task.id + "|";
    }
    
    auto it = fdp_flight_ids_cache_.find(fdp_key);
    if (it != fdp_flight_ids_cache_.end()) {
        return it->second;
    }
    
    // 由于是const方法，不能修改缓存，直接返回计算结果
    return fdp.get_included_flight_ids();
}

double SubproblemSolver::calculateFDPReward(const FDP& fdp, const std::unordered_map<std::string, double>& flight_duals) const {
    double reward = 0.0;
    
    // 使用缓存获取航班ID
    auto flight_ids = getCachedFlightIds(fdp);
    
    // 累加FDP中所有航班的对偶价值
    for (const auto& flight_id : flight_ids) {
        auto it = flight_duals.find(flight_id);
        if (it != flight_duals.end()) {
            reward += -it->second;
        }
    }
    
    return reward;
}

std::vector<FDP> SubproblemSolver::solveLongestPath(const std::string& crew_id, 
                                                  const std::vector<FDP>& valid_fdps,
                                                  double crew_dual) {
    // 如果没有有效的FDP，直接返回空结果
    if (valid_fdps.empty()) {
        return {};
    }
    
    // 尝试从文件加载网络
    FDPNetwork network;
    std::string file_path = getNetworkFilePath(crew_id);
    
    bool network_loaded = false;
    if (fs::exists(file_path)) {
        network_loaded = deserializeFDPNetwork(crew_id, file_path, network);
    }
    
    // 如果加载失败，构建新的网络并保存
    if (!network_loaded) {
        network = buildFDPNetwork(crew_id, valid_fdps);
        serializeFDPNetwork(crew_id, network, file_path);
    }
    
    // 更新网络中的奖励值
    updateNetworkRewards(network);
    
    // 使用网络求解最长路问题
    auto result = solveLongestPathWithNetwork(network, crew_dual, crew_id);
    
    // 使用完网络后，显式释放内存
    network = FDPNetwork(); // 清空网络数据
    
    return result;
}

// 辅助函数：按起始机场对FDP进行分组
std::unordered_map<std::string, std::vector<std::pair<size_t, const FDP*>>> 
SubproblemSolver::groupFDPsByStartAirport(const std::vector<FDP>& fdps) const {
    std::unordered_map<std::string, std::vector<std::pair<size_t, const FDP*>>> airport_groups;
    
    // 将FDP按起始机场分组，同时保存原始索引
    for (size_t i = 0; i < fdps.size(); ++i) {
        const FDP& fdp = fdps[i];
        airport_groups[fdp.get_start_airport()].emplace_back(i, &fdp);
    }
    
    // 对每个机场组内的FDP按开始时间排序
    for (auto& [airport, group] : airport_groups) {
        std::sort(group.begin(), group.end(),
                 [](const auto& a, const auto& b) {
                     return a.second->get_start_time() < b.second->get_start_time();
                 });
    }
    
    return airport_groups;
}

FDPNetwork SubproblemSolver::buildFDPNetwork(
    const std::string& crew_id, const std::vector<FDP>& valid_fdps) {
    
    FDPNetwork network;
    
    // 按开始时间排序FDP
    network.sorted_fdps = valid_fdps;
    std::sort(network.sorted_fdps.begin(), network.sorted_fdps.end(), 
              [](const FDP& a, const FDP& b) {
                  return a.get_start_time() < b.get_start_time();
              });
    
    // 计算每个FDP的奖励值
    size_t n = network.sorted_fdps.size();
    network.rewards.resize(n);
    for (size_t i = 0; i < n; ++i) {
        network.rewards[i] = calculateFDPReward(network.sorted_fdps[i], flight_duals_cache_);
    }
    
    // 构建邻接表表示有向无环图
    network.graph.resize(n + 2);
    network.source = n;     // 源点
    network.sink = n + 1;   // 汇点
    
    // 获取机组信息
    const Crew* crew = data_.get_crew(crew_id);
    if (!crew) {
        std::cerr << "错误：找不到机组 " << crew_id << std::endl;
        return network;
    }
    std::string base = crew->base;
    std::string initialStation = crew->initial_station;
    
    // 检查FDP之前是否有占位任务的辅助函数
    auto hasDutyBeforeFDP = [&crew](const FDP& fdp) -> bool {
        for (const auto& duty : crew->ground_duties) {
            if (duty.end_time <= fdp.get_start_time()) {
                return true;  
            }
        }
        return false;
    };
    
    // 检查FDP之后是否有占位任务的辅助函数
    auto hasDutyAfterFDP = [&crew](const FDP& fdp) -> bool {
        for (const auto& duty : crew->ground_duties) {
            if (duty.start_time >= fdp.get_end_time()) {
                return true;
            }
        }
        return false;
    };
    
    // 从源点到符合条件的FDP的边
    for (size_t i = 0; i < n; ++i) {
        const FDP& fdp = network.sorted_fdps[i];
        bool canConnectFromSource = false;
        
        // debug
        auto start_time = fdp.get_start_time();
        auto ground_duties = crew->ground_duties;
        if (hasDutyBeforeFDP(fdp)) {
            // 如果FDP之前有占位任务，起点必须是base
            if (fdp.get_start_airport() == base) {
                canConnectFromSource = true;
            }
        } else {
            // 如果FDP之前没有占位任务，起点必须是initialStation
            if (fdp.get_start_airport() == initialStation) {
                canConnectFromSource = true;
            }
        }
        
        if (canConnectFromSource) {
            network.graph[network.source].push_back(i);
        }
    }
    
    // 按起始机场对FDP进行分组
    auto airport_groups = groupFDPsByStartAirport(network.sorted_fdps);
    
    // 为每个FDP建立连接
    for (size_t i = 0; i < n; ++i) {
        const FDP& current_fdp = network.sorted_fdps[i];
        
        // 符合条件的FDP到汇点的边
        bool canConnectToSink = false;
        if (hasDutyAfterFDP(current_fdp)) {
            // 如果FDP之后有占位任务，终点必须是base
            if (current_fdp.get_end_airport() == base) {
                canConnectToSink = true;
            }
        } else {
            // 如果FDP之后没有占位任务，任何FDP都可以连接到汇点
            canConnectToSink = true;
        }
        
        if (canConnectToSink) {
            network.graph[i].push_back(network.sink);
        }
        
        const std::string& end_airport = current_fdp.get_end_airport();
        const auto end_time = current_fdp.get_end_time();
        
        // 获取在结束机场开始的所有FDP
        auto it = airport_groups.find(end_airport);
        if (it == airport_groups.end()) continue;
        
        const auto potential_next_fdps = it->second;
        
        // 计算最早可能的开始时间（最短休息时间为12小时）
        auto earliest_start = end_time + std::chrono::hours(12);
        
        // 使用二分查找找到第一个可能连接的FDP
        auto lower = std::lower_bound(
            potential_next_fdps.begin(),
            potential_next_fdps.end(),
            earliest_start,
            [](const auto& fdp_pair, const auto& target_time) {
                return fdp_pair.second->get_start_time() < target_time;
            }
        );
        
        // 从找到的位置开始扫描，直到找到第一个不满足最大休息时间的FDP
        for (auto it = lower; it != potential_next_fdps.end(); ++it) {
            const size_t next_idx = it->first;
            const FDP* next_fdp = it->second;
            
            // 使用缓存检查连接性（这里主要检查其他条件）
            std::pair<const FDP*, const FDP*> fdp_pair(&current_fdp, next_fdp);
            bool can_connect;
            
            can_connect = canConnect(crew_id, current_fdp, *next_fdp);
            
            if (can_connect) {
                network.graph[i].push_back(next_idx);
            }
        }
    }
    
    return network;
}

void SubproblemSolver::updateNetworkRewards(FDPNetwork& network) {
    // 更新网络中每个FDP的奖励值
    for (size_t i = 0; i < network.sorted_fdps.size(); ++i) {
        network.rewards[i] = calculateFDPReward(network.sorted_fdps[i], flight_duals_cache_);
    }
}

// 辅助函数：计算两个时间点之间的日历天数（包含起止当天）
auto calculateCalendarDays = [](time_point start, time_point end) {
    auto start_day = std::chrono::floor<std::chrono::days>(start);
    auto end_day = std::chrono::floor<std::chrono::days>(end);
    return (end_day - start_day).count() + 1;
};

// 状态标签结构，用于记录路径和资源消耗
struct Label {
    std::vector<int> path;
    double reward = 0.0;
    std::chrono::minutes flight_time{0};
    time_point start_time;
    time_point end_time;

    // 启发式评估函数：单位时间内的奖励
    double get_profitability_metric() const {
        if (path.empty() || reward <= 0) {
            return std::numeric_limits<double>::lowest();
        }
        long calendar_duration_days = calculateCalendarDays(start_time, end_time);
        // 加上2天强制休息时间来计算总占用时间
        return reward / (calendar_duration_days + 2.0);
    }

    // 用于排序
    bool operator<(const Label& other) const {
        return get_profitability_metric() < other.get_profitability_metric();
    }
};

//==============================================================================
// 新实现：Beam + DP 组合求最优周期序列
//==============================================================================
std::vector<FDP> SubproblemSolver::solveLongestPathWithNetwork(
        const FDPNetwork& network,
        double           crew_dual,
        std::string      crew_id) {

    if (network.sorted_fdps.empty()) return {};

    // ---------- 1. 生成"候选飞行周期" --------------------------------------
    struct CycleInfo {
        std::vector<int> fdps;           // 节点索引序列
        time_point       start_day_tp;   // 日期 00:00
        time_point       end_day_tp;     // 日期 23:59
        std::chrono::minutes fly_minutes{};
        double           reward  = 0.0;
        bool             can_reach_sink = false;  // 是否能到达汇点（即在base机场结束）
    };
    std::vector<CycleInfo> cycles;
    const auto MAX_FLY   = std::chrono::hours(60);
    const int  MAX_DAY   = 4;                     // ≤4 日历日

    // 获取机组信息
    const Crew* crew = data_.get_crew(crew_id);
    if (!crew) {
        std::cerr << "错误：找不到机组 " << crew_id << std::endl;
        return {};
    }
    const auto& ground_duties = crew->ground_duties;

    auto day_floor = [](const time_point& tp){
        return std::chrono::floor<std::chrono::days>(tp);
    };

    // 计算完整日历日的辅助函数
    auto calculate_calendar_days = [](time_point start, time_point end) {
        auto start_day = std::chrono::floor<std::chrono::days>(start);
        auto end_day = std::chrono::floor<std::chrono::days>(end);
        return std::chrono::duration_cast<std::chrono::days>(end_day - start_day).count() -1;
    };

    // 获取FDP前后的占位任务
    auto get_duties_before_fdp = [&ground_duties](const FDP& fdp) -> std::vector<const GroundDuty*> {
        std::vector<const GroundDuty*> duties_before;
        for (const auto& duty : ground_duties) {
            if (duty.end_time <= fdp.get_start_time()) {
                duties_before.push_back(&duty);
            }
        }
        // 按结束时间降序排序，以便最近的占位任务在前面
        std::sort(duties_before.begin(), duties_before.end(), 
            [](const GroundDuty* a, const GroundDuty* b) {
                return a->end_time > b->end_time;
            });
        return duties_before;
    };

    auto get_duties_after_fdp = [&ground_duties](const FDP& fdp) -> std::vector<const GroundDuty*> {
        std::vector<const GroundDuty*> duties_after;
        for (const auto& duty : ground_duties) {
            if (duty.start_time >= fdp.get_end_time()) {
                duties_after.push_back(&duty);
            }
        }
        // 按开始时间升序排序，以便最近的占位任务在前面
        std::sort(duties_after.begin(), duties_after.end(), 
            [](const GroundDuty* a, const GroundDuty* b) {
                return a->start_time < b->start_time;
            });
        return duties_after;
    };

    // ↓ 每个源 -> Beam-Search 出一条本地"最佳周期"
    for (int src : network.graph[network.source]) {
        const FDP& first = network.sorted_fdps[src];
        CycleInfo base;
        base.fdps       = {src};
        base.fly_minutes= first.get_flight_hours();
        base.reward     = network.rewards[src];
        
        // 检查第一个FDP之前是否有紧密相连的占位任务
        auto duties_before = get_duties_before_fdp(first);
        if (!duties_before.empty()) {
            const GroundDuty* closest_duty = duties_before[0];
            int calendar_days = calculate_calendar_days(closest_duty->end_time, first.get_start_time());
            if (calendar_days < 2) {
                // 如果占位任务与FDP间隔小于两个完整日历日，周期开始时间应从占位任务开始计算
                auto farthest_duty = duties_before.back();
                base.start_day_tp = day_floor(farthest_duty->start_time);
            } else {
                base.start_day_tp = day_floor(first.get_start_time());
            }
        } else {
            base.start_day_tp = day_floor(first.get_start_time());
        }
        
        // 检查第一个FDP之后是否有紧密相连的占位任务
        auto duties_after = get_duties_after_fdp(first);
        if (!duties_after.empty()) {
            const GroundDuty* closest_duty = duties_after[0];
            int calendar_days = calculate_calendar_days(first.get_end_time(), closest_duty->start_time);
            if (calendar_days < 2) {
                // 如果占位任务与FDP间隔小于两个完整日历日，周期结束时间应考虑占位任务
                base.end_day_tp = day_floor(closest_duty->end_time);
            } else {
                base.end_day_tp = day_floor(first.get_end_time());
            }
        } else {
            base.end_day_tp = day_floor(first.get_end_time());
        }

        // 约束 1：累计飞行 ≤60h
        if (base.fly_minutes > MAX_FLY) continue;

        // 约束 2：周期跨度 ≤4 天
        auto span = std::chrono::duration_cast<std::chrono::days>(base.end_day_tp - base.start_day_tp).count() + 1;
        if (span > MAX_DAY) continue;
        
        // 检查第一个FDP是否在base机场结束
        if (first.get_end_airport() == crew->base) {
            base.can_reach_sink = true;
        } 
        
        std::vector<CycleInfo> beam{base};

        for (;;) {
            std::vector<CycleInfo> next;
            for (const auto& c : beam) {
                int last = c.fdps.back();
                for (int nxt : network.graph[last]) {
                    if (nxt==network.sink) continue;
                    const FDP& f = network.sorted_fdps[nxt];

                    CycleInfo tmp = c;
                    tmp.fdps.push_back(nxt);
                    tmp.fly_minutes += f.get_flight_hours();
                    tmp.reward     += network.rewards[nxt];
                    
                    // 检查新的最后一个FDP之后是否有紧密相连的占位任务
                    auto duties_after = get_duties_after_fdp(f);
                    if (!duties_after.empty()) {
                        const GroundDuty* closest_duty = duties_after[0];
                        int calendar_days = calculate_calendar_days(f.get_end_time(), closest_duty->start_time);
                        if (calendar_days < 2) {
                            // 如果占位任务与FDP间隔小于两个完整日历日，周期结束时间应考虑占位任务
                            auto farthest_duty = duties_after.back();
                            tmp.end_day_tp = day_floor(farthest_duty->end_time);
                        } else {
                            tmp.end_day_tp = day_floor(f.get_end_time());
                        }
                    } else {
                        tmp.end_day_tp = day_floor(f.get_end_time());
                    }
                    
                    // 检查新的最后一个FDP是否在base机场结束
                    if (f.get_end_airport() == crew->base) {
                        tmp.can_reach_sink = true;
                    } else {
                        tmp.can_reach_sink = false;
                    }
                    
                    // 约束 1：累计飞行 ≤60h
                    if (tmp.fly_minutes > MAX_FLY) continue;
                    
                    // 约束 2：周期跨度 ≤4 天
                    auto span = std::chrono::duration_cast<std::chrono::days>(tmp.end_day_tp - tmp.start_day_tp).count() + 1;
                    if (span > MAX_DAY) continue;

                    if (tmp.can_reach_sink) {
                        next.push_back(std::move(tmp));
                    }
                }
            }
            if (next.empty()) break;
            std::sort(next.begin(), next.end(),
                [](const CycleInfo& a, const CycleInfo& b){return a.reward > b.reward;});
            if (next.size() > beam_width_) next.resize(beam_width_);
            beam.swap(next);
        }
        // beam 里保留的都是以 src 开头的最优周期
        for (auto& c:beam) cycles.push_back(std::move(c));
    }

    if (cycles.empty()) return {};

    // ---------- 2. 离散化日历轴，做后向 DP ----------------------------------
    // 取全局最早＆最晚日期
    auto min_day = cycles[0].start_day_tp;
    auto max_day = cycles[0].end_day_tp;
    for (auto& c:cycles){
        min_day = std::min(min_day,c.start_day_tp);
        max_day = std::max(max_day,c.end_day_tp);
    }
    int D = std::chrono::duration_cast<std::chrono::days>(max_day - min_day).count()+1;        // 总天数
    std::vector<double> dp(D, 0.0);             // +3 留两天休息边界

    // 预索引：某天开始的所有周期
    std::vector<std::vector<int>> day2cycles(D);
    for (int i=0;i<(int)cycles.size();++i){
        int d = std::chrono::duration_cast<std::chrono::days>(cycles[i].start_day_tp - min_day).count();
        day2cycles[d].push_back(i);
    }

    // 后向 DP
    for (int d=D-1; d>=0; --d){
        double best = dp[d+1];                    // 休息一天
        for(int idx: day2cycles[d]){
            auto& c = cycles[idx];
            // 只考虑能够回到汇点的周期（即在base机场结束的周期）
            if (!c.can_reach_sink) continue;
            
            int  dur = std::chrono::duration_cast<std::chrono::days>(c.end_day_tp - c.start_day_tp).count()+1;
            int  nxt = d + dur + 2;               // 强制休息两天
            if (nxt > D) nxt = D;
            best = std::max(best, c.reward + dp[nxt]);
        }
        dp[d] = best;
    }

    double total_profitability = dp[0] - crew_dual;
    if (total_profitability <= 1e-6) {
        // 如果这位机长最优的完整计划都无法创造正的盈利，
        // 那么就没有任何值得添加的新列。
        return {};
    }

    // ---------- 3. 回溯得到首个 reduced-cost>0 的周期 -------------------------
    std::vector<FDP> chosen_cycle;
    int cur = 0;
    while(cur < D){
        // 若休息是最佳
        if (dp[cur] == dp[cur+1]) { ++cur; continue; }

        // 找到使值提升的周期
        for(int idx: day2cycles[cur]){
            auto& c = cycles[idx];
            // 只考虑能够回到汇点的周期（即在base机场结束的周期）
            if (!c.can_reach_sink) continue;
            
            int dur = std::chrono::duration_cast<std::chrono::days>(c.end_day_tp-c.start_day_tp).count()+1;
            int nxt = cur+dur+2;
            if (nxt>D) nxt=D;
            if (std::abs(dp[cur] - (c.reward+dp[nxt])) < 1e-6){   // 选中
                double red_cost = c.reward - crew_dual;
                if (red_cost > 1e-6){           // 找到正 reduced cost
                    for(int node: c.fdps)
                        chosen_cycle.push_back(network.sorted_fdps[node]);
                    return chosen_cycle;
                }
                cur = nxt;                      // 否则继续往后找
                break;
            }
        }
    }
    return {};
}

bool SubproblemSolver::canConnect(std::string crew_id, const FDP& fdp1, const FDP& fdp2) const {
    // 检查时间顺序
    if (fdp1.get_end_time() >= fdp2.get_start_time()) {
        return false;
    }
    
    // 检查最小休息时间
    auto rest_time = std::chrono::duration_cast<std::chrono::hours>(
        fdp2.get_start_time() - fdp1.get_end_time());
    if (rest_time.count() < 12) {
        return false;
    }

    const Crew* crew = data_.get_crew(crew_id);
    std::string base = crew->base;
    
    // 检查两个FDP之间是否有占位任务
    bool has_duty_between = false;
    for (const auto& duty : crew->ground_duties) {
        // 检查占位任务是否在两个FDP之间
        // 占位任务的开始时间在fdp1结束之后，结束时间在fdp2开始之前
        if (duty.start_time >= fdp1.get_end_time() && duty.end_time <= fdp2.get_start_time()) {
            has_duty_between = true;
            // 如果有占位任务，则要求两个FDP的相关机场都必须是基地机场
            if (fdp1.get_end_airport() != base || fdp2.get_start_airport() != base) {
                return false;
            }
        }
    }
    
    // 如果没有占位任务，则只需要检查位置连接性
    if (!has_duty_between) {
        if (fdp1.get_end_airport() != fdp2.get_start_airport()) {
            return false;
        }
    }
    
    return true;
}

double SubproblemSolver::calculateRestCost(const FDP& fdp1, const FDP& fdp2) const {
    // 简化的休息成本计算
    // 如果在基地外过夜，有额外成本
    double cost = 0.0;
    
    // 获取第一个机长的基地（简化处理，假设所有FDP属于同一个机长）
    std::string base = "";
    if (!data_.get_all_crews().empty()) {
        base = data_.get_all_crews().begin()->second.base;
    }
    
    // 如果在非基地机场过夜，添加成本
    if (fdp1.get_end_airport() != base) {
        cost += 100.0; // 简化的过夜成本
    }
    
    // 休息时间越长，成本越高
    auto rest_time = std::chrono::duration_cast<std::chrono::hours>(
        fdp2.get_start_time() - fdp1.get_end_time());
    cost += rest_time.count() * 5.0; // 每小时5单位成本
    
    return cost;
}

void SubproblemSolver::precomputeAllFDPNetworks() {
    std::cout << "开始预处理所有机组的FDP网络..." << std::endl;
    
    // 获取所有机组ID
    std::vector<std::string> all_crew_ids;
    for (const auto& [crew_id, crew] : data_.get_all_crews()) {
        std::string file_path = getNetworkFilePath(crew_id);
        if (fs::exists(file_path)) {
            continue;
        }
        all_crew_ids.push_back(crew_id);
    }
    
    // 创建网络存储目录
    if (!fs::exists(network_directory_)) {
        try {
            fs::create_directories(network_directory_);
        } catch (const std::exception& e) {
            std::cerr << "创建网络目录失败: " << e.what() << std::endl;
            return;
        }
    }
    
    // 单线程处理所有机组的FDP网络
    size_t total_count = all_crew_ids.size();
    size_t processed_count = 0;
    
    for (const std::string& crew_id : all_crew_ids) {
        // 过滤有效的FDP
        std::vector<FDP> valid_fdps = filterValidFDPs(crew_id);
        
        if (!valid_fdps.empty()) {
            // 构建网络
            FDPNetwork network = buildFDPNetwork(crew_id, valid_fdps);
            
            // 保存到文件
            std::string file_path = getNetworkFilePath(crew_id);
            serializeFDPNetwork(crew_id, network, file_path);
        }
        
        // 更新进度
        processed_count++;
        
        std::cout << "已处理 " << processed_count << " / " << total_count 
                 << " 个机组的FDP网络 (" 
                 << std::fixed << std::setprecision(1) 
                 << (100.0 * processed_count / total_count) << "%)" << std::endl;
    }
    
    std::cout << "所有机组的FDP网络计算完成并保存到 " << network_directory_ << " 目录" << std::endl;
}


std::string SubproblemSolver::getNetworkFilePath(const std::string& crew_id) const {
    return network_directory_ + "/" + crew_id + ".fdp";
}

bool SubproblemSolver::serializeFDPNetwork(const std::string& crew_id, const FDPNetwork& network, 
                                          const std::string& filename) const {
    try {
        // 创建父目录
        fs::path file_path(filename);
        std::cout << "准备写入FDP网络文件: " << file_path << std::endl;
        std::cout << "父目录: " << file_path.parent_path() << std::endl;
        
        // 打开文件
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件进行写入: " << filename << std::endl;
            return false;
        }
        
        // 写入网络基本信息
        size_t n = network.sorted_fdps.size();
        file.write(reinterpret_cast<const char*>(&n), sizeof(n));
        file.write(reinterpret_cast<const char*>(&network.source), sizeof(network.source));
        file.write(reinterpret_cast<const char*>(&network.sink), sizeof(network.sink));
        
        // 写入FDP序列
        for (const auto& fdp : network.sorted_fdps) {
            std::string fdp_str = serializeFDP(fdp);
            size_t str_len = fdp_str.size();
            file.write(reinterpret_cast<const char*>(&str_len), sizeof(str_len));
            file.write(fdp_str.c_str(), str_len);
        }
        
        // 写入图结构
        size_t graph_size = network.graph.size();
        file.write(reinterpret_cast<const char*>(&graph_size), sizeof(graph_size));
        
        for (const auto& adj_list : network.graph) {
            size_t adj_size = adj_list.size();
            file.write(reinterpret_cast<const char*>(&adj_size), sizeof(adj_size));
            
            for (int node : adj_list) {
                file.write(reinterpret_cast<const char*>(&node), sizeof(node));
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "序列化FDP网络失败: " << e.what() << std::endl;
        return false;
    }
}

bool SubproblemSolver::deserializeFDPNetwork(const std::string& crew_id, const std::string& filename, 
                                            FDPNetwork& network) {
    try {
        // 打开文件
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // 读取网络基本信息
        size_t n;
        file.read(reinterpret_cast<char*>(&n), sizeof(n));
        file.read(reinterpret_cast<char*>(&network.source), sizeof(network.source));
        file.read(reinterpret_cast<char*>(&network.sink), sizeof(network.sink));
        
        // 读取FDP序列
        network.sorted_fdps.clear();
        network.sorted_fdps.reserve(n);
        
        for (size_t i = 0; i < n; ++i) {
            size_t str_len;
            file.read(reinterpret_cast<char*>(&str_len), sizeof(str_len));
            
            std::string fdp_str(str_len, '\0');
            file.read(&fdp_str[0], str_len);
            
            FDP fdp = deserializeFDP(fdp_str);
            network.sorted_fdps.push_back(fdp);
        }
        
        // 读取图结构
        size_t graph_size;
        file.read(reinterpret_cast<char*>(&graph_size), sizeof(graph_size));
        
        network.graph.clear();
        network.graph.resize(graph_size);
        
        for (size_t i = 0; i < graph_size; ++i) {
            size_t adj_size;
            file.read(reinterpret_cast<char*>(&adj_size), sizeof(adj_size));
            
            network.graph[i].reserve(adj_size);
            for (size_t j = 0; j < adj_size; ++j) {
                int node;
                file.read(reinterpret_cast<char*>(&node), sizeof(node));
                network.graph[i].push_back(node);
            }
        }
        
        // 初始化奖励值
        network.rewards.resize(n, 0.0);
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "反序列化FDP网络失败: " << e.what() << std::endl;
        return false;
    }
}

std::string SubproblemSolver::serializeFDP(const FDP& fdp) const {
    std::stringstream ss;
    
    // 写入任务数量
    ss << fdp.tasks.size() << "|";
    
    // 序列化每个任务
    for (const auto& task : fdp.tasks) {
        ss << task.id << "|";
        ss << task.task_type << "|";
        ss << task.start_airport << "|";
        ss << task.end_airport << "|";
        
        // 转换时间点为字符串，使用GMT时间避免时区问题
        auto to_time_string = [](const time_point& tp) {
            auto time_t = std::chrono::system_clock::to_time_t(tp);
            std::tm* tm = std::gmtime(&time_t);
            char buffer[32];
            std::strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", tm);
            return std::string(buffer);
        };
        
        ss << to_time_string(task.start_time) << "|";
        ss << to_time_string(task.end_time) << "|";
        ss << task.fly_time.count() << "|";
        ss << task.aircraft_no << "|";
    }
    
    return ss.str();
}

FDP SubproblemSolver::deserializeFDP(const std::string& str) const {
    FDP fdp;
    std::stringstream ss(str);
    std::string token;
    
    // 读取任务数量
    std::getline(ss, token, '|');
    int task_count = std::stoi(token);
    
    // 解析每个任务
    for (int i = 0; i < task_count; ++i) {
        Task task;
        
        std::getline(ss, task.id, '|');
        std::getline(ss, task.task_type, '|');
        std::getline(ss, task.start_airport, '|');
        std::getline(ss, task.end_airport, '|');
        
        // 从字符串转换时间点，使用GMT时间避免时区问题
        std::string time_str;
        std::getline(ss, time_str, '|');
        auto from_time_string = [](const std::string& time_str) {
            std::tm tm = {};
            std::istringstream iss(time_str);
            iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            // 将GMT时间转换为时间点
            tm.tm_isdst = 0; // 禁用夏令时
            return std::chrono::system_clock::from_time_t(timegm(&tm));
        };
        task.start_time = from_time_string(time_str);
        
        std::getline(ss, time_str, '|');
        task.end_time = from_time_string(time_str);
        
        std::string minutes_str;
        std::getline(ss, minutes_str, '|');
        task.fly_time = std::chrono::minutes(std::stoi(minutes_str));
        
        std::getline(ss, task.aircraft_no, '|');
        
        fdp.tasks.push_back(task);
    }
    
    return fdp;
}

// ==============================================================================
// Serialization Test
// ==============================================================================

bool SubproblemSolver::compareTasks(const Task& t1, const Task& t2) const {
    if (t1.id != t2.id) {
        std::cout << "  - Task ID mismatch: " << t1.id << " vs " << t2.id << std::endl;
        return false;
    }
    if (t1.task_type != t2.task_type) {
        std::cout << "  - Task type mismatch: " << t1.task_type << " vs " << t2.task_type << std::endl;
        return false;
    }
    if (t1.start_airport != t2.start_airport) {
        std::cout << "  - Task start_airport mismatch: " << t1.start_airport << " vs " << t2.start_airport << std::endl;
        return false;
    }
    if (t1.end_airport != t2.end_airport) {
        std::cout << "  - Task end_airport mismatch: " << t1.end_airport << " vs " << t2.end_airport << std::endl;
        return false;
    }
    if (t1.start_time != t2.start_time) {
        std::cout << "  - Task start_time mismatch" << std::endl;
        return false;
    }
    if (t1.end_time != t2.end_time) {
        std::cout << "  - Task end_time mismatch" << std::endl;
        return false;
    }
    if (t1.fly_time != t2.fly_time) {
        std::cout << "  - Task fly_time mismatch" << std::endl;
        return false;
    }
    if (t1.aircraft_no != t2.aircraft_no) {
        std::cout << "  - Task aircraft_no mismatch: " << t1.aircraft_no << " vs " << t2.aircraft_no << std::endl;
        return false;
    }
    return true;
}

bool SubproblemSolver::compareFDPs(const FDP& f1, const FDP& f2) const {
    if (f1.tasks.size() != f2.tasks.size()) {
        std::cout << " - FDP tasks size mismatch: " << f1.tasks.size() << " vs " << f2.tasks.size() << std::endl;
        return false;
    }
    for (size_t i = 0; i < f1.tasks.size(); ++i) {
        if (!compareTasks(f1.tasks[i], f2.tasks[i])) {
            std::cout << " - Difference in Task at index " << i << std::endl;
            return false;
        }
    }
    return true;
}

bool SubproblemSolver::compareNetworks(const FDPNetwork& n1, const FDPNetwork& n2) const {
    bool is_identical = true;

    if (n1.source != n2.source) {
        std::cout << "Network source mismatch: " << n1.source << " vs " << n2.source << std::endl;
        is_identical = false;
    }

    if (n1.sink != n2.sink) {
        std::cout << "Network sink mismatch: " << n1.sink << " vs " << n2.sink << std::endl;
        is_identical = false;
    }

    if (n1.sorted_fdps.size() != n2.sorted_fdps.size()) {
        std::cout << "Network sorted_fdps size mismatch: " << n1.sorted_fdps.size() << " vs " << n2.sorted_fdps.size() << std::endl;
        return false; // Fatal difference, no need to continue
    }

    for (size_t i = 0; i < n1.sorted_fdps.size(); ++i) {
        if (!compareFDPs(n1.sorted_fdps[i], n2.sorted_fdps[i])) {
            std::cout << "Difference in FDP at index " << i << std::endl;
            is_identical = false;
        }
    }

    if (n1.graph.size() != n2.graph.size()) {
        std::cout << "Network graph size mismatch: " << n1.graph.size() << " vs " << n2.graph.size() << std::endl;
        return false; // Fatal difference
    }
    
    // Note: We don't compare 'rewards' as it's not part of the serialization.
    // It is calculated dynamically after loading.

    for (size_t i = 0; i < n1.graph.size(); ++i) {
        // Sort adjacency lists to ensure order-independent comparison
        auto adj1 = n1.graph[i];
        auto adj2 = n2.graph[i];
        std::sort(adj1.begin(), adj1.end());
        std::sort(adj2.begin(), adj2.end());

        if (adj1 != adj2) {
            std::cout << "Difference in graph adjacency list for node " << i << std::endl;
            is_identical = false;
        }
    }

    return is_identical;
}


void SubproblemSolver::testSerialization(const std::string& crew_id) {
    std::cout << "\n======================================================\n";
    std::cout << "开始对机组 " << crew_id << " 进行网络序列化测试" << std::endl;
    std::cout << "======================================================\n";

    // 1. 过滤该机组有效的FDPs
    std::vector<FDP> valid_fdps = filterValidFDPs(crew_id);
    if (valid_fdps.empty()) {
        std::cout << "机组 " << crew_id << " 没有有效的FDP，无法进行测试。" << std::endl;
        return;
    }
    std::cout << "找到 " << valid_fdps.size() << " 个有效FDP。" << std::endl;

    // 2. 在内存中构建原始网络
    std::cout << "在内存中构建原始FDP网络..." << std::endl;
    FDPNetwork original_network = buildFDPNetwork(crew_id, valid_fdps);
    std::cout << "原始网络构建完成。" << std::endl;
    
    // 打印原始网络的基本信息
    std::cout << "原始网络信息：" << std::endl;
    std::cout << " - FDP数量: " << original_network.sorted_fdps.size() << std::endl;
    std::cout << " - 源点索引: " << original_network.source << std::endl;
    std::cout << " - 汇点索引: " << original_network.sink << std::endl;
    std::cout << " - 源点连接数: " << original_network.graph[original_network.source].size() << std::endl;
    
    // 统计连接到汇点的节点数
    size_t sink_connections = 0;
    for (size_t i = 0; i < original_network.sorted_fdps.size(); ++i) {
        auto& adj = original_network.graph[i];
        if (std::find(adj.begin(), adj.end(), original_network.sink) != adj.end()) {
            sink_connections++;
        }
    }
    std::cout << " - 连接到汇点的节点数: " << sink_connections << std::endl;

    // 3. 将网络序列化到临时文件
    std::string temp_filename = "temp_network_test_" + crew_id + ".fdp";
    std::cout << "序列化网络到临时文件: " << temp_filename << "..." << std::endl;
    if (!serializeFDPNetwork(crew_id, original_network, temp_filename)) {
        std::cerr << "序列化网络失败。测试中止。" << std::endl;
        return;
    }
    std::cout << "序列化成功。" << std::endl;

    // 4. 从文件反序列化网络
    FDPNetwork deserialized_network;
    std::cout << "从文件反序列化网络..." << std::endl;
    if (!deserializeFDPNetwork(crew_id, temp_filename, deserialized_network)) {
        std::cerr << "反序列化网络失败。测试中止。" << std::endl;
        fs::remove(temp_filename);
        return;
    }
    std::cout << "反序列化成功。" << std::endl;
    
    // 打印反序列化网络的基本信息
    std::cout << "反序列化网络信息：" << std::endl;
    std::cout << " - FDP数量: " << deserialized_network.sorted_fdps.size() << std::endl;
    std::cout << " - 源点索引: " << deserialized_network.source << std::endl;
    std::cout << " - 汇点索引: " << deserialized_network.sink << std::endl;
    std::cout << " - 源点连接数: " << deserialized_network.graph[deserialized_network.source].size() << std::endl;
    
    // 统计连接到汇点的节点数
    sink_connections = 0;
    for (size_t i = 0; i < deserialized_network.sorted_fdps.size(); ++i) {
        auto& adj = deserialized_network.graph[i];
        if (std::find(adj.begin(), adj.end(), deserialized_network.sink) != adj.end()) {
            sink_connections++;
        }
    }
    std::cout << " - 连接到汇点的节点数: " << sink_connections << std::endl;

    // 5. 比较两个网络
    std::cout << "\n--- 比较原始网络和反序列化网络 ---\n" << std::endl;
    bool are_identical = compareNetworks(original_network, deserialized_network);

    std::cout << "\n--- 测试结果 ---\n";
    if (are_identical) {
        std::cout << "成功：原始网络和反序列化网络完全一致。" << std::endl;
    } else {
        std::cout << "失败：在原始网络和反序列化网络之间发现差异。" << std::endl;
    }
     std::cout << "======================================================\n" << std::endl;

    // 6. 清理临时文件
    fs::remove(temp_filename);
    std::cout << "已清理临时文件: " << temp_filename << std::endl;
}

