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
#include <random>    // Add random number generator header
#include <algorithm> // Add algorithm library header

int main(int argc, char** argv) {
    try {
        // Check command line arguments
        // if (argc < 2) {
        //     std::cerr << "Usage: " << argv[0] << " <data directory path>" << std::endl;
        //     return 1;
        // }

        // Paths for all data used
        std::string data_version = "0623";
        std::filesystem::path data_path = std::filesystem::path("data") / data_version; 
        std::string fdp_path = "exact_solver/fdp_networks/" + data_version; // Folder path to store FDP networks
        std::filesystem::path heuristic_path = std::filesystem::path("heuristic") / "report" / data_version / "rosterResult.csv"; // Folder path to store heuristic solution
        // std::filesystem::path heuristic_path = std::filesystem::path("/home/dbxp/new-cts/result_0623.csv");
        Date start_date{std::chrono::year(2025)/std::chrono::April/std::chrono::day(29)};
        Date end_date{std::chrono::year(2025)/std::chrono::May/std::chrono::day(7)};
        
        // Set rejection probability for FDPs ending at non-base airport
        double non_base_rejection_prob = 0; 
        
        // Set task count threshold, randomly delete bus if exceeded
        size_t max_tasks_threshold = 10000;
        
        // Set Beam search width
        int beam_width = 50;

        const int MAX_ITERATIONS = 100; // Set maximum iterations

        const int NUM_THREADS = 8;  // Set number of threads

        // Load scheduling data
        std::cout << "Loading data..." << std::endl;
        SchedulingData data(data_path, heuristic_path, max_tasks_threshold);
        std::cout << "Data loading complete." << std::endl;

        // --- Phase 1: Pairing Generation ---
        std::cout << "\n--- Starting Phase 1: FDP Generation ---" << std::endl;
        auto phase1_start_time = std::chrono::high_resolution_clock::now();
        PairingGenerator fdp_generator(data, start_date, end_date);
        fdp_generator.build_all_valid_fdps();
        auto phase1_end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> phase1_duration = phase1_end_time - phase1_start_time;

        long long total_fdps = 0;
        for(const auto& pair : data.all_valid_fdps_) total_fdps += pair.second.size();
        std::cout << "\nPhase 1 complete, generated " << total_fdps << " FDPs, Time elapsed: " 
                    << phase1_duration.count() << " seconds." << std::endl;
        
        // Create Gurobi environment
        std::cout << "Initializing Gurobi environment..." << std::endl;
        GRBEnv env = GRBEnv();
        env.set(GRB_IntParam_OutputFlag, 1); // Set to 1 to show Gurobi output
        std::cout << "Gurobi environment created successfully." << std::endl;
        std::cout << "Gurobi Version: " << GRB_VERSION_MAJOR << "." << GRB_VERSION_MINOR << "." << GRB_VERSION_TECHNICAL << std::endl;
        
        // Create Master Problem
        std::cout << "\nCreating Master Problem..." << std::endl;
        MasterProblem master(data, data_version);
        master.initialize();
        
        // Create Subproblem Solver
        std::cout << "\nCreating Subproblem Solver..." << std::endl;
        SubproblemSolver subproblem(data, master, fdp_path, non_base_rejection_prob, beam_width);
        
        // Precompute all crew FDP networks
        std::cout << "\nPrecomputing all crew FDP networks..." << std::endl;
        auto start_precompute = std::chrono::steady_clock::now();
        subproblem.precomputeAllFDPNetworksParallel(8);
        auto end_precompute = std::chrono::steady_clock::now();
        auto precompute_time = std::chrono::duration_cast<std::chrono::seconds>(end_precompute - start_precompute).count();
        std::cout << "FDP network precomputation complete, Time elapsed: " << precompute_time << " seconds" << std::endl;
        
        // Main loop of Column Generation algorithm
        std::cout << "\nStarting Column Generation algorithm..." << std::endl;
        int iteration = 0;
        bool found_new_columns = true;
        
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        while (iteration < MAX_ITERATIONS) {
            iteration++;
            std::cout << "\n===== Iteration " << iteration << " =====