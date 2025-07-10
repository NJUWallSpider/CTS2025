#include "SubproblemSolver.h"
#include "../gurobi_solvers/MasterProblem.h"
#include <algorithm>
#include <limits>
#include <map>
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

void SubproblemSolver::clearCrewSpecificCache() {
    crew_valid_fdps_cache_.clear();
    fdp_flight_ids_cache_.clear();
    connectivity_cache_.clear();
}

void SubproblemSolver::clearCache() {
    clearCrewSpecificCache();
    flight_duals_cache_.clear();
}

bool SubproblemSolver::solveForCrew(const std::string& crew_id) {
    // 使用已缓存的对偶值
    updateDuals();
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

FDPNetwork SubproblemSolver::buildFDPNetwork(const std::string& crew_id, const std::vector<FDP>& valid_fdps) {
    
    FDPNetwork network;
    
    // 按开始时间排序FDP
    network.sorted_fdps = valid_fdps;
    std::sort(network.sorted_fdps.begin(), network.sorted_fdps.end(), 
              [](const FDP& a, const FDP& b) {
                  return a.get_start_time() < b.get_start_time();
              });
    
    for (int i = 0; i < network.sorted_fdps.size(); ++i) {
        network.sorted_fdps[i].id = i;
    }
    
    // 计算每个FDP的奖励值
    size_t n = network.sorted_fdps.size();
    network.rewards.resize(n);
    for (size_t i = 0; i < n; ++i) {
        network.rewards[i] = calculateFDPReward(network.sorted_fdps[i], flight_duals_cache_);
    }
    
    // 获取机组信息
    const Crew* crew = data_.get_crew(crew_id);
    if (!crew) {
        std::cerr << "错误：找不到机组 " << crew_id << std::endl;
        return network;
    }
    std::string base = crew->base;
    std::string initialStation = crew->initial_station;
    
    // 收集所有唯一的(机场,时间点)节点
    std::unordered_map<NetworkNode, size_t, NetworkNodeHash> node_map;
    
    // 添加源点和汇点
    NetworkNode source_node = {"SOURCE", time_point()};
    NetworkNode sink_node = {"SINK", time_point()};
    
    // 添加源点和汇点到节点集合
    node_map[source_node] = 0;
    node_map[sink_node] = 1;
    network.nodes.push_back(source_node);
    network.nodes.push_back(sink_node);
    
    // 为每个FDP的起点和终点创建节点
    for (size_t i = 0; i < n; ++i) {
        const FDP& fdp = network.sorted_fdps[i];
        
        // 创建起点节点
        NetworkNode start_node = {fdp.get_start_airport(), fdp.get_start_time()};
        if (node_map.find(start_node) == node_map.end()) {
            node_map[start_node] = network.nodes.size();
            network.nodes.push_back(start_node);
        }
        
        // 创建终点节点
        NetworkNode end_node = {fdp.get_end_airport(), fdp.get_end_time()};
        if (node_map.find(end_node) == node_map.end()) {
            node_map[end_node] = network.nodes.size();
            network.nodes.push_back(end_node);
        }

        // 创建从节点到FDP的映射
        network.node_to_fdp_start[start_node].push_back(i);
        network.node_to_fdp_end[end_node].push_back(i);
    }
    
    // 初始化图结构
    network.source = 0;
    network.sink = 1;
    network.graph.resize(network.nodes.size());
    
    // 为每个FDP创建边
    for (int i = 0; i < n; ++i) {
        const FDP& fdp = network.sorted_fdps[i];
        size_t start_node_idx = node_map[{fdp.get_start_airport(), fdp.get_start_time()}];
        size_t end_node_idx = node_map[{fdp.get_end_airport(), fdp.get_end_time()}];
        
        // 创建从起点到终点的边
        EdgeInfo edge_info;
        edge_info.best_fdp_idx = static_cast<int>(i);
        edge_info.reward = network.rewards[i];
        
        // 添加新边
        network.graph[start_node_idx].push_back({end_node_idx, edge_info});
        
        // 记录FDP到边的映射，用于快速更新
        network.fdp_to_edge[i] = {start_node_idx, end_node_idx};
    }
    
    // 从源点到符合条件的起点节点添加边
    for (size_t i = 2; i < network.nodes.size(); ++i) { // 跳过源点和汇点
        const auto& node = network.nodes[i];
        std::string airport = node.airport;
        time_point time = node.time;
        
        // 检查是否有FDP以该节点为起点
        bool is_start_node = false;
        auto it = network.node_to_fdp_start.find(node);
        if (it != network.node_to_fdp_start.end()) {
            is_start_node = true;
        }
        
        if (!is_start_node) continue; // 如果没有FDP以该节点为起点，跳过
        
        bool canConnectFromSource = false;
        
        // 检查是否在该时间点之前有占位任务
        bool has_duty_before = false;
        for (const auto& duty : crew->ground_duties) {
            if (duty.end_time <= time) {
                has_duty_before = true;
                break;
            }
        }
        
        if (has_duty_before) {
            // 如果时间点之前有占位任务，起点必须是base
            if (airport == base) {
                canConnectFromSource = true;
            }
        } else {
            // 如果时间点之前没有占位任务，起点必须是initialStation
            if (airport == initialStation) {
                canConnectFromSource = true;
            }
        }
        
        if (canConnectFromSource) {
            // 创建从源点到起点节点的边
            EdgeInfo edge_info;
            edge_info.best_fdp_idx = -1; // 源点到起点没有对应的FDP
            edge_info.reward = 0.0;
            
            network.graph[network.source].push_back({i, edge_info});
        }
    }
    
    // 从符合条件的终点节点到汇点添加边
    for (size_t i = 2; i < network.nodes.size(); ++i) { // 跳过源点和汇点
        const auto& node = network.nodes[i];
        std::string airport = node.airport;
        time_point time = node.time;
        
        // 检查是否有FDP以该节点为终点
        bool is_end_node = false;
        auto it = network.node_to_fdp_end.find(node);
        if (it != network.node_to_fdp_end.end()) {
            is_end_node = true;
        }
        
        if (!is_end_node) continue; // 如果没有FDP以该节点为终点，跳过
        
        bool canConnectToSink = false;
        
        // 检查是否在该时间点之后有占位任务
        bool has_duty_after = false;
        for (const auto& duty : crew->ground_duties) {
            if (duty.start_time >= time) {
                has_duty_after = true;
                break;
            }
        }
        
        if (has_duty_after) {
            // 如果时间点之后有占位任务，终点必须是base
            if (airport == base) {
                canConnectToSink = true;
            }
        } else {
            // 如果时间点之后没有占位任务，任何终点都可以连接到汇点
            canConnectToSink = true;
        }
        
        if (canConnectToSink) {
            // 创建从终点节点到汇点的边
            EdgeInfo edge_info;
            edge_info.best_fdp_idx = -1; // 终点到汇点没有对应的FDP
            edge_info.reward = 0.0;
            
            network.graph[i].push_back({network.sink, edge_info});
        }
    }
    
    return network;
}

void SubproblemSolver::updateNetworkRewards(FDPNetwork& network) {
    // 更新每个FDP的奖励值
    for (size_t i = 0; i < network.sorted_fdps.size(); ++i) {
        network.rewards[i] = calculateFDPReward(network.sorted_fdps[i], flight_duals_cache_);
        
        // 更新所有使用此FDP的边
        int fdp_idx = static_cast<int>(i);
        auto pair = network.fdp_to_edge[fdp_idx];
        size_t from_node = pair.first;
        size_t to_node = pair.second;
                
        // 查找并更新边的奖励值
        for (auto& edge : network.graph[from_node]) {
            if (edge.first == to_node && edge.second.reward < network.rewards[fdp_idx]) {
                edge.second.best_fdp_idx = fdp_idx;
                edge.second.reward = network.rewards[fdp_idx];
                break;
            }
        }
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
        std::vector<int> fdps;           // FDP索引序列
        time_point       start_day_tp;   // 日期 00:00
        time_point       end_day_tp;     // 日期 23:59
        std::chrono::minutes fly_minutes{};
        double           reward  = 0.0;
        bool             can_reach_sink = false;  // 是否能到达汇点（即在base机场结束）
        
        // 记录周期的起止机场，便于周期级连接判断
        std::string      start_airport;
        std::string      end_airport;
        
        // 添加比较运算符用于排序
        bool operator>(const CycleInfo& other) const {
            return reward > other.reward;
        }
        
        // 为优先队列添加比较运算符（最小堆，reward较小的在顶部）
        bool operator<(const CycleInfo& other) const {
            return reward < other.reward;
        }
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
    std::string base = crew->base;

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

    // 预处理：为每个机场建立时间索引
    std::unordered_map<std::string, std::map<time_point, size_t>> airport_time_index;
    for (size_t i = 2; i < network.nodes.size(); ++i) {
        const auto& node = network.nodes[i];
        if (i != network.source && i != network.sink) {
            airport_time_index[node.airport][node.time] = i;
        }
    }

    // 预处理：缓存每个节点是否可以到达汇点
    std::vector<bool> can_reach_sink(network.nodes.size(), false);
    for (size_t i = 0; i < network.nodes.size(); ++i) {
        for (const auto& edge : network.graph[i]) {
            if (edge.first == network.sink) {
                can_reach_sink[i] = true;
                break;
            }
        }
    }

    // 使用新的网络结构生成候选周期
    // 从源点开始的所有路径
    for (const auto& source_edge : network.graph[network.source]) {
        size_t start_node_idx = source_edge.first;
        
        // 使用Beam Search找出从每个起点节点出发的最优路径
        std::vector<CycleInfo> beam;
        
        // 初始化beam搜索的起点
        for (const auto& first_edge : network.graph[start_node_idx]) {
            size_t next_node_idx = first_edge.first;
            int fdp_idx = first_edge.second.best_fdp_idx;
            if (fdp_idx == -1) continue;
            const FDP& first_fdp = network.sorted_fdps[fdp_idx];
            
            CycleInfo base_cycle;
            base_cycle.fdps = {fdp_idx};
            base_cycle.fly_minutes = first_fdp.get_flight_hours();
            base_cycle.reward = first_edge.second.reward;
            base_cycle.start_airport = first_fdp.get_start_airport();
            base_cycle.end_airport   = first_fdp.get_end_airport();
        
            // 检查第一个FDP之前是否有紧密相连的占位任务
            auto duties_before = get_duties_before_fdp(first_fdp);
            if (!duties_before.empty()) {
                const GroundDuty* closest_duty = duties_before[0];
                int calendar_days = calculate_calendar_days(closest_duty->end_time, first_fdp.get_start_time());
                if (calendar_days < 2) {
                    auto farthest_duty = duties_before.back();
                    base_cycle.start_day_tp = day_floor(farthest_duty->start_time);
                } else {
                    base_cycle.start_day_tp = day_floor(first_fdp.get_start_time());
                }
            } else {
                base_cycle.start_day_tp = day_floor(first_fdp.get_start_time());
            }
            
            // 检查第一个FDP之后是否有紧密相连的占位任务
            auto duties_after = get_duties_after_fdp(first_fdp);
            if (!duties_after.empty()) {
                const GroundDuty* closest_duty = duties_after[0];
                int calendar_days = calculate_calendar_days(first_fdp.get_end_time(), closest_duty->start_time);
                if (calendar_days < 2) {
                    auto farthest_duty = duties_after.back();
                    base_cycle.end_day_tp = day_floor(farthest_duty->end_time);
                } else {
                    base_cycle.end_day_tp = day_floor(first_fdp.get_end_time());
                }
            } else {
                base_cycle.end_day_tp = day_floor(first_fdp.get_end_time());
            }

            // 约束 1：累计飞行 ≤60h
            if (base_cycle.fly_minutes > MAX_FLY) continue;

            // 约束 2：周期跨度 ≤4 天
            auto span = std::chrono::duration_cast<std::chrono::days>(base_cycle.end_day_tp - base_cycle.start_day_tp).count() + 1;
            if (span > MAX_DAY) continue;
            
            // 检查第一个FDP
            if (can_reach_sink[next_node_idx]) {
                base_cycle.can_reach_sink = true;
            } 
            
            beam.push_back(std::move(base_cycle));
        }

        // 继续Beam Search过程
        for (;;) {
            auto compare_cycles = [](const CycleInfo& a, const CycleInfo& b) {
                return a.reward > b.reward;
            };
            std::priority_queue<CycleInfo, std::vector<CycleInfo>, decltype(compare_cycles)> next_beam_pq(compare_cycles);
            
            for (const auto& cycle : beam) {
                // 获取当前周期最后一个FDP
                int last_fdp_idx = cycle.fdps.back();
                const FDP& last_fdp = network.sorted_fdps[last_fdp_idx];
                
                // 找到最后一个FDP对应的终点节点
                auto pair_it = network.fdp_to_edge.find(last_fdp_idx);
                if (pair_it == network.fdp_to_edge.end()) {
                    continue;
                }
                auto pair = pair_it->second;
                size_t last_node_idx = pair.second;
                const NetworkNode& last_node = network.nodes[last_node_idx];
                
                // 使用机场索引快速查找下一个可能的节点
                auto it_airport = airport_time_index.find(last_node.airport);
                if (it_airport == airport_time_index.end()) continue;
                
                // 找到第一个时间满足最小休息时间的节点
                auto min_next_time = last_node.time + std::chrono::hours(12);
                auto it_time = it_airport->second.lower_bound(min_next_time);
                auto max_next_time = last_node.time + std::chrono::hours(48);
                while (it_time != it_airport->second.end() && it_time->first < max_next_time) {
                    size_t next_node_idx = it_time->second;
                    const NetworkNode& next_node = network.nodes[next_node_idx];
                    
                    // 检查是否有FDP以该节点为起点
                    auto it_start = network.node_to_fdp_start.find(next_node);
                    if (it_start == network.node_to_fdp_start.end() || it_start->second.empty()) {
                        ++it_time;
                        continue;
                    }
                    
                    // 检查两个节点之间是否有占位任务
                    bool is_valid = true;
                    for (const auto& duty : ground_duties) {
                        if (duty.start_time >= last_node.time && duty.end_time <= next_node.time) {
                            if (last_node.airport != base) {
                                is_valid = false;
                                break;
                            }
                        }
                    }
                    
                    if (!is_valid) {
                        break;
                    }
                    
                    // 先收集所有边（不检查约束）
                    std::vector<std::pair<int, double>> all_edges;
                    all_edges.reserve(network.graph[next_node_idx].size());
                    
                    for (const auto& edge : network.graph[next_node_idx]) {
                        if (edge.first == network.sink) continue;
                        all_edges.emplace_back(edge.second.best_fdp_idx, edge.second.reward);
                    }
                    
                    // 按reward降序排序所有边
                    std::sort(all_edges.begin(), all_edges.end(),
                             [](const auto& a, const auto& b) {
                                 return a.second > b.second;
                             });
                    
                    // 遍历排序后的边，计数满足约束的边
                    int valid_edge_count = 0;
                    for (const auto& [fdp_idx, next_reward] : all_edges) {
                        // 如果已经找到足够多的满足约束的边，结束遍历
                        if (valid_edge_count >= beam_width_) break;
                        
                        const FDP& next_fdp = network.sorted_fdps[fdp_idx];
                        
                        // 检查约束条件
                        // 1. 累计飞行时间约束
                        auto total_fly_minutes = cycle.fly_minutes + next_fdp.get_flight_hours();
                        if (total_fly_minutes > MAX_FLY) continue;
                        
                        // 2. 计算结束日期
                        time_point end_day_tp;
                        auto duties_after = get_duties_after_fdp(next_fdp);
                        if (!duties_after.empty()) {
                            const GroundDuty* closest_duty = duties_after[0];
                            int calendar_days = calculate_calendar_days(next_fdp.get_end_time(), closest_duty->start_time);
                            if (calendar_days < 2) {
                                auto farthest_duty = duties_after.back();
                                end_day_tp = day_floor(farthest_duty->end_time);
                            } else {
                                end_day_tp = day_floor(next_fdp.get_end_time());
                            }
                        } else {
                            end_day_tp = day_floor(next_fdp.get_end_time());
                        }
                        
                        // 3. 周期跨度约束
                        auto span = std::chrono::duration_cast<std::chrono::days>(
                            end_day_tp - cycle.start_day_tp).count() + 1;
                        if (span > MAX_DAY) continue;
                        
                        // 满足所有约束，创建新的周期
                        CycleInfo next_cycle = cycle;
                        next_cycle.fdps.push_back(fdp_idx);
                        next_cycle.fly_minutes += next_fdp.get_flight_hours();
                        next_cycle.reward += next_reward;
                        next_cycle.end_airport = next_fdp.get_end_airport();
                        next_cycle.end_day_tp = end_day_tp;
                        
                        // 使用预计算的can_reach_sink
                        if (can_reach_sink[next_node_idx]) {
                            next_cycle.can_reach_sink = true;
                        }
                        
                        // 使用优先队列的擂台赛机制
                        // 如果队列未满，直接加入
                        if (next_beam_pq.size() < static_cast<size_t>(beam_width_)) {
                            next_beam_pq.push(std::move(next_cycle));
                        } 
                        // 如果队列已满，但当前候选比队列中最差的好，则替换
                        else if (next_cycle.reward > next_beam_pq.top().reward) {
                            next_beam_pq.pop(); // 移除最差的
                            next_beam_pq.push(std::move(next_cycle)); // 加入新的更好的
                        }
                        // 否则，直接丢弃这个候选
                        
                        // 增加计数
                        valid_edge_count++;
                    }
                    
                    ++it_time;
                }
            }
            
            if (next_beam_pq.empty()) break;
            
            // 将优先队列中的元素转移到beam中
            beam.clear();
            while (!next_beam_pq.empty()) {
                beam.push_back(std::move(const_cast<CycleInfo&>(next_beam_pq.top())));
                next_beam_pq.pop();
            }
        }
        
        // 将该起点的所有候选周期添加到总集合中
        for (auto& cycle : beam) {
            cycles.push_back(std::move(cycle));
        }
    }

    if (cycles.empty()) return {};

    std::vector<CycleInfo> valid_cycles;
    // 检查周期是否从源点出发
    for (const auto& cycle : cycles) {
        bool is_valid = true;
        auto [start_node_idx, a] = network.fdp_to_edge.at(cycle.fdps[0]);
        for (const auto& edge : network.graph[start_node_idx]) {
            if (edge.first == start_node_idx) {
                is_valid = false;
                break;
            }
        }
        auto [b, end_node_idx] = network.fdp_to_edge.at(cycle.fdps.back());
        if (!can_reach_sink[end_node_idx]) {
            is_valid = false;
        }
        if (is_valid) {
            valid_cycles.push_back(cycle);
        }
    }

    // 选择最优的周期
    int best_idx = -1;
    double best_reward = -1;
    for (int i = 0; i < valid_cycles.size(); ++i) {
        if (valid_cycles[i].reward > best_reward) {
            best_reward = valid_cycles[i].reward;
            best_idx = i;
        }
    }
    if (best_idx == -1) return {};
    std::vector<FDP> chosen_cycle;
    for (int fdp_idx : valid_cycles[best_idx].fdps) {
        chosen_cycle.push_back(network.sorted_fdps[fdp_idx]);
    }
    return chosen_cycle;
    
    // 检查周期是否能到达汇点

    // // ====================== 优化后的周期级 DAG 最长路 =====================
    // const double NEG_INF = -1e100;
    // int n_cycles = static_cast<int>(cycles.size());
    // std::vector<double> dist(n_cycles, NEG_INF);
    // std::vector<int>    prev_idx(n_cycles, -1);

    // // 拓扑序：按开始日期排序
    // std::vector<int> order(n_cycles);
    // std::iota(order.begin(), order.end(), 0);
    // std::sort(order.begin(), order.end(), [&](int a, int b){
    //     return cycles[a].start_day_tp < cycles[b].start_day_tp;
    // });

    // // 初始化：所有周期都可作为首周期
    // for (int idx = 0; idx < n_cycles; ++idx) {
    //     dist[idx] = cycles[idx].reward;
    // }

    // // 使用基于机场的索引加速查找（关键优化）
    // std::unordered_map<std::string, std::multimap<time_point, int>> airport_start_map;

    // for (int idx : order) {
    //     if (dist[idx] <= NEG_INF/2) continue;
        
    //     auto& cur_cycle = cycles[idx];
    //     // 关键优化：查找可接续的后续周期
    //     auto it_air = airport_start_map.find(cur_cycle.end_airport);
    //     if (it_air != airport_start_map.end()) {
    //         // 修正时间间隔判断：至少间隔2整天（原代码逻辑）
    //         auto min_start = cur_cycle.end_day_tp + std::chrono::days(3);
    //         auto& start_map = it_air->second;
    //         auto it_low = start_map.lower_bound(min_start);
            
    //         // 遍历所有可能的后继周期
    //         for (auto it = it_low; it != start_map.end(); ++it) {
    //             int next_idx = it->second;
    //             // 候选收益 = 当前收益 + 后继周期收益
    //             double cand = dist[idx] + cycles[next_idx].reward;
                
    //             // 松弛操作
    //             if (cand > dist[next_idx] + 1e-6) {
    //                 dist[next_idx] = cand;
    //                 prev_idx[next_idx] = idx;
    //             }
    //         }
    //     }
        
    //     // 将当前周期加入索引（供后续周期查找）
    //     airport_start_map[cur_cycle.start_airport].insert(
    //         {cur_cycle.start_day_tp, idx});
    // }

    // // 选择能连接到汇点的最佳周期
    // double best_total = NEG_INF;
    // int best_end = -1;
    // for (int i = 0; i < n_cycles; ++i) {
    //     if (!cycles[i].can_reach_sink) continue;
    //     if (dist[i] > best_total) {
    //         best_total = dist[i];
    //         best_end = i;
    //     }
    // }

    // if (best_end == -1 || best_total - crew_dual <= 1e-6) {
    //     return {};
    // }

    // // 回溯
    // std::vector<int> path_indices;
    // for (int idx = best_end; idx != -1; idx = prev_idx[idx]) {
    //     path_indices.push_back(idx);
    // }
    // std::reverse(path_indices.begin(), path_indices.end());

    // std::vector<FDP> chosen_cycle;
    // for (int idx : path_indices) {
    //     for (int fdp_idx : cycles[idx].fdps) {
    //         if (fdp_idx == -1) continue;
    //         chosen_cycle.push_back(network.sorted_fdps[fdp_idx]);
    //     }
    // }
    return chosen_cycle;
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

            // 验证网络
            // FDPNetwork test_network;
            // if (!deserializeFDPNetwork(crew_id, file_path, test_network)) {
            //     std::cerr << "反序列化网络失败: " << crew_id << std::endl;
            //     continue;
            // }
            // if (!compareNetworks(network, test_network)) {
            //     std::cerr << "网络不一致: " << crew_id << std::endl;
            //     continue;
            // }
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

void SubproblemSolver::precomputeAllFDPNetworksParallel(int num_threads) {
    std::cout << "开始多线程预处理所有机组的FDP网络，使用 " << num_threads << " 个线程..." << std::endl;
    
    // 获取所有需要处理的机组ID
    std::vector<std::string> all_crew_ids;
    {
        for (const auto& [crew_id, crew] : data_.get_all_crews()) {
            std::string file_path = getNetworkFilePath(crew_id);
            if (fs::exists(file_path)) {
                continue;
            }
            all_crew_ids.push_back(crew_id);
        }
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
    
    size_t total_count = all_crew_ids.size();
    std::atomic<size_t> processed_count(0);
    std::mutex cout_mutex;
    
    // 创建任务队列
    std::mutex queue_mutex;
    size_t next_index = 0;
    
    // 创建线程池
    std::vector<std::thread> threads;
    
    auto worker_function = [&]() {
        // 为每个线程创建一个独立的SubproblemSolver实例
        SubproblemSolver local_solver(data_, master_, network_directory_, 
                non_base_rejection_prob_, beam_width_);
                
        while (true) {
            // 获取下一个要处理的机组ID
            std::string crew_id;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (next_index >= all_crew_ids.size()) {
                    break;
                }
                crew_id = all_crew_ids[next_index++];
            }

            try {
                // 过滤有效的FDP
                std::vector<FDP> valid_fdps = local_solver.filterValidFDPs(crew_id);
                
                if (!valid_fdps.empty()) {
                    // 构建网络
                    FDPNetwork network = local_solver.buildFDPNetwork(crew_id, valid_fdps);
                    
                    // 保存到文件
                    std::string file_path = getNetworkFilePath(crew_id);
                    std::string temp_file_path = file_path + ".tmp";

                    // 使用RAII确保临时文件被清理
                    struct TempFileGuard {
                        std::string path;
                        ~TempFileGuard() {
                            if (fs::exists(path)) {
                                fs::remove(path);
                            }
                        }
                    } temp_file_guard{temp_file_path};
                    
                    // 尝试最多3次序列化和验证
                    bool success = false;
                    while (!success) {
                        // 序列化到临时文件
                        if (!local_solver.serializeFDPNetwork(crew_id, network, temp_file_path)) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            continue;
                        }
                        
                        FDPNetwork test_network;
                        if (!local_solver.deserializeFDPNetwork(crew_id, temp_file_path, test_network)) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            continue;
                        }
                        
                        if (local_solver.compareNetworks(network, test_network)) {
                            try {
                                if (fs::exists(file_path)) {
                                    fs::remove(file_path);
                                }
                                fs::rename(temp_file_path, file_path);
                                success = true;
                                break;
                            } catch (const std::exception& e) {
                                std::lock_guard<std::mutex> lock(cout_mutex);
                                std::cerr << "重命名文件失败: " << e.what() << std::endl;
                            }
                        }
                    }
                }
                
                // 清理本地缓存
                local_solver.clearCache();
                
                // 清理可能的大对象
                valid_fdps.clear();
                valid_fdps.shrink_to_fit();
                
                // 更新进度
                size_t current = ++processed_count;
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "已处理 " << current << " / " << total_count 
                             << " 个机组的FDP网络 (" 
                             << std::fixed << std::setprecision(1) 
                             << (100.0 * current / total_count) << "%)" << std::endl;
                }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cerr << "处理机组 " << crew_id << " 时发生错误: " << e.what() << std::endl;
                // 确保发生异常时也清理缓存
                local_solver.clearCache();
            }
        }
        
        // 线程结束前确保清理
        local_solver.clearCache();
    };
    
    // 启动工作线程
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_function);
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
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
        fs::create_directories(file_path.parent_path());
        
        // 打开文件
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件进行写入: " << filename << std::endl;
            return false;
        }
        
        // 写入网络基本信息
        size_t n_fdps = network.sorted_fdps.size();
        size_t n_nodes = network.nodes.size();
        file.write(reinterpret_cast<const char*>(&n_fdps), sizeof(n_fdps));
        file.write(reinterpret_cast<const char*>(&n_nodes), sizeof(n_nodes));
        file.write(reinterpret_cast<const char*>(&network.source), sizeof(network.source));
        file.write(reinterpret_cast<const char*>(&network.sink), sizeof(network.sink));
        
        // 写入节点信息
        for (const auto& node : network.nodes) {
            // 写入机场
            size_t airport_len = node.airport.size();
            file.write(reinterpret_cast<const char*>(&airport_len), sizeof(airport_len));
            file.write(node.airport.c_str(), airport_len);
            
            // 写入时间点
            auto duration = node.time.time_since_epoch();
            int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            file.write(reinterpret_cast<const char*>(&microseconds), sizeof(microseconds));
        }
        
        // 写入FDP序列
        for (const auto& fdp : network.sorted_fdps) {
            // 写入任务数量
            size_t task_count = fdp.tasks.size();
            file.write(reinterpret_cast<const char*>(&task_count), sizeof(task_count));
            
            // 写入每个任务
            for (const auto& task : fdp.tasks) {
                // 写入字符串长度和内容
                auto writeString = [&file](const std::string& str) {
                    size_t len = str.size();
                    file.write(reinterpret_cast<const char*>(&len), sizeof(len));
                    file.write(str.c_str(), len);
                };
                
                writeString(task.id);
                writeString(task.task_type);
                writeString(task.start_airport);
                writeString(task.end_airport);
                writeString(task.aircraft_no);
                
                // 直接写入时间点的时间戳（微秒）
                auto writeTimePoint = [&file](const time_point& tp) {
                    auto duration = tp.time_since_epoch();
                    int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
                    file.write(reinterpret_cast<const char*>(&microseconds), sizeof(microseconds));
                };
                
                writeTimePoint(task.start_time);
                writeTimePoint(task.end_time);
                
                // 写入飞行时间（分钟）
                int64_t fly_minutes = task.fly_time.count();
                file.write(reinterpret_cast<const char*>(&fly_minutes), sizeof(fly_minutes));
            }
        }
        
        // 写入图结构
        size_t graph_size = network.graph.size();
        file.write(reinterpret_cast<const char*>(&graph_size), sizeof(graph_size));
        
        for (const auto& adj_list : network.graph) {
            size_t adj_size = adj_list.size();
            file.write(reinterpret_cast<const char*>(&adj_size), sizeof(adj_size));
            
            for (const auto& edge : adj_list) {
                // 写入目标节点索引
                size_t target_node = edge.first;
                file.write(reinterpret_cast<const char*>(&target_node), sizeof(target_node));
                
                // 写入边信息
                int best_fdp_idx = edge.second.best_fdp_idx;
                double reward = edge.second.reward;
                file.write(reinterpret_cast<const char*>(&best_fdp_idx), sizeof(best_fdp_idx));
                file.write(reinterpret_cast<const char*>(&reward), sizeof(reward));
            }
        }
        
        // 写入FDP到边的映射
        size_t fdp_to_edge_size = network.fdp_to_edge.size();
        file.write(reinterpret_cast<const char*>(&fdp_to_edge_size), sizeof(fdp_to_edge_size));
        
        for (const auto& [fdp_idx, edge_pair] : network.fdp_to_edge) {
            // 写入FDP索引
            file.write(reinterpret_cast<const char*>(&fdp_idx), sizeof(fdp_idx));
            
            // 写入边的起点和终点
            file.write(reinterpret_cast<const char*>(&edge_pair.first), sizeof(edge_pair.first));
            file.write(reinterpret_cast<const char*>(&edge_pair.second), sizeof(edge_pair.second));
        }

        // 写入 node_to_fdp_start
        size_t node_to_fdp_start_size = network.node_to_fdp_start.size();
        file.write(reinterpret_cast<const char*>(&node_to_fdp_start_size), sizeof(node_to_fdp_start_size));
        for (const auto& [node, fdp_vec] : network.node_to_fdp_start) {
            // 写入节点
            size_t airport_len = node.airport.size();
            file.write(reinterpret_cast<const char*>(&airport_len), sizeof(airport_len));
            file.write(node.airport.c_str(), airport_len);
            auto duration = node.time.time_since_epoch();
            int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            file.write(reinterpret_cast<const char*>(&microseconds), sizeof(microseconds));
            // 写入vector<int>
            size_t vec_size = fdp_vec.size();
            file.write(reinterpret_cast<const char*>(&vec_size), sizeof(vec_size));
            for (int idx : fdp_vec) {
                file.write(reinterpret_cast<const char*>(&idx), sizeof(idx));
            }
        }
        // 写入 node_to_fdp_end
        size_t node_to_fdp_end_size = network.node_to_fdp_end.size();
        file.write(reinterpret_cast<const char*>(&node_to_fdp_end_size), sizeof(node_to_fdp_end_size));
        for (const auto& [node, fdp_vec] : network.node_to_fdp_end) {
            size_t airport_len = node.airport.size();
            file.write(reinterpret_cast<const char*>(&airport_len), sizeof(airport_len));
            file.write(node.airport.c_str(), airport_len);
            auto duration = node.time.time_since_epoch();
            int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            file.write(reinterpret_cast<const char*>(&microseconds), sizeof(microseconds));
            size_t vec_size = fdp_vec.size();
            file.write(reinterpret_cast<const char*>(&vec_size), sizeof(vec_size));
            for (int idx : fdp_vec) {
                file.write(reinterpret_cast<const char*>(&idx), sizeof(idx));
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
        size_t n_fdps, n_nodes;
        file.read(reinterpret_cast<char*>(&n_fdps), sizeof(n_fdps));
        file.read(reinterpret_cast<char*>(&n_nodes), sizeof(n_nodes));
        file.read(reinterpret_cast<char*>(&network.source), sizeof(network.source));
        file.read(reinterpret_cast<char*>(&network.sink), sizeof(network.sink));
        
        // 读取节点信息
        network.nodes.clear();
        network.nodes.reserve(n_nodes);
        
        for (size_t i = 0; i < n_nodes; ++i) {
            NetworkNode node;
            
            // 读取机场
            size_t airport_len;
            file.read(reinterpret_cast<char*>(&airport_len), sizeof(airport_len));
            node.airport.resize(airport_len);
            file.read(&node.airport[0], airport_len);
            
            // 读取时间点
            int64_t microseconds;
            file.read(reinterpret_cast<char*>(&microseconds), sizeof(microseconds));
            node.time = time_point(std::chrono::microseconds(microseconds));
            
            network.nodes.push_back(node);
        }
        
        // 读取FDP序列
        network.sorted_fdps.clear();
        network.sorted_fdps.reserve(n_fdps);
        
        for (size_t i = 0; i < n_fdps; ++i) {
            FDP fdp;
            
            // 读取任务数量
            size_t task_count;
            file.read(reinterpret_cast<char*>(&task_count), sizeof(task_count));
            
            // 读取每个任务
            for (size_t j = 0; j < task_count; ++j) {
                Task task;
                
                // 读取字符串
                auto readString = [&file]() -> std::string {
                    size_t len;
                    file.read(reinterpret_cast<char*>(&len), sizeof(len));
                    std::string str(len, '\0');
                    file.read(&str[0], len);
                    return str;
                };
                
                task.id = readString();
                task.task_type = readString();
                task.start_airport = readString();
                task.end_airport = readString();
                task.aircraft_no = readString();
                
                // 读取时间点
                auto readTimePoint = [&file]() -> time_point {
                    int64_t microseconds;
                    file.read(reinterpret_cast<char*>(&microseconds), sizeof(microseconds));
                    return time_point(std::chrono::microseconds(microseconds));
                };
                
                task.start_time = readTimePoint();
                task.end_time = readTimePoint();
                
                // 读取飞行时间
                int64_t fly_minutes;
                file.read(reinterpret_cast<char*>(&fly_minutes), sizeof(fly_minutes));
                task.fly_time = std::chrono::minutes(fly_minutes);
                
                fdp.tasks.push_back(task);
            }
            
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
                // 读取目标节点索引
                size_t target_node;
                file.read(reinterpret_cast<char*>(&target_node), sizeof(target_node));
                
                // 读取边信息
                EdgeInfo edge_info;
                file.read(reinterpret_cast<char*>(&edge_info.best_fdp_idx), sizeof(edge_info.best_fdp_idx));
                file.read(reinterpret_cast<char*>(&edge_info.reward), sizeof(edge_info.reward));
                
                network.graph[i].push_back({target_node, edge_info});
            }
        }
        
        // 读取FDP到边的映射
        network.fdp_to_edge.clear();
        size_t fdp_to_edge_size;
        file.read(reinterpret_cast<char*>(&fdp_to_edge_size), sizeof(fdp_to_edge_size));
        
        for (size_t i = 0; i < fdp_to_edge_size; ++i) {
            // 读取FDP索引
            int fdp_idx;
            file.read(reinterpret_cast<char*>(&fdp_idx), sizeof(fdp_idx));
            
            // 读取边的起点和终点
            size_t from_node, to_node;
            file.read(reinterpret_cast<char*>(&from_node), sizeof(from_node));
            file.read(reinterpret_cast<char*>(&to_node), sizeof(to_node));
            
            // 存储FDP到边的映射
            network.fdp_to_edge[fdp_idx] = {from_node, to_node};
        }
        
        // 初始化奖励值
        network.rewards.resize(n_fdps, 0.0);

        // 读取 node_to_fdp_start
        network.node_to_fdp_start.clear();
        size_t node_to_fdp_start_size;
        file.read(reinterpret_cast<char*>(&node_to_fdp_start_size), sizeof(node_to_fdp_start_size));
        for (size_t i = 0; i < node_to_fdp_start_size; ++i) {
            NetworkNode node;
            size_t airport_len;
            file.read(reinterpret_cast<char*>(&airport_len), sizeof(airport_len));
            node.airport.resize(airport_len);
            file.read(&node.airport[0], airport_len);
            int64_t microseconds;
            file.read(reinterpret_cast<char*>(&microseconds), sizeof(microseconds));
            node.time = time_point(std::chrono::microseconds(microseconds));
            size_t vec_size;
            file.read(reinterpret_cast<char*>(&vec_size), sizeof(vec_size));
            std::vector<int> fdp_vec(vec_size);
            for (size_t j = 0; j < vec_size; ++j) {
                file.read(reinterpret_cast<char*>(&fdp_vec[j]), sizeof(fdp_vec[j]));
            }
            network.node_to_fdp_start[node] = fdp_vec;
        }
        // 读取 node_to_fdp_end
        network.node_to_fdp_end.clear();
        size_t node_to_fdp_end_size;
        file.read(reinterpret_cast<char*>(&node_to_fdp_end_size), sizeof(node_to_fdp_end_size));
        for (size_t i = 0; i < node_to_fdp_end_size; ++i) {
            NetworkNode node;
            size_t airport_len;
            file.read(reinterpret_cast<char*>(&airport_len), sizeof(airport_len));
            node.airport.resize(airport_len);
            file.read(&node.airport[0], airport_len);
            int64_t microseconds;
            file.read(reinterpret_cast<char*>(&microseconds), sizeof(microseconds));
            node.time = time_point(std::chrono::microseconds(microseconds));
            size_t vec_size;
            file.read(reinterpret_cast<char*>(&vec_size), sizeof(vec_size));
            std::vector<int> fdp_vec(vec_size);
            for (size_t j = 0; j < vec_size; ++j) {
                file.read(reinterpret_cast<char*>(&fdp_vec[j]), sizeof(fdp_vec[j]));
            }
            network.node_to_fdp_end[node] = fdp_vec;
        }
        
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
        // std::cout << "  - Task ID mismatch: " << t1.id << " vs " << t2.id << std::endl;
        return false;
    }
    if (t1.task_type != t2.task_type) {
        // std::cout << "  - Task type mismatch: " << t1.task_type << " vs " << t2.task_type << std::endl;
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
        // std::cout << "  - Task end_time mismatch" << std::endl;
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
        // std::cout << " - FDP tasks size mismatch: " << f1.tasks.size() << " vs " << f2.tasks.size() << std::endl;
        return false;
    }
    for (size_t i = 0; i < f1.tasks.size(); ++i) {
        if (!compareTasks(f1.tasks[i], f2.tasks[i])) {
            // std::cout << " - Difference in Task at index " << i << std::endl;
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

    // 比较节点
    if (n1.nodes.size() != n2.nodes.size()) {
        std::cout << "Network nodes size mismatch: " << n1.nodes.size() << " vs " << n2.nodes.size() << std::endl;
        return false; // Fatal difference
    }

    for (size_t i = 0; i < n1.nodes.size(); ++i) {
        const auto& node1 = n1.nodes[i];
        const auto& node2 = n2.nodes[i];
        
        if (node1.airport != node2.airport) {
            std::cout << "Node airport mismatch at index " << i << ": " << node1.airport << " vs " << node2.airport << std::endl;
            is_identical = false;
        }
        
        if (node1.time != node2.time) {
            std::cout << "Node time mismatch at index " << i << std::endl;
            is_identical = false;
        }
    }

    if (n1.graph.size() != n2.graph.size()) {
        std::cout << "Network graph size mismatch: " << n1.graph.size() << " vs " << n2.graph.size() << std::endl;
        return false; // Fatal difference
    }
    
    // 比较图结构
    for (size_t i = 0; i < n1.graph.size(); ++i) {
        if (n1.graph[i].size() != n2.graph[i].size()) {
            std::cout << "Graph adjacency list size mismatch for node " << i << ": " 
                     << n1.graph[i].size() << " vs " << n2.graph[i].size() << std::endl;
            is_identical = false;
            continue;
        }
        
        // 创建两个映射，用于比较边
        std::unordered_map<size_t, const EdgeInfo*> edges_map1, edges_map2;
        
        for (const auto& edge : n1.graph[i]) {
            edges_map1[edge.first] = &edge.second;
        }
        
        for (const auto& edge : n2.graph[i]) {
            edges_map2[edge.first] = &edge.second;
        }
        
        // 比较映射
        if (edges_map1.size() != edges_map2.size()) {
            std::cout << "Edge map size mismatch for node " << i << std::endl;
            is_identical = false;
            continue;
        }
        
        for (const auto& [target, info_ptr1] : edges_map1) {
            auto it = edges_map2.find(target);
            if (it == edges_map2.end()) {
                std::cout << "Edge to node " << target << " missing in second network for node " << i << std::endl;
                is_identical = false;
                continue;
            }
            
            const EdgeInfo* info_ptr2 = it->second;
            
            if (info_ptr1->best_fdp_idx != info_ptr2->best_fdp_idx) {
                std::cout << "Edge best_fdp_idx mismatch for edge " << i << "->" << target << ": " 
                         << info_ptr1->best_fdp_idx << " vs " << info_ptr2->best_fdp_idx << std::endl;
                is_identical = false;
            }
            
            if (std::abs(info_ptr1->reward - info_ptr2->reward) > 1e-6) {
                std::cout << "Edge reward mismatch for edge " << i << "->" << target << ": " 
                         << info_ptr1->reward << " vs " << info_ptr2->reward << std::endl;
                is_identical = false;
            }
        }
    }
    
    // 比较FDP到边的映射
    if (n1.fdp_to_edge.size() != n2.fdp_to_edge.size()) {
        std::cout << "FDP to edge map size mismatch: " << n1.fdp_to_edge.size() << " vs " << n2.fdp_to_edge.size() << std::endl;
        is_identical = false;
    } else {
        for (const auto& [fdp_idx, pair1] : n1.fdp_to_edge) {
            auto it2 = n2.fdp_to_edge.find(fdp_idx);
            if (it2 == n2.fdp_to_edge.end()) {
                std::cout << "FDP index " << fdp_idx << " missing in second network's map" << std::endl;
                is_identical = false;
                continue;
            }
            const auto& pair2 = it2->second;
            if (pair1 != pair2) {
                std::cout << "Edge pair mismatch for FDP " << fdp_idx << ": (" << pair1.first << "," << pair1.second
                          << ") vs (" << pair2.first << "," << pair2.second << ")" << std::endl;
                is_identical = false;
            }
        }
    }

    // 比较 node_to_fdp_start
    if (n1.node_to_fdp_start.size() != n2.node_to_fdp_start.size()) return false;
    for (const auto& [node, v1] : n1.node_to_fdp_start) {
        auto it = n2.node_to_fdp_start.find(node);
        if (it == n2.node_to_fdp_start.end() || v1 != it->second) return false;
    }
    // 比较 node_to_fdp_end
    if (n1.node_to_fdp_end.size() != n2.node_to_fdp_end.size()) return false;
    for (const auto& [node, v1] : n1.node_to_fdp_end) {
        auto it = n2.node_to_fdp_end.find(node);
        if (it == n2.node_to_fdp_end.end() || v1 != it->second) return false;
    }

    return is_identical;
}


bool SubproblemSolver::testSerialization(const std::string& crew_id, FDPNetwork& original_network) {
    std::cout << "\n======================================================\n";
    std::cout << "开始对机组 " << crew_id << " 进行网络序列化测试" << std::endl;
    std::cout << "======================================================\n";

    // 打印原始网络的基本信息
    std::cout << "原始网络信息：" << std::endl;
    std::cout << " - FDP数量: " << original_network.sorted_fdps.size() << std::endl;
    std::cout << " - 源点索引: " << original_network.source << std::endl;
    std::cout << " - 汇点索引: " << original_network.sink << std::endl;
    std::cout << " - 源点连接数: " << original_network.graph[original_network.source].size() << std::endl;
    
    // 统计连接到汇点的节点数
    // size_t sink_connections = 0;
    // for (size_t i = 0; i < original_network.sorted_fdps.size(); ++i) {
    //     auto& adj = original_network.graph[i];
    //     if (std::find(adj.begin(), adj.end(), std::make_pair(original_network.sink, EdgeInfo())) != adj.end()) {
    //         sink_connections++;
    //     }
    // }
    // std::cout << " - 连接到汇点的节点数: " << sink_connections << std::endl;

    // 3. 将网络序列化到临时文件
    std::string temp_filename = "temp_network_test_" + crew_id + ".fdp";
    std::cout << "序列化网络到临时文件: " << temp_filename << "..." << std::endl;
    if (!serializeFDPNetwork(crew_id, original_network, temp_filename)) {
        std::cerr << "序列化网络失败。测试中止。" << std::endl;
        return false;
    }
    std::cout << "序列化成功。" << std::endl;

    // 4. 从文件反序列化网络
    FDPNetwork deserialized_network;
    std::cout << "从文件反序列化网络..." << std::endl;
    if (!deserializeFDPNetwork(crew_id, temp_filename, deserialized_network)) {
        std::cerr << "反序列化网络失败。测试中止。" << std::endl;
        fs::remove(temp_filename);
        return false;
    }
    std::cout << "反序列化成功。" << std::endl;
    
    // 打印反序列化网络的基本信息
    std::cout << "反序列化网络信息：" << std::endl;
    std::cout << " - FDP数量: " << deserialized_network.sorted_fdps.size() << std::endl;
    std::cout << " - 源点索引: " << deserialized_network.source << std::endl;
    std::cout << " - 汇点索引: " << deserialized_network.sink << std::endl;
    std::cout << " - 源点连接数: " << deserialized_network.graph[deserialized_network.source].size() << std::endl;
    
    // // 统计连接到汇点的节点数
    // sink_connections = 0;
    // for (size_t i = 0; i < deserialized_network.sorted_fdps.size(); ++i) {
    //     auto& adj = deserialized_network.graph[i];
    //     if (std::find(adj.begin(), adj.end(), deserialized_network.sink) != adj.end()) {
    //         sink_connections++;
    //     }
    // }
    // std::cout << " - 连接到汇点的节点数: " << sink_connections << std::endl;

    // 5. 比较两个网络
    std::cout << "\n--- 比较原始网络和反序列化网络 ---\n" << std::endl;
    bool are_identical = compareNetworks(original_network, deserialized_network);

    // 6. 清理临时文件
    fs::remove(temp_filename);

    std::cout << "\n--- 测试结果 ---\n";
    if (are_identical) {
        std::cout << "成功：原始网络和反序列化网络完全一致。" << std::endl;
        return true;
    } else {
        std::cout << "失败：在原始网络和反序列化网络之间发现差异。" << std::endl;
        return false;
    }
}

