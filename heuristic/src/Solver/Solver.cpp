// src/Solver.cpp

#include "Solver.hpp"
#include "../Constructor/SolutionConstruct.hpp"
#include <iostream>
#include "ReportGenerator.hpp"
#include <chrono>
// #include <mpi.h> // mpi is not used in this project
//#include "ReportGenerator.h"

Solver::Solver(int argc, char* argv[]) {}

void Solver::run() {    

    std::filesystem::path data_dir = "/home/dbxp/CTS-2025/data/0623";
    const DataLoader data_loader(data_dir);

    SolutionConstructor solution_constructor(data_loader);
    SolutionState best_solution;

    int max_iter = 1000;
    for(int i = 0; i < max_iter; i++) {
        std::cout << "iter " << i + 1 << std::endl;
        auto start_time = std::chrono::steady_clock::now();
        SolutionState current_solution = solution_constructor.generate_schedule();
        if (current_solution.score > best_solution.score) {
            best_solution = current_solution;
            ReportGenerator::generate_schedule_report(current_solution, data_loader, "/home/dbxp/CTS-2025/heuristic/report/schedule_report.txt", start_time);
            ReportGenerator::generate_submission_csv(current_solution, "/home/dbxp/CTS-2025/heuristic/report/rosterResult.csv");
            ReportGenerator::validate_crew_flight_consistency(current_solution, "/home/dbxp/CTS-2025/heuristic/report/crew_flight_consistency.txt");
        }
    }

    // ReportGenerator::generate_schedule_report(best_solution, "schedule_report.txt");
}