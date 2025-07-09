#pragma once

#include "gurobi_c++.h"
#include "../data_model/SchedulingData.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <deque>

// 列的状态信息
struct ColumnInfo {
    int index;                          // 在pairing_vars_中的索引
    std::string crew_id;                // 机长ID
    FDP fdp;                           // 飞行值勤期
    int zero_value_count;              // 连续解值为0的次数
    int age;                           // 年龄（迭代次数）
    double last_reduced_cost;          // 最近一次的检验数
    bool is_active;                    // 是否在活跃集中

    ColumnInfo(int idx, const std::string& cid, const FDP& f) 
        : index(idx), crew_id(cid), fdp(f), zero_value_count(0), age(0), 
          last_reduced_cost(0.0), is_active(true) {}
};

class MasterProblem {
public:
    MasterProblem(const SchedulingData& data, std::string data_version);
    ~MasterProblem();

    // 初始化主问题模型
    void initialize();
    
    // 求解当前主问题
    void solve();
    
    // 求解整数规划
    void solveIntegerProgram();
    
    // 获取航班的对偶价值
    double getFlightDual(const std::string& flight_id) const;
    
    // 获取机长的对偶价值
    double getCrewDual(const std::string& crew_id) const;
    
    // 添加新的列(飞行周期)到主问题
    int addColumn(const std::string& crew_id, const FDP& fdp);
    
    // 检查列是否已存在
    bool columnExists(const std::string& crew_id, const FDP& fdp) const;
    
    // 获取当前解的目标值(覆盖的航班数)
    double getObjectiveValue() const;
    
    // 检查算法是否收敛
    bool isConverged() const;
    
    // 输出最终解决方案
    void printSolution() const;

private:
    std::string data_version_;
    // 引用外部数据
    const SchedulingData& data_;
    
    // Gurobi环境和模型
    GRBEnv env_;
    std::unique_ptr<GRBModel> model_;
    
    // 航班覆盖变量 y_i
    std::unordered_map<std::string, GRBVar> flight_vars_;
    
    // 飞行周期分配变量 x_jk
    std::vector<GRBVar> pairing_vars_;
    
    // 记录每个变量对应的机长和FDP
    std::vector<std::pair<std::string, FDP>> pairing_info_;
    
    // 记录每个航班被哪些变量覆盖
    std::unordered_map<std::string, std::vector<int>> flight_coverage_;
    
    // 记录每个机长被哪些变量使用
    std::unordered_map<std::string, std::vector<int>> crew_usage_;
    
    // 记录对偶值
    std::unordered_map<std::string, double> flight_duals_;
    std::unordered_map<std::string, double> crew_duals_;
    
    // 存储约束对象，用于直接访问而非通过名称查找
    std::unordered_map<std::string, GRBConstr> flight_constrs_;
    std::unordered_map<std::string, GRBConstr> crew_constrs_;
    
    // 已添加列的哈希集合，用于检查重复
    std::unordered_set<std::string> added_columns_;
    
    // 辅助函数，用于从FDP中提取覆盖的航班
    std::vector<std::string> getFlightsFromFDP(const FDP& fdp) const;
    
    // 辅助函数，为FDP生成唯一标识符
    std::string generateColumnHash(const std::string& crew_id, const FDP& fdp) const;
    
    // 迭代计数和收敛标志
    int iteration_count_;
    bool converged_;

    // 列管理相关的参数
    static constexpr int MAX_ZERO_VALUE_COUNT = 10;   // 连续解值为0的最大次数
    static constexpr int MAX_AGE = 50;               // 最大年龄
    static constexpr double REDUCED_COST_THRESHOLD = -10.0; // 检验数阈值
    static constexpr int REACTIVATION_INTERVAL = 5;   // 重激活检查间隔
    static constexpr int MAX_ACTIVE_COLUMNS = 2500;   // 活跃列的最大数量

    // 列管理相关的数据结构
    std::vector<ColumnInfo> columns_;                // 所有列的信息
    std::deque<int> column_pool_;                   // 列池（存储非活跃列的索引）

    // 列管理相关的方法
    void manageColumns();                           // 列管理的主要逻辑
    void updateColumnStatus();                      // 更新列的状态信息
    void deactivateColumns();                       // 将不活跃的列移入列池
    void reactivateColumns();                       // 从列池中重激活有潜力的列
    double calculateReducedCost(const ColumnInfo& col) const;  // 计算列的检验数

    // 新增：将变量转换为整数变量
    void convertToIntegerProgram();

    // 新增：导出MPS文件
    void exportMPSFile() const;
    // 新增：尝试从MPS文件读取模型
    bool tryLoadFromMPSFile();
    // 新增：获取MPS文件路径
    std::string getMPSFilePath() const;
};