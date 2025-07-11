// src/Solver.cpp

#include "Solver.hpp"
#include "../Constructor/SolutionConstruct.hpp"
#include <iostream>
#include "ReportGenerator.hpp"
#include <chrono>
#include <thread>
// #include <mpi.h> // mpi is not used in this project
//#include "ReportGenerator.h"

Solver::Solver(int argc, char* argv[]) {}

void Solver::run() {    

    std::string data_version = "0711";
    std::filesystem::path data_dir = std::filesystem::path("data") / data_version;
    // std::filesystem::path heuristic_path = std::filesystem::path("exact_solver") / "report" / data_version / "rosterResult.csv";
    std::filesystem::path heuristic_path = std::filesystem::path("/home/mip/new-cts/empty_submission.csv");
    const DataLoader data_loader(data_dir, heuristic_path);
    std::string start_str = "2025/1/31 00:00";

    SolutionConstructor solution_constructor(data_loader, start_str);
    SolutionState best_solution;

    // 第一阶段：生成初始解
    std::cout << "正在生成初始解..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();
    SolutionState current_solution = solution_constructor.generate_schedule();
    best_solution = current_solution;
    
    std::string directory = "heuristic/report/" + data_version + "/";
    ReportGenerator::generate_schedule_report(best_solution, data_loader, directory + "schedule_report.txt", start_time);
    ReportGenerator::generate_submission_csv(best_solution, directory + "rosterResult.csv");
    ReportGenerator::validate_crew_flight_consistency(best_solution, directory + "crew_flight_consistency.txt");
    
    std::cout << "初始解分数: " << best_solution.score << std::endl;
    
    // 第二阶段：使用多线程模拟退火的多路径破坏与重建优化
    std::cout << "开始多线程模拟退火多路径优化..." << std::endl;
    
    // 获取可用的线程数
    int available_threads = std::thread::hardware_concurrency();
    // 如果无法确定，默认使用4个线程
    if (available_threads == 0) {
        available_threads = 4;
    }
    
    // 模拟退火参数
    int num_paths = 72;                // 探索路径数量
    int num_threads = available_threads; // 使用的线程数
    int max_iterations = 160;        // 最大迭代次数
    double initial_temperature = 1.0; // 初始温度
    double cooling_rate = 0.95;       // 冷却率
    double min_temperature = 0.01;    // 最小温度
    double initial_ruin_percentage = 0.05; // 初始破坏比例
    
    start_time = std::chrono::steady_clock::now();
    
    // 执行多线程模拟退火多路径优化
    best_solution = solution_constructor.parallel_simulated_annealing_ruin_recreate(
        current_solution,
        num_paths,
        num_threads,
        max_iterations,
        initial_temperature,
        cooling_rate,
        min_temperature,
        initial_ruin_percentage
    );
    
    // 生成最终报告
    std::cout << "优化完成，生成最终报告..." << std::endl;
    ReportGenerator::generate_schedule_report(best_solution, data_loader, directory + "schedule_report.txt", start_time);
    ReportGenerator::generate_submission_csv(best_solution, directory + "rosterResult.csv");
    ReportGenerator::validate_crew_flight_consistency(best_solution, directory + "crew_flight_consistency.txt");
    
    std::cout << "优化完成，最终解分数: " << best_solution.score << std::endl;
}