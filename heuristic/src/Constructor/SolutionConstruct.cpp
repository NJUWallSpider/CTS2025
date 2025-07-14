#include "SolutionConstruct.hpp"
#include "Scheduler/CrewSchedule.hpp"
#include "../Solver/ReportGenerator.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <map>
#include <limits>
#include <algorithm>
#include <numeric>
#include <random>
#include <cmath>
#include <thread>
#include <mutex>
#include <atomic>
#include <future>
#include <functional>
#include <set>

SolutionConstructor::SolutionConstructor(const DataLoader& data) : data_(data) {
    data_version_ = data.getDataVersion();
}

SolutionState SolutionConstructor::generate_schedule() {
    SolutionState solution;
    const auto& crews = data_.getCrews();
    
    // Create a vector of crews with priority scores
    std::vector<std::pair<Crew, double>> crew_scores;
    
    // Calculate maximum values for normalization
    double max_ground_duties = 0;
    double max_quals = 0;
    std::map<std::string, int> base_flight_counts;
    
    // First pass: gather statistics 6-3-2
    for(const auto& [_, crew] : crews) {
        max_ground_duties = std::max(max_ground_duties, static_cast<double>(crew.groundDuties.size()));
        max_quals = std::max(max_quals, static_cast<double>(crew.qualifications.size()));
    }
    
    for(const auto& flight : data_.getFlights()) {
        base_flight_counts[flight.depaAirport]++;
    }
    
    double max_base_flights = 0;
    for(const auto& [_, count] : base_flight_counts) {
        max_base_flights = std::max(max_base_flights, static_cast<double>(count));
    }
    
    // Calculate priority scores for each crew 6-3-2
    for(const auto& [_, crew] : crews) {
        double score = 0.0;
        
        // // Factor 1: Ground duties (30% weight)
        // if(max_ground_duties > 0) {
        //     score += (crew.groundDuties.size() / max_ground_duties) * 0.6;
        // }
        
        // Factor 2: Qualification flexibility (40% weight)
        if(max_quals > 0) {
            score -= (crew.qualifications.size() / max_quals) * 0.3;
        }
        
        // Factor 3: Base station strategic value (30% weight)
        // if(max_base_flights > 0 && base_flight_counts.count(crew.base)) {
        //     score += (base_flight_counts[crew.base] / max_base_flights) * 0.3;
        // }

        // Factor 4: Initial airport (20% weight)
        // if(max_base_flights > 0 && base_flight_counts.count(crew.initialStayStation)) {
        //     score -= (base_flight_counts[crew.initialStayStation] / max_base_flights) * 0.7;
        // }
        
        crew_scores.emplace_back(crew, score);
    }
    
    // Sort crews by their composite scores in descending order
    std::sort(crew_scores.begin(), crew_scores.end(), 
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

    // Extract crews
    std::vector<Crew> weight_quali_base_sorted_crews;
    for(const auto& [crew, _] : crew_scores) {
        weight_quali_base_sorted_crews.push_back(crew);
    }

    /////////////////////

    // // shuffle option
    // std::random_device rd;
    // std::mt19937 g(rd());
    // std::shuffle(sorted_crews.begin(), sorted_crews.end(), g); 

    /////////////////////

    // for the second phase:
    std::vector<Crew> crews_with_no_ground_duties;
    for(const auto& crew : weight_quali_base_sorted_crews){
        // get the crews that has no ground duties
        if(crew.groundDuties.size() < 1){
            crews_with_no_ground_duties.emplace_back(crew);            
        }
    }

    //////////////////////

    std::vector<Crew> crews_possess_qualifications;
    for(const auto& crew : crews_with_no_ground_duties){
        if(!crew.qualifications.empty()){
            crews_possess_qualifications.emplace_back(crew);
        }
    }
    //////////////////////




    for(const auto& crew : crews_possess_qualifications){
        solution.crew_assignment_order.push_back(crew.id);
        CrewSchedule crew_schedule_builder(data_, crew, 
            solution.crew_dutyperiods[crew.id], 
            solution.flight_assignments,
            solution.crew_cycles[crew.id]);

        crew_schedule_builder.construct_schedule_DFS();

    }

    // std::vector<Crew> unassigned_crews;
    // // 收集所有未分配任务的机长ID
    // for (const auto& [crew_id, duty_periods] : solution.crew_dutyperiods) {
    //     // 只选择有任务的机长
    //     if (duty_periods.back().tasks.empty()) {
    //         unassigned_crews.push_back(data_.getCrews().at(crew_id));
    //     }
    // }

    // for(const auto& crew : unassigned_crews){
    //     solution.crew_assignment_order.push_back(crew.id);
    //     CrewSchedule crew_schedule_builder(data_, crew, 
    //         solution.crew_dutyperiods[crew.id], 
    //         solution.flight_assignments,
    //         solution.crew_cycles[crew.id]);
    //     crew_schedule_builder.construct_schedule_DFS();

    // }

    // the score of the solution is the sum of the flights that is piloted
    solution.score = 0.0;
    for(const auto& flight : solution.flight_assignments){
        // get the flight that has one "true" in the vector
        for(const auto& task : flight.second){
            if(task.second){
                solution.score += 1;
                break;
            }
        }
    }
    solution.avg_flight_hours = calculate_avg_flight_hours(solution);
    solution.NFDP_count = calculate_NFDP_count(solution);

    return solution;
}

// 破坏与重建优化
SolutionState SolutionConstructor::ruin_and_recreate(SolutionState& initial_solution, double ruin_percentage) {
    // 创建一个解决方案的副本
    SolutionState current_solution = initial_solution;
    
    // 选择要破坏的机长
    std::vector<std::string> crews_to_ruin = select_crews_to_ruin(current_solution, ruin_percentage);
    
    // 如果没有选择到机长，直接返回原始解决方案
    if (crews_to_ruin.empty()) {
        return current_solution;
    }
    
   // std::cout << "选择了 " << crews_to_ruin.size() << " 名机长进行破坏与重建优化" << std::endl;
    
    // 执行破坏操作
    ruin_solution(current_solution, crews_to_ruin);
    
    // 执行重建操作
    recreate_solution(current_solution, crews_to_ruin);
    
    // 重新计算解决方案的分数
    current_solution.score = 0.0;
    for(const auto& flight : current_solution.flight_assignments){
        for(const auto& task : flight.second){
            if(task.second){
                current_solution.score += 1;
                break;
            }
        }
    }
    
    return current_solution;
}

// 选择要破坏的机长
std::vector<std::string> SolutionConstructor::select_crews_to_ruin(const SolutionState& solution, double percentage) {
    std::vector<std::string> all_crew_ids;
    std::vector<std::string> selected_crews;
    
    // // 收集所有已分配任务的机长ID
    // for (const auto& [crew_id, duty_periods] : solution.crew_dutyperiods) {
    //     // 只选择有任务的机长
    //     if (!duty_periods.empty()) {
    //         all_crew_ids.push_back(crew_id);
    //     }
    // }
    
    // // 如果没有机长有任务，返回空列表
    // if (all_crew_ids.empty()) {
    //     return selected_crews;
    // }

    for(const auto& [crew_id, crew] : data_.getCrews()){
        if(crew.qualifications.size() > 0 && crew.groundDuties.size() < 1){
            all_crew_ids.push_back(crew_id);
        }
    }
    
    // 计算要选择的机长数量
    int num_crews_to_select = std::max(1, static_cast<int>(all_crew_ids.size() * percentage));
    
    // 随机选择指定数量的机长
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, all_crew_ids.size() - 1);
    
    // 使用set来避免重复选择
    std::set<std::string> selected_set;
    while (selected_set.size() < num_crews_to_select) {
        int random_index = dis(gen);
        selected_set.insert(all_crew_ids[random_index]);
    }
    
    // 将set转换为vector
    selected_crews.assign(selected_set.begin(), selected_set.end());

    return selected_crews;
}

// 破坏阶段：移除部分机长的排班
void SolutionConstructor::ruin_solution(SolutionState& solution, const std::vector<std::string>& selected_crews) {
    // 对于每个选中的机长
    for (const auto& crew_id : selected_crews) {
        // 获取该机长的所有任务
        if (solution.crew_dutyperiods.find(crew_id) != solution.crew_dutyperiods.end()) {
            const auto& duty_periods = solution.crew_dutyperiods[crew_id];
            
            // 遍历所有任务周期
            for (const auto& duty_period : duty_periods) {
                // 遍历任务周期中的所有任务
                for (const auto& task : duty_period.tasks) {
                    // 如果是航班任务，则从航班分配中移除
                    if (std::holds_alternative<Flight>(task)) {
                        const auto& flight = std::get<Flight>(task);
                        solution.flight_assignments.erase(flight.id);
                    }
                    // 注意：地面任务和巴士任务保持不变，因为它们是固定的
                }
            }
            
            // 清空该机长的任务周期
            solution.crew_dutyperiods.erase(crew_id);
            
            // 清空该机长的周期
            if (solution.crew_cycles.find(crew_id) != solution.crew_cycles.end()) {
                solution.crew_cycles.erase(crew_id);
            }
        }
    }
    
    // std::cout << "完成破坏阶段，已移除选中机长的排班" << std::endl;
}

// 重建阶段：为被选中的机长重新安排航班
void SolutionConstructor::recreate_solution(SolutionState& solution, const std::vector<std::string>& selected_crews) {
    const auto& crews = data_.getCrews();
    

    // 对于每个选中的机长
    for (const auto& crew_id : selected_crews) {
        // 确保机长存在于数据中
        if (crews.find(crew_id) != crews.end()) {
            const auto& crew = crews.at(crew_id);
            
            // 创建机长调度构建器
            CrewSchedule crew_schedule_builder(data_, crew, 
                solution.crew_dutyperiods[crew_id], 
                solution.flight_assignments,
                solution.crew_cycles[crew_id]);
            
            // 为该机长构建新的调度
            crew_schedule_builder.construct_schedule_DFS();
        }
    }
    
    // std::cout << "完成重建阶段，已为选中机长重新安排排班" << std::endl;
}

// 计算接受新解的概率（模拟退火）
double SolutionConstructor::calculate_acceptance_probability(double delta_energy, double temperature) {
    if (delta_energy <= 0) {
        // 如果新解更好，总是接受
        return 1.0;
    } else {
        // 如果新解更差，根据差距和温度计算接受概率
        return std::exp(-delta_energy / temperature);
    }
}

// 多线程工作函数：处理一组路径
void SolutionConstructor::thread_worker(
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
) {
    // 为每个线程创建独立的随机数生成器
    std::random_device rd;
    std::mt19937 local_rng(rd());
    std::vector<std::uniform_real_distribution<double>> random_distributions;
    
    // 为每条路径创建独立的随机分布
    for (int i = start_idx; i < end_idx; ++i) {
        random_distributions.emplace_back(0.0, 1.0);
    }
    
    // 开始迭代优化
    for (int iter = 0; iter < max_iterations && !should_terminate; iter++) {
        // 对每条分配给该线程的路径进行一次迭代
        for (int path_offset = 0; path_offset < (end_idx - start_idx); path_offset++) {
            int path_idx = start_idx + path_offset;
            
            // 当前路径的当前解
            SolutionState& current_solution = paths[path_idx];
            
            // 当前路径的温度和破坏比例
            double& temperature = temperatures[path_idx];
            double& ruin_percentage = ruin_percentages[path_idx];
            
            // 生成新解
            std::vector<std::string> crews_to_ruin = select_crews_to_ruin(current_solution, ruin_percentage);
            if (!crews_to_ruin.empty()) {
                // 创建当前解的副本
                SolutionState candidate_solution = current_solution;
                
                // 执行破坏操作
                ruin_solution(candidate_solution, crews_to_ruin);
                
                // 执行重建操作
                recreate_solution(candidate_solution, crews_to_ruin);
                
                // 重新计算解的分数
                candidate_solution.score = 0.0;
                for (const auto& flight : candidate_solution.flight_assignments) {
                    for (const auto& task : flight.second) {
                        if (task.second) {
                            candidate_solution.score += 1;
                            break;
                        }
                    }
                }
                
                // 计算能量差（分数差）
                double delta_energy = current_solution.score - candidate_solution.score;
                
                // 决定是否接受新解
                bool accept_new_solution = false;
                if (delta_energy <= 0) {
                    // 如果新解更好，总是接受
                    accept_new_solution = true;
                } else {
                    // 如果新解更差，根据模拟退火准则决定是否接受
                    double acceptance_probability = calculate_acceptance_probability(delta_energy, temperature);
                    double random_value = random_distributions[path_offset](local_rng);
                    accept_new_solution = (random_value < acceptance_probability);
                }
                
                // 更新当前解
                if (accept_new_solution) {
                    current_solution = candidate_solution;
                    
                    // 如果新解比全局最优解更好，更新全局最优解
                    {
                        // 使用互斥锁保护全局最优解的访问
                        std::lock_guard<std::mutex> lock(global_best_mutex);
                        if (current_solution.score > global_best_solution.score) {
                            global_best_solution = current_solution;
                            std::cout << "线程 " << std::this_thread::get_id() << " 路径 " << path_idx + 1 
                                      << " 找到新的全局最优解，分数: " << global_best_solution.score 
                                      << ", 迭代: " << global_iteration_counter.load() 
                                      << ", 温度: " << temperature << std::endl;
                                ReportGenerator::generate_schedule_report(global_best_solution, data_, "heuristic/report/" + data_version_ + "/schedule_report.txt", std::chrono::steady_clock::now());
                                ReportGenerator::generate_submission_csv(global_best_solution, "heuristic/report/" + data_version_ + "/rosterResult.csv");
                                ReportGenerator::validate_crew_flight_consistency(global_best_solution, "heuristic/report/" + data_version_ + "/crew_flight_consistency.txt");

                        }
                    }
                }
            }
            
            // 降低温度
            temperature = std::max(min_temperature, temperature * cooling_rate);
            
            // 动态调整破坏比例
            if (iter % 50 == 0) {
                // 每50次迭代调整一次破坏比例
                if (temperature > initial_temperature * 0.5) {
                    // 温度较高时，增加破坏比例以促进探索
                    ruin_percentage = std::min(0.3, ruin_percentage * 1.1);
                } else {
                    // 温度较低时，减小破坏比例以促进局部搜索
                    ruin_percentage = std::max(0.02, ruin_percentage * 0.9);
                }
            }
        }
        
        // 增加全局迭代计数器
        int current_iteration = ++global_iteration_counter;
        
        // 每100次全局迭代输出一次进度
        if (current_iteration % 100 == 0 && start_idx == 0) {  // 只让第一个线程输出进度
            std::lock_guard<std::mutex> lock(global_best_mutex);
            std::cout << "完成迭代: " << current_iteration << "/" << (max_iterations * paths.size()) 
                      << ", 当前全局最优分数: " << global_best_solution.score << std::endl;
        }
    }
}

// 多线程版本的模拟退火多路径破坏与重建优化
SolutionState SolutionConstructor::parallel_simulated_annealing_ruin_recreate(
    SolutionState& initial_solution, 
    int num_paths,
    int num_threads,
    int max_iterations,
    double initial_temperature,
    double cooling_rate,
    double min_temperature,
    double initial_ruin_percentage
) {
    // 确定使用的线程数
    if (num_threads <= 0) {
        num_threads = std::thread::hardware_concurrency();
        // 如果无法确定硬件支持的线程数，默认使用4个线程
        if (num_threads == 0) {
            num_threads = 4;
        }
    }
    
    std::cout << "使用 " << num_threads << " 个线程进行并行模拟退火多路径优化" << std::endl;
    std::cout << "创建 " << num_paths << " 条探索路径" << std::endl;
    
    // 创建多条探索路径，每条路径都从初始解出发
    std::vector<SolutionState> paths(num_paths, initial_solution);
    std::vector<double> temperatures(num_paths, initial_temperature);
    std::vector<double> ruin_percentages(num_paths, initial_ruin_percentage);
    
    // 记录全局最优解
    SolutionState global_best_solution = initial_solution;
    
    // 创建互斥锁，用于保护全局最优解的访问
    std::mutex global_best_mutex;
    
    // 创建原子布尔变量，用于通知所有线程终止
    std::atomic<bool> should_terminate(false);
    
    // 创建原子整数，用于跟踪全局迭代次数
    std::atomic<int> global_iteration_counter(0);
    
    // 创建线程池
    std::vector<std::thread> threads;
    
    // 计算每个线程处理的路径数
    int paths_per_thread = num_paths / num_threads;
    int remaining_paths = num_paths % num_threads;
    
    // 启动线程
    int start_idx = 0;
    for (int t = 0; t < num_threads; ++t) {
        // 计算该线程处理的路径范围
        int end_idx = start_idx + paths_per_thread + (t < remaining_paths ? 1 : 0);
        
        // 创建并启动线程
        threads.emplace_back(
            &SolutionConstructor::thread_worker,
            this,
            std::ref(paths),
            std::ref(temperatures),
            std::ref(ruin_percentages),
            start_idx,
            end_idx,
            max_iterations,
            cooling_rate,
            min_temperature,
            initial_temperature,
            std::ref(should_terminate),
            std::ref(global_best_mutex),
            std::ref(global_best_solution),
            std::ref(global_iteration_counter)
        );
        
        start_idx = end_idx;
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    // 最终路径交叉：找出最佳路径
    double best_path_score = paths[0].score;
    int best_path_idx = 0;
    
    for (int i = 1; i < num_paths; i++) {
        if (paths[i].score > best_path_score) {
            best_path_score = paths[i].score;
            best_path_idx = i;
        }
    }
    
    // 如果最佳路径比全局记录的最优解更好，更新全局最优解
    if (best_path_score > global_best_solution.score) {
        global_best_solution = paths[best_path_idx];
    }
    
    std::cout << "并行模拟退火多路径优化完成" << std::endl;
    std::cout << "全局最优解分数: " << global_best_solution.score << std::endl;
    
    return global_best_solution;
}

// 使用模拟退火的多路径破坏与重建优化（单线程版本）
SolutionState SolutionConstructor::simulated_annealing_ruin_recreate(
    SolutionState& initial_solution, 
    int num_paths,
    int max_iterations,
    double initial_temperature,
    double cooling_rate,
    double min_temperature,
    double initial_ruin_percentage
) {
    // 创建多条探索路径，每条路径都从初始解出发
    std::vector<SolutionState> paths(num_paths, initial_solution);
    std::vector<double> temperatures(num_paths, initial_temperature);
    std::vector<double> ruin_percentages(num_paths, initial_ruin_percentage);
    
    // 记录全局最优解
    SolutionState global_best_solution = initial_solution;
    
    // 为每条路径创建独立的随机数生成器
    std::vector<std::uniform_real_distribution<double>> random_distributions(num_paths, std::uniform_real_distribution<double>(0.0, 1.0));
    
    // 开始迭代优化
    for (int iter = 0; iter < max_iterations; iter++) {
        // 对每条路径进行一次迭代
        for (int path_idx = 0; path_idx < num_paths; path_idx++) {
            // 当前路径的当前解
            SolutionState& current_solution = paths[path_idx];
            
            // 当前路径的温度和破坏比例
            double& temperature = temperatures[path_idx];
            double& ruin_percentage = ruin_percentages[path_idx];
            
            // 生成新解
            std::vector<std::string> crews_to_ruin = select_crews_to_ruin(current_solution, ruin_percentage);
            if (!crews_to_ruin.empty()) {
                // 创建当前解的副本
                SolutionState candidate_solution = current_solution;
                
                // 执行破坏操作
                ruin_solution(candidate_solution, crews_to_ruin);
                
                // 执行重建操作
                recreate_solution(candidate_solution, crews_to_ruin);
                
                // 重新计算解的分数
                candidate_solution.score = 0.0;
                for (const auto& flight : candidate_solution.flight_assignments) {
                    for (const auto& task : flight.second) {
                        if (task.second) {
                            candidate_solution.score += 1;
                            break;
                        }
                    }
                }
                
                // 计算能量差（分数差）
                double delta_energy = current_solution.score - candidate_solution.score;
                
                // 决定是否接受新解
                bool accept_new_solution = false;
                if (delta_energy <= 0) {
                    // 如果新解更好，总是接受
                    accept_new_solution = true;
                } else {
                    // 如果新解更差，根据模拟退火准则决定是否接受
                    double acceptance_probability = calculate_acceptance_probability(delta_energy, temperature);
                    double random_value = random_distributions[path_idx](rng_);
                    accept_new_solution = (random_value < acceptance_probability);
                }
                
                // 更新当前解
                if (accept_new_solution) {
                    current_solution = candidate_solution;
                    
                    // 如果新解比全局最优解更好，更新全局最优解
                    if (current_solution.score > global_best_solution.score) {
                        global_best_solution = current_solution;
                        std::cout << "路径 " << path_idx + 1 << " 找到新的全局最优解，分数: " 
                                  << global_best_solution.score << ", 迭代: " << iter + 1 
                                  << ", 温度: " << temperature << std::endl;
                    }
                }
            }
            
            // 降低温度
            temperature = std::max(min_temperature, temperature * cooling_rate);
            
            // 动态调整破坏比例
            if (iter % 50 == 0) {
                // 每50次迭代调整一次破坏比例
                if (temperature > initial_temperature * 0.5) {
                    // 温度较高时，增加破坏比例以促进探索
                    ruin_percentage = std::min(0.3, ruin_percentage * 1.1);
                } else {
                    // 温度较低时，减小破坏比例以促进局部搜索
                    ruin_percentage = std::max(0.02, ruin_percentage * 0.9);
                }
            }
        }
        
        // 每100次迭代输出一次进度
        if ((iter + 1) % 100 == 0) {
            std::cout << "完成迭代: " << iter + 1 << "/" << max_iterations 
                      << ", 当前全局最优分数: " << global_best_solution.score << std::endl;
        }
        
        // 每200次迭代进行路径交叉（将全局最优解注入到表现最差的路径）
        if ((iter + 1) % 200 == 0 && iter > 0) {
            // 找出表现最差的路径
            int worst_path_idx = 0;
            double worst_score = paths[0].score;
            
            for (int i = 1; i < num_paths; i++) {
                if (paths[i].score < worst_score) {
                    worst_score = paths[i].score;
                    worst_path_idx = i;
                }
            }
            
            // 将全局最优解注入到表现最差的路径
            paths[worst_path_idx] = global_best_solution;
            // 重置该路径的温度和破坏比例，以便它可以进行更多的探索
            temperatures[worst_path_idx] = initial_temperature * 0.5;
            ruin_percentages[worst_path_idx] = initial_ruin_percentage;
            
            std::cout << "路径交叉: 将全局最优解注入到路径 " << worst_path_idx + 1 << std::endl;
        }
    }
    
    return global_best_solution;
}