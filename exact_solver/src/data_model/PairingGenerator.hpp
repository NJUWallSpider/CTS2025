// PairingGenerator.hpp
#pragma once

#include "SchedulingData.hpp"
#include <future>

class PairingGenerator {
public:
    // Receive reference to SchedulingData object to avoid copying
    explicit PairingGenerator(SchedulingData& data, Date start_date, Date end_date);

    // Main function, build all valid FDPs
    void build_all_valid_fdps();
    
    // Single-threaded version of build function, for debugging
    void build_all_valid_fdps_single_thread();

private:
    // --- Internal Methods ---
    void _prepare_tasks();

    // DFS core function, generate FDPs for a single (airport, date) combination
    // This function will be called concurrently by multiple threads
    std::vector<FDP> _process_airport_date(const std::string& airport, const Date& date);

    // Recursive DFS builder
    void _dfs_fdp_builder(
        std::vector<Task>& current_path,
        std::unordered_set<std::string>& used_task_ids,
        std::vector<FDP>& found_fdps
    );

    // --- Member Variables ---
    SchedulingData& data_; // Stores reference to original data
    std::unordered_map<std::string, std::vector<Task>> tasks_by_airport_;

    // --- Algorithm Constants ---
    const std::chrono::hours MIN_CONNECTION_TIME_BUS; 
    const std::chrono::hours MIN_CONNECTION_TIME_FLIGHT;
    const int MAX_FLIGHT_TASKS_PER_FDP;
    const int MAX_TOTAL_TASKS_PER_FDP;
    const std::chrono::hours MAX_FLY_TIME_PER_FDP;
    const std::chrono::hours MAX_DUTY_TIME_PER_FDP;

    Date start_date_;
    Date end_date_;
};
