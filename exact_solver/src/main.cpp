#include "gurobi_c++.h"
#include "data_model/SchedulingData.hpp"
#include "gurobi_solvers/MasterProblem.h"
#include "subproblem/SubproblemSolver.h"
#include "data_model/PairingGenerator.hpp"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <queue>
#include <atomic>
#include <random>    // 添加随机数生成器头文件
#include <algorithm> // 添加算法库头文件

int main(int argc, char** argv) {
    try {
        // 检查命令行参数
        // if (argc < 2) {
        //     std::cerr << "用法: " << argv[0] << " <数据目录路径>" << std::endl;
        //     return 1;
        // }

        // 此处是所有使用到的数据路径
        std::string data_version = "0606";
        std::filesystem::path data_path = std::filesystem::path("data") / data_version; 
        std::string fdp_path = "exact_solver/fdp_networks/" + data_version; // 储存FDP网络的文件夹路径
        std::filesystem::path heuristic_path = std::filesystem::path("heuristic") / "report" / data_version / "rosterResult.csv"; // 储存启发式解的文件夹路径
        Date start_date{std::chrono::year(2025)/std::chrono::May/std::chrono::day(29)};
        Date end_date{std::chrono::year(2025)/std::chrono::June/std::chrono::day(4)};
        
        // 设置非基地机场结束的FDP被拒绝的概率
        double non_base_rejection_prob = 0; 
        
        // 设置任务数量阈值，超过此阈值将随机删除bus
        size_t max_tasks_threshold = 6000;
        
        // 设置Beam搜索的宽度
        int beam_width = 25;

        const int MAX_ITERATIONS = 100; // 设置最大迭代次数

        const int NUM_THREADS = 8;  // 设置线程数

        // 加载调度数据
        std::cout << "正在加载数据..." << std::endl;
        SchedulingData data(data_path, heuristic_path, max_tasks_threshold);
        std::cout << "数据加载完成。" << std::endl;

        // --- Phase 1: Pairing Generation ---
        std::cout << "\n--- 开始执行第一阶段：FDP生成 ---" << std::endl;
        auto phase1_start_time = std::chrono::high_resolution_clock::now();
        PairingGenerator fdp_generator(data, start_date, end_date);
        fdp_generator.build_all_valid_fdps();
        auto phase1_end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> phase1_duration = phase1_end_time - phase1_start_time;

        long long total_fdps = 0;
        for(const auto& pair : data.all_valid_fdps_) total_fdps += pair.second.size();
        std::cout << "\n第一阶段完成，共生成 " << total_fdps << " 个FDP，耗时: " 
                    << phase1_duration.count() << " 秒。" << std::endl;
        
        // 创建Gurobi环境
        std::cout << "初始化Gurobi环境..." << std::endl;
        GRBEnv env = GRBEnv();
        env.set(GRB_IntParam_OutputFlag, 1); // 设置为1以显示Gurobi输出
        std::cout << "Gurobi环境创建成功。" << std::endl;
        std::cout << "Gurobi版本: " << GRB_VERSION_MAJOR << "." << GRB_VERSION_MINOR << "." << GRB_VERSION_TECHNICAL << std::endl;
        
        // 创建主问题
        std::cout << "\n创建主问题..." << std::endl;
        MasterProblem master(data, data_version);
        master.initialize();
        
        // 创建子问题求解器
        std::cout << "\n创建子问题求解器..." << std::endl;
        SubproblemSolver subproblem(data, master, fdp_path, non_base_rejection_prob, beam_width);
        
        // 预处理所有机组的FDP网络
        std::cout << "\n预处理所有机组的FDP网络..." << std::endl;
        auto start_precompute = std::chrono::steady_clock::now();
        subproblem.precomputeAllFDPNetworks();
        auto end_precompute = std::chrono::steady_clock::now();
        auto precompute_time = std::chrono::duration_cast<std::chrono::seconds>(end_precompute - start_precompute).count();
        std::cout << "FDP网络预处理完成，耗时: " << precompute_time << " 秒" << std::endl;
        
        // 列生成算法的主循环
        std::cout << "\n开始列生成算法..." << std::endl;
        int iteration = 0;
        bool found_new_columns = true;
        
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        while (iteration < MAX_ITERATIONS) {
            iteration++;
            std::cout << "\n===== 迭代 " << iteration << " =====" << std::endl;
            
            // 求解当前主问题
            master.solve();
            std::cout << "当前主问题目标值: " << master.getObjectiveValue() << std::endl;
            
            // 获取所有机长
            const auto& crews = data.get_all_crews();
            
            // 将机长ID放入vector中便于随机选择
            std::vector<std::string> crew_ids;
            for (const auto& [crew_id, _] : crews) {
                crew_ids.push_back(crew_id);
            }
            
            // 创建随机数生成器
            std::random_device rd;
            std::mt19937 gen(rd());
            
            // 计算需要抽样的机长数量（10%，至少1个）
            size_t sample_size = std::max(size_t(1), crews.size() / 10);
            
            // 随机打乱vector
            std::shuffle(crew_ids.begin(), crew_ids.end(), gen);
            
            // 只取前sample_size个机长
            crew_ids.resize(sample_size);
            
            std::cout << "本次迭代随机抽样 " << sample_size << " 个机长（总数的10%）求解子问题..." << std::endl;
            
            // 创建机长ID队列
            std::queue<std::string> crew_queue;
            for (const auto& crew_id : crew_ids) {
                crew_queue.push(crew_id);
            }
            
            // 使用原子变量记录添加的列数
            std::atomic<int> columns_added(0);
            std::atomic<bool> found_any_columns(false);
            
            // 用于保护队列的互斥锁
            std::mutex queue_mutex;
            
            // 用于保护主问题更新的互斥锁
            std::mutex master_mutex;
            
            // 创建工作线程
            std::vector<std::thread> threads;
            for (int t = 0; t < NUM_THREADS; ++t) {
                threads.emplace_back([&]() {
                    // 每个线程创建自己的子问题求解器
                    SubproblemSolver subproblem(data, master, fdp_path, non_base_rejection_prob, beam_width);
                    
                    while (true) {
                        // 从队列中获取一个机长ID
                        std::string crew_id;
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex);
                            if (crew_queue.empty()) {
                                break;  // 队列为空，线程退出
                            }
                            crew_id = crew_queue.front();
                            crew_queue.pop();
                        }
                        
                        // 为该机长求解子问题
                        bool found = subproblem.solveForCrew(crew_id);
                        
                        if (found) {
                            // 找到了有价值的新列，添加到主问题
                            double reduced_cost = subproblem.getReducedCost();
                            const FDP& best_fdp = subproblem.getBestFDP();
                            
                            // 添加新列到主问题（需要加锁保护）
                            {
                                std::lock_guard<std::mutex> lock(master_mutex);
                                int result = master.addColumn(crew_id, best_fdp);
                                if (result == 1) {
                                    found_any_columns = true;
                                    columns_added++;
                                }
                            }
                        }
                    }
                });
            }
            
            // 等待所有线程完成
            for (auto& thread : threads) {
                thread.join();
            }
            
            found_new_columns = found_any_columns;
            std::cout << "本次迭代添加了 " << columns_added << " 个新列。" << std::endl;
            
            // 如果没有找到新列，算法收敛
            // if (!found_new_columns) {
            //     std::cout << "没有找到有价值的新列，算法收敛。" << std::endl;
            //     break;
            // }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
        
        // 输出最终结果
        std::cout << "\n===== 列生成算法完成 =====" << std::endl;
        std::cout << "总迭代次数: " << iteration << std::endl;
        std::cout << "总运行时间: " << duration << " 秒" << std::endl;
        std::cout << "非基地机场结束的FDP被拒绝的概率: " << non_base_rejection_prob << std::endl;
        std::cout << "任务数量阈值: " << max_tasks_threshold << std::endl;
        std::cout << "Beam搜索宽度: " << beam_width << std::endl;
        
        // 求解最终的整数主问题
        std::cout << "\n求解最终的整数主问题..." << std::endl;
        master.solveIntegerProgram();
        
        // 输出最终解决方案
        master.printSolution();

    } catch (GRBException& e) {
        std::cerr << "Gurobi错误: " << e.getErrorCode() << std::endl;
        std::cerr << e.getMessage() << std::endl;
        return 1;
    } catch (std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "未知错误" << std::endl;
        return 1;
    }

    // 创建 SubproblemSolver 实例
    // for (const auto& [crew_id, crew] : data.get_all_crews()) {
        // subproblem.testSerialization("Crew_11202");
    // }


    return 0;
}