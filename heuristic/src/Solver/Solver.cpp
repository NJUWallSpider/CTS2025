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

    std::string data_version = "0623";
    std::filesystem::path data_dir = std::filesystem::path("data") / data_version;
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
            ReportGenerator::generate_schedule_report(current_solution, data_loader, "heuristic/report/" + data_version + "/schedule_report.txt", start_time);
            ReportGenerator::generate_submission_csv(current_solution, "heuristic/report/" + data_version + "/rosterResult.csv");
            ReportGenerator::validate_crew_flight_consistency(current_solution, "heuristic/report/" + data_version + "/crew_flight_consistency.txt");
        }
    }

    // ReportGenerator::generate_schedule_report(best_solution, "schedule_report.txt");
}