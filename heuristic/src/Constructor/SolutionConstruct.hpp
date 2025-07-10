#pragma once

#include "../Loader/LoadData.hpp"
#include "./SolutionState.hpp"
#include <vector>
#include <map>
#include <string>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>

/**
 * @brief Internal helper struct for tracking detailed dynamic state of crew during evaluation
 * 
 * This struct maintains runtime state information for each crew member during schedule evaluation.
 * It tracks both duty-level metrics (flight tasks, total tasks, duty time) and cycle-level metrics
 * (total duties, total hours) to ensure all scheduling constraints are met.
 * 
 * The state is updated as tasks are assigned/removed and helps validate scheduling rules like:
 * - Maximum flight tasks per duty
 * - Maximum total tasks per duty  
 * - Maximum duty time limits
 * - Required rest periods between duties
 * - Maximum cycle duration and total cycle hours
 */


class SolutionConstructor {
public:
    /**
     * @brief Constructor
     * @param data All of the data loaded
     */
    explicit SolutionConstructor(const DataLoader& data);

    // Generates a new, complete schedule.
    SolutionState generate_schedule();
    
    // 破坏与重建优化
    SolutionState ruin_and_recreate(SolutionState& initial_solution, double ruin_percentage = 0.1);
    
    // 使用模拟退火的多路径破坏与重建优化
    SolutionState simulated_annealing_ruin_recreate(
        SolutionState& initial_solution, 
        int num_paths = 5,
        int max_iterations = 500,
        double initial_temperature = 100.0,
        double cooling_rate = 0.98,
        double min_temperature = 0.01,
        double initial_ruin_percentage = 0.05
    );
    
    // 多线程版本的模拟退火多路径破坏与重建优化
    SolutionState parallel_simulated_annealing_ruin_recreate(
        SolutionState& initial_solution, 
        int num_paths = 5,
        int num_threads = 0,  // 0表示使用系统可用的线程数
        int max_iterations = 500,
        double initial_temperature = 100.0,
        double cooling_rate = 0.98,
        double min_temperature = 0.01,
        double initial_ruin_percentage = 0.05
    );

private:
    const DataLoader& data_; // Holds a const reference to the original data
    std::string data_version_;
    
    
    // 破坏阶段：移除部分机长的排班
    void ruin_solution(SolutionState& solution, const std::vector<std::string>& selected_crews);
    
    // 重建阶段：为被选中的机长重新安排航班
    void recreate_solution(SolutionState& solution, const std::vector<std::string>& selected_crews);
    
    // 选择要破坏的机长
    std::vector<std::string> select_crews_to_ruin(const SolutionState& solution, double percentage);
    
    // 计算接受新解的概率（模拟退火）
    double calculate_acceptance_probability(double delta_energy, double temperature);
    
    // 多线程工作函数：处理一组路径
    void thread_worker(
        std::vector<SolutionState>& paths,
        std::vector<double>& temperatures,
        std::vector<double>& ruin_percentages,
        int start_idx,
        int end_idx,
        int max_iterations,
        double cooling_rate,
        double min_temperature,
        double initial_temperature,
        std::atomic<bool>& should_terminate,
        std::mutex& global_best_mutex,
        SolutionState& global_best_solution,
        std::atomic<int>& global_iteration_counter
    );
    
    // 随机数生成器
    std::mt19937 rng_{std::random_device{}()};
};