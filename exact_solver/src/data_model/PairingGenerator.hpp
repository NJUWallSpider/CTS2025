// PairingGenerator.hpp
#pragma once

#include "SchedulingData.hpp"
#include <future>

class PairingGenerator {
public:
    // 接收 SchedulingData 对象的引用，避免拷贝
    explicit PairingGenerator(SchedulingData& data, Date start_date, Date end_date);

    // 主函数，构建所有合法的 FDP
    void build_all_valid_fdps();
    
    // 单线程版本的构建函数，用于调试
    void build_all_valid_fdps_single_thread();

private:
    // --- 内部方法 ---
    void _prepare_tasks();

    // DFS核心函数，用于为单个(机场, 日期)组合生成FDPs
    // 这个函数将由多线程并发调用
    std::vector<FDP> _process_airport_date(const std::string& airport, const Date& date);

    // 递归的DFS构建器
    void _dfs_fdp_builder(
        std::vector<Task>& current_path,
        std::unordered_set<std::string>& used_task_ids,
        std::vector<FDP>& found_fdps
    );

    // --- 成员变量 ---
    SchedulingData& data_; // 存储对原始数据的引用
    std::unordered_map<std::string, std::vector<Task>> tasks_by_airport_;

    // --- 算法常量 ---
    const std::chrono::hours MIN_CONNECTION_TIME_BUS; 
    const std::chrono::hours MIN_CONNECTION_TIME_FLIGHT;
    const int MAX_FLIGHT_TASKS_PER_FDP;
    const int MAX_TOTAL_TASKS_PER_FDP;
    const std::chrono::hours MAX_FLY_TIME_PER_FDP;
    const std::chrono::hours MAX_DUTY_TIME_PER_FDP;

    Date start_date_;
    Date end_date_;
};