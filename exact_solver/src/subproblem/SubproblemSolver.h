#pragma once

#include "../data_model/SchedulingData.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <functional>
#include <thread>

// 前向声明
class MasterProblem;

// 节点类型定义 - 表示(机场,时间点)组合
struct NetworkNode {
    std::string airport;
    time_point time;
    
    // 用于比较和哈希
    bool operator==(const NetworkNode& other) const {
        return airport == other.airport && time == other.time;
    }
};

// 为NetworkNode定义哈希函数
struct NetworkNodeHash {
    std::size_t operator()(const NetworkNode& node) const {
        return std::hash<std::string>{}(node.airport) ^ 
               std::hash<std::size_t>{}(std::chrono::system_clock::to_time_t(node.time));
    }
};

// 边信息 - 存储连接两个节点的最优FDP
struct EdgeInfo {
    int best_fdp_idx = -1;  // 最优FDP在sorted_fdps中的索引
    double reward = 0.0;    // 最优FDP的奖励值
    bool operator==(const EdgeInfo& other) const {
        return best_fdp_idx == other.best_fdp_idx && std::abs(reward - other.reward) < 1e-8;
    }
};

// FDP网络缓存结构
class FDPNetwork {
public:
    FDPNetwork() = default;
    std::vector<FDP> sorted_fdps;           // 所有可用的FDP列表
    std::vector<double> rewards;            // 每个FDP的奖励值
    
    // 节点集合
    std::vector<NetworkNode> nodes;
    
    // 邻接表表示的有向图 - 从节点索引到(目标节点索引,边信息)的映射
    std::vector<std::vector<std::pair<size_t, EdgeInfo>>> graph;
    
    // 源点和汇点索引
    size_t source;
    size_t sink;
    
    // 从FDP索引到对应边的映射(用于快速更新)
    std::unordered_map<int, std::pair<size_t, size_t>> fdp_to_edge;

    // 从节点索引到FDP索引的映射
    std::unordered_map<NetworkNode, std::vector<int>, NetworkNodeHash> node_to_fdp_start;
    std::unordered_map<NetworkNode, std::vector<int>, NetworkNodeHash> node_to_fdp_end;
};

class SubproblemSolver {
public:
    // 构造函数，接收数据和主问题引用
    SubproblemSolver(const SchedulingData& data, const MasterProblem& master, std::string fdp_path, 
                    double non_base_rejection_prob = 0.0, int beam_width = 20);
    
    // 更新对偶值
    void updateDuals();
    
    // 清除所有缓存数据
    void clearCache();
    
    // 只清除与特定机组相关的缓存
    void clearCrewSpecificCache();

    // 为特定机长求解子问题
    bool solveForCrew(const std::string& crew_id);
    bool solveForCrewWithDuals(const std::string& crew_id, 
                               const std::unordered_map<std::string, double>& flight_duals);
    
    // 获取最优FDP
    const FDP& getBestFDP() const;
    
    // 获取最优解的检验数
    double getReducedCost() const;
    
    // 预处理所有机组的FDP网络并保存
    void precomputeAllFDPNetworks();

    void precomputeAllFDPNetworksParallel(int num_threads = std::thread::hardware_concurrency());
    
    // 测试FDP网络的序列化与反序列化
    bool testSerialization(const std::string& crew_id, FDPNetwork& original_network);
    
private:

    // 使用std::chrono定义时间点类型
    using time_point = std::chrono::system_clock::time_point;
    

    // 筛选机长可执行的合法FDP
    std::vector<FDP> filterValidFDPs(const std::string& crew_id);
    
    // 计算FDP的奖励值
    double calculateFDPReward(const FDP& fdp, const std::unordered_map<std::string, double>& flight_duals) const;
    
    // 构建并求解最长路问题
    std::vector<FDP> solveLongestPath(const std::string& crew_id, 
                                     const std::vector<FDP>& valid_fdps,
                                     double crew_dual);
    
    // 辅助函数：按起始机场对FDP进行分组
    std::unordered_map<std::string, std::vector<std::pair<size_t, const FDP*>>>
    groupFDPsByStartAirport(const std::vector<FDP>& fdps) const;
    
    // 构建FDP网络
    FDPNetwork buildFDPNetwork(const std::string& crew_id, const std::vector<FDP>& valid_fdps);
    
    // 更新网络奖励值
    void updateNetworkRewards(FDPNetwork& network);
    
    // 求解网络最长路
    std::vector<FDP> solveLongestPathWithNetwork(const FDPNetwork& network, double crew_dual, std::string crew_id);
    
    // 获取FDP包含的航班ID（使用缓存）
    std::unordered_set<std::string> getCachedFlightIds(const FDP& fdp) const;
    
    // 文件操作：获取网络文件路径
    std::string getNetworkFilePath(const std::string& crew_id) const;
    
    // 序列化FDP网络到文件
    bool serializeFDPNetwork(const std::string& crew_id, const FDPNetwork& network, 
                              const std::string& filename) const;
    
    // 从文件反序列化FDP网络
    bool deserializeFDPNetwork(const std::string& crew_id, const std::string& filename, 
                                FDPNetwork& network);
    
    // 序列化单个FDP
    std::string serializeFDP(const FDP& fdp) const;
    
    // 反序列化单个FDP
    FDP deserializeFDP(const std::string& str) const;
    
    // 比较两个Task是否相同（用于测试）
    bool compareTasks(const Task& t1, const Task& t2) const;
    
    // 比较两个FDP是否相同（用于测试）
    bool compareFDPs(const FDP& f1, const FDP& f2) const;
    
    // 比较两个FDP网络是否相同（用于测试）
    bool compareNetworks(const FDPNetwork& n1, const FDPNetwork& n2) const;
    
private:
    // 引用项目数据
    const SchedulingData& data_;
    
    // 引用主问题
    const MasterProblem& master_;
    
    // 缓存各航班的对偶价值
    std::unordered_map<std::string, double> flight_duals_cache_;
    
    // 缓存各机长可执行的合法FDP
    std::unordered_map<std::string, std::vector<FDP>> crew_valid_fdps_cache_;
    
    // 缓存FDP包含的航班ID
    mutable std::unordered_map<std::string, std::unordered_set<std::string>> fdp_flight_ids_cache_;
    
    // 哈希函数用于FDP指针对
    struct PairFDPPtrHash {
        std::size_t operator()(const std::pair<const FDP*, const FDP*>& p) const {
            return std::hash<const FDP*>{}(p.first) ^ std::hash<const FDP*>{}(p.second);
        }
    };
    
    // 缓存FDP的连接性检查结果
    mutable std::unordered_map<std::pair<const FDP*, const FDP*>, bool, PairFDPPtrHash> connectivity_cache_;
    
    // 最优的FDP
    FDP best_fdp_;
    
    // 最优解的检验数
    double reduced_cost_;
    
    // FDP网络文件存储目录
    std::string network_directory_;
    
    // 非基地机场结束的FDP被拒绝的概率
    double non_base_rejection_prob_;
    
    // Beam搜索的宽度
    int beam_width_;
};
