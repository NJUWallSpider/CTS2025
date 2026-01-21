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

    std::string data_version = "0623";
    std::filesystem::path data_dir = std::filesystem::path("data") / data_version;
    const DataLoader data_loader(data_dir, data_version);

    SolutionConstructor solution_constructor(data_loader);
    SolutionState best_solution;

    // Phase 1: Generate Initial Solution
    std::cout << "Generating initial solution..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();
    SolutionState current_solution = solution_constructor.generate_schedule();
    best_solution = current_solution;
    
    std::string directory = "heuristic/report/" + data_version + "/";
    ReportGenerator::generate_schedule_report(best_solution, data_loader, directory + "schedule_report.txt", start_time);
    ReportGenerator::generate_submission_csv(best_solution, directory + "rosterResult.csv");
    ReportGenerator::validate_crew_flight_consistency(best_solution, directory + "crew_flight_consistency.txt");
    ReportGenerator::generate_assignment_report(best_solution, data_loader, directory + "assignment_report.csv");
    ReportGenerator::generate_crew_dutyperiod_report(best_solution, data_loader, directory + "crew_dutyperiod_report.csv");
    std::cout << "Initial solution score: " << best_solution.score << std::endl;



    
    // Phase 2: Use Multi-threaded Simulated Annealing with Multi-path Ruin and Recreate Optimization
    std::cout << "Starting multi-threaded simulated annealing multi-path optimization..." << std::endl;
    
    // Get available thread count
    int available_threads = std::thread::hardware_concurrency();
    // If undetermined, default to 4 threads
    if (available_threads == 0) {
        available_threads = 4;
    }
    
    // Simulated Annealing Parameters
    int num_paths = 16;                // Number of exploration paths
    int num_threads = available_threads; // Number of threads to use
    int max_iterations = 15000;        // Maximum iterations
    double initial_temperature = 700.0; // Initial temperature
    double cooling_rate = 0.98;       // Cooling rate
    double min_temperature = 0.01;    // Minimum temperature
    double initial_ruin_percentage = 0.05; // Initial ruin percentage
    
    start_time = std::chrono::steady_clock::now();
    
    // Execute Multi-threaded Simulated Annealing Multi-path Optimization
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
    
    // Generate Final Report
    std::cout << "Optimization complete, generating final report..." << std::endl;
    ReportGenerator::generate_schedule_report(best_solution, data_loader, directory + "schedule_report.txt", start_time);
    ReportGenerator::generate_submission_csv(best_solution, directory + "rosterResult.csv");
    ReportGenerator::validate_crew_flight_consistency(best_solution, directory + "crew_flight_consistency.txt");

    std::cout << "Optimization complete, final solution score: " << best_solution.score << std::endl;
}
