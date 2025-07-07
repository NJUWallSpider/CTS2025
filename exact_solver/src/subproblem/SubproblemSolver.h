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

// FDP网络缓存结构
struct FDPNetwork {
    std::vector<FDP> sorted_fdps;           // 按开始时间排序的FDP列表
    std::vector<double> rewards;            // 每个FDP的奖励值
    std::vector<std::vector<int>> graph;    // 邻接表表示的有向无环图
    size_t source;                          // 源点索引
    size_t sink;                            // 汇点索引
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
    
    // 检查两个FDP是否可以连接
    bool canConnect(std::string crew_id, const FDP& fdp1, const FDP& fdp2) const;
    
    // 计算两个FDP之间的休息成本
    double calculateRestCost(const FDP& fdp1, const FDP& fdp2) const;
    
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
