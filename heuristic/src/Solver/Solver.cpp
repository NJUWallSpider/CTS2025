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

    std::filesystem::path data_dir = "/home/bhz/new-cts/data/0606";
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
            std::string directory = "/home/bhz/new-cts/heuristic/report/";
            ReportGenerator::generate_schedule_report(current_solution, data_loader, directory + "schedule_report.txt", start_time);
            ReportGenerator::generate_submission_csv(current_solution, directory + "rosterResult.csv");
            ReportGenerator::validate_crew_flight_consistency(current_solution, directory + "crew_flight_consistency.txt");
        }
    }

    // ReportGenerator::generate_schedule_report(best_solution, "schedule_report.txt");
}