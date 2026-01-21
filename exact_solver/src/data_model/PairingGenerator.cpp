// PairingGenerator.cpp
#include "PairingGenerator.hpp"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <thread>
#include <vector>
#include <fstream>

void write_results(const std::string& airport, const Date& date, const std::vector<FDP>& found_fdps) {
    // Attempt to open file
    std::ofstream out_file("/home/bhz/CTS-2025/Pair-and-Assign/FDP_result.txt", std::ios::app);

    // Critical check: confirm file opened successfully
    if (out_file.is_open()) {
        // File opened successfully, perform write
        out_file << "--------------------------------" << std::endl;
        out_file << "Airport: " << airport << " Date: " << std::to_string(int(date.year())) + "-" +
            std::to_string(unsigned(date.month())) + "-" +
            std::to_string(unsigned(date.day())) << std::endl;
        out_file << "--------------------------------" << std::endl;    
        out_file << "Total FDPs: " << found_fdps.size() << std::endl;
        for (const auto& fdp : found_fdps) {
            out_file << fdp.to_string() << std::endl;
        }
        out_file << std::endl;
        out_file.close(); 
    } else {
        // File open failed, report issue to standard error stream
        std::cerr << "Error: Unable to open file for writing at /home/bhz/CTS-2025/Pair-and-Assign/FDP_result.txt" << std::endl;
        std::cerr << "Please check if the directory exists and you have write permissions." << std::endl;
    }
}

// --- Constructor ---
PairingGenerator::PairingGenerator(SchedulingData& data, Date start_date, Date end_date) : 
    data_(data), 
    start_date_(start_date),
    end_date_(end_date),
    MIN_CONNECTION_TIME_BUS(std::chrono::hours(2)),
    MIN_CONNECTION_TIME_FLIGHT(std::chrono::hours(3)),
    MAX_FLIGHT_TASKS_PER_FDP(4),
    MAX_TOTAL_TASKS_PER_FDP(6),
    MAX_FLY_TIME_PER_FDP(std::chrono::hours(8)),
    MAX_DUTY_TIME_PER_FDP(std::chrono::hours(12)){
    _prepare_tasks();
}

// --- Task Preprocessing ---
// This function is responsible for preprocessing all tasks, including:
// 1. Converting flight tasks into two types:
//    - flight: Normal flight duty task
//    - positioning_flight: Positioning flight task where crew flies as passengers
// 2. Adding bus tasks
// 3. Grouping tasks by departure airport and sorting by departure time
void PairingGenerator::_prepare_tasks() {
    std::cout << "Preprocessing all tasks..." << std::endl;

    // 1. Add all flights, generating two tasks for each flight: 'flight' and 'positioning_flight'
    for (const auto& pair : data_.get_all_flights()) {
        const Flight& flt = pair.second;
        
        // Construct Task directly in map's vector, more efficient than creating temp vector then grouping
        tasks_by_airport_[flt.depa_airport].emplace_back(Task{
            flt.id, "flight", flt.depa_airport, flt.arri_airport,
            flt.std, flt.sta, std::chrono::minutes(flt.fly_time), flt.aircraft_no
        });

        // tasks_by_airport_[flt.depa_airport].emplace_back(Task{
        //     flt.id, "deadhead_flight", flt.depa_airport, flt.arri_airport,
        //     flt.std, flt.sta, std::chrono::minutes(flt.fly_time), flt.aircraft_no
        // });
    }

    // 2. Add all bus tasks
    for (const auto& pair : data_.get_all_buses()) {
        const Bus& bus = pair.second;
        tasks_by_airport_[bus.depa_airport].emplace_back(Task{
            bus.id, "bus", bus.depa_airport, bus.arri_airport,
            bus.td, bus.ta, std::chrono::minutes(0), ""
        });
    }
    
    // 3. After grouping by departure airport, sort task list for each airport by departure time
    for (auto& pair : tasks_by_airport_) {
        std::sort(pair.second.begin(), pair.second.end(), [](const Task& a, const Task& b) {
            return a.start_time < b.start_time;
        });
    }
    std::cout << "Task preprocessing complete." << std::endl;
}

// --- Single-threaded version of FDP build function ---
void PairingGenerator::build_all_valid_fdps_single_thread() {
    using namespace std::chrono;
    
    Date start_date{year(2025)/May/day(27)};
    Date end_date{year(2025)/June/day(4)};

    // Generate all (Airport, Date) combinations for tasks
    std::vector<std::pair<std::string, Date>> jobs;
    for (const auto& pair : tasks_by_airport_) {
        for (auto d = sys_days(start_date); d < sys_days(end_date); d += days(1)) {
            jobs.emplace_back(pair.first, Date(d));
        }
    }

    std::cout << "Calculating using single-threaded mode..." << std::endl;
    std::cout << "Total tasks: " << jobs.size() << std::endl;

    // Process each task sequentially
    int count = 0;
    for (const auto& job : jobs) {
        // Call processing function directly, without async
        std::vector<FDP> result_fdps = _process_airport_date(job.first, job.second);
        
        if (!result_fdps.empty()) {
            data_.all_valid_fdps_[job] = std::move(result_fdps);
        }
        
        // Print progress
        if (++count % 10 == 0) {
            std::cout << "Completed " << count << " / " << jobs.size() << " tasks..." << std::endl;
        }
    }

    // Sort all results by score
    for (auto& pair : data_.all_valid_fdps_) {
        std::sort(pair.second.begin(), pair.second.end(), [](const FDP& a, const FDP& b) {
            return a.get_score() > b.get_score();
        });
    }

    // Output results to file
    for (const auto& [job, fdps] : data_.all_valid_fdps_) {
        write_results(job.first, job.second, fdps);
    }

    std::cout << "All valid FDPs constructed." << std::endl;
    
    // Output example FDPs
    if (!data_.all_valid_fdps_.empty()) {
        auto it = data_.all_valid_fdps_.begin();
        std::cout << "Example Airport " << it->first.first << " Date " << std::to_string(int(it->first.second.year())) + "-" +
            std::to_string(unsigned(it->first.second.month())) + "-" +
            std::to_string(unsigned(it->first.second.day())) << " FDP Count: " << it->second.size() << std::endl;
        
        // Output details of first 5 FDPs
        int fdp_count = 0;
        for (const auto& fdp : it->second) {
            if (fdp_count++ >= 5) break;
            std::cout << fdp.to_string() << std::endl;
        }
    }
}

// --- Concurrent FDP Construction ---
void PairingGenerator::build_all_valid_fdps() {
    using namespace std::chrono;
    
    std::vector<std::pair<std::string, Date>> jobs;
    for (const auto& pair : tasks_by_airport_) {
        for (auto d = sys_days(start_date_); d < sys_days(end_date_); d += days(1)) {
            jobs.emplace_back(pair.first, Date(d));
        }
    }

    unsigned int num_threads = std::thread::hardware_concurrency();
    std::cout << "Using " << num_threads << " threads for parallel calculation..." << std::endl;

    // Store futures of all async tasks
    std::vector<std::future<std::vector<FDP>>> futures;
    futures.reserve(jobs.size()); // Reserve space to improve efficiency

    for (const auto& job : jobs) {
        // std::async starts an asynchronous task
        futures.push_back(std::async(std::launch::async, 
            &PairingGenerator::_process_airport_date, this,
            job.first, job.second));
    }
    
    std::cout << "All tasks submitted, waiting for results..." << std::endl;
    int count = 0;
    for (size_t i = 0; i < futures.size(); ++i) {
        std::vector<FDP> result_fdps = futures[i].get(); // .get() blocks until task completes
        if (!result_fdps.empty()) {
            data_.all_valid_fdps_[jobs[i]] = std::move(result_fdps);
        }
        // Print progress
        if (++count % 100 == 0) {
            std::cout << "Completed " << count << " / " << jobs.size() << " tasks..." << std::endl;
        }
    }

    // Sort all results by score
    for (auto& pair : data_.all_valid_fdps_) {
        std::sort(pair.second.begin(), pair.second.end(), [](const FDP& a, const FDP& b) {
            return a.get_score() > b.get_score();
        });
    }

    // for (const auto& [job, fdps] : data_.all_valid_fdps_) {
    //     write_results(job.first, job.second, fdps);
    // }

    std::cout << "All valid FDPs constructed." << std::endl;
    // ... Can add example output code here ...
}

// --- Handler function for single (Airport, Date) ---
std::vector<FDP> PairingGenerator::_process_airport_date(const std::string& airport, const Date& date) {
    std::vector<FDP> found_fdps;
    auto it = tasks_by_airport_.find(airport);
    if (it == tasks_by_airport_.end()) {
        return found_fdps;
    }

    const auto& tasks_at_airport = it->second;
    for (const auto& initial_task : tasks_at_airport) {
        bool flag = initial_task.id == "Flt_101694";
        Date task_date = std::chrono::floor<std::chrono::days>(initial_task.start_time);
        if (task_date == date) {
            std::vector<Task> current_path = {initial_task};
            std::unordered_set<std::string> used_task_ids = {initial_task.id};
            _dfs_fdp_builder(current_path, used_task_ids, found_fdps);
        }
    }
    
    // Filter out FDPs that only contain bus tasks and start/end at same airport
    found_fdps.erase(
        std::remove_if(found_fdps.begin(), found_fdps.end(),
            [](const FDP& fdp) {
                // Check if all tasks are bus
                bool all_bus = std::all_of(fdp.tasks.begin(), fdp.tasks.end(),
                    [](const Task& task) { return task.task_type == "bus"; });
                
                // If all bus tasks, check if start and end airports are same
                if (all_bus) {
                    return fdp.get_start_airport() == fdp.get_end_airport();
                }
                return false;  // Not all bus FDP, keep
            }
        ),
        found_fdps.end()
    );
    
    return found_fdps;
}

// --- Core DFS Algorithm ---
void PairingGenerator::_dfs_fdp_builder(
    std::vector<Task>& current_path,
    std::unordered_set<std::string>& used_task_ids,
    std::vector<FDP>& found_fdps) 
{
    bool is_end = false;
    // std::vector<FDP> no_good_fdps;
    // --- Rule Check and Pruning ---
    Task& last_task = current_path.back();
    
    // Rule 1: Task count check
    long flight_task_count = std::count_if(current_path.begin(), current_path.end(), 
        [](const Task& t){ return t.task_type != "bus"; });
    
    if (flight_task_count == MAX_FLIGHT_TASKS_PER_FDP) {
        is_end = true;
    } 

    // Rule 2: Total task count limit
    if (current_path.size() == MAX_TOTAL_TASKS_PER_FDP) {
        is_end = true;
    }
    
    // Rule 4: Positioning rule check
    if (current_path.size() >= 3) {
        // Check if middle tasks are positioning tasks
        for (size_t i = 1; i < current_path.size() - 1; ++i) {
            const auto& task_type = current_path[i].task_type;
            if (task_type != "flight") {
                return; // Prune
            }
        }
    }

    // --- Record valid FDP ---
    // Must end at a layover-capable airport
    // if (data_.get_layover_stations().count(current_path.back().end_airport) > 0) {

        FDP new_fdp{0, current_path};
        found_fdps.push_back(std::move(new_fdp));
    // }

    // If termination condition met, stop searching deeper
    if (is_end) {
        return;
    }
    
    // --- Extend Search ---
    auto it = tasks_by_airport_.find(last_task.end_airport);
    if (it == tasks_by_airport_.end()) return;
    std::vector<Task>& next_tasks = it->second;

    for (const auto& next_task : next_tasks) {
        bool flag = next_task.id == "Flt_100324";
        // Connection time check
        if (next_task.start_time < last_task.end_time) continue;
        
        // Avoid cycles
        if (used_task_ids.count(next_task.id)) continue;
        
        // Minimum connection time check
        if (last_task.aircraft_no != next_task.aircraft_no) {
            bool is_bus_involved = (last_task.task_type == "bus" || next_task.task_type == "bus");
            auto min_connection = is_bus_involved ? MIN_CONNECTION_TIME_BUS : MIN_CONNECTION_TIME_FLIGHT;
            if (next_task.start_time < last_task.end_time + min_connection) {
                continue;
            }
        }

        // Move flight time check here, because condition needs to be checked before recursion
        if (next_task.task_type != "bus") {
            auto total_fly_time = std::accumulate(current_path.begin(), current_path.end(), std::chrono::seconds(0), 
                [](std::chrono::seconds sum, const Task& t){ return sum + t.fly_time; });
            total_fly_time += next_task.fly_time;
            if (total_fly_time > MAX_FLY_TIME_PER_FDP) {
                continue; // Prune
            }

            // Since duty time calculates from first task to last flight task
            auto duty_time = next_task.end_time - current_path.front().start_time;
            if (duty_time > MAX_DUTY_TIME_PER_FDP) {
                continue;
            }
        }


        // Recursion
        current_path.push_back(next_task);
        used_task_ids.insert(next_task.id);
        _dfs_fdp_builder(current_path, used_task_ids, found_fdps);
        current_path.pop_back();
        used_task_ids.erase(next_task.id);
    }
}
