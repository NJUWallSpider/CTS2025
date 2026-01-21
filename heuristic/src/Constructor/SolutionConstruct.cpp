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
    // // Collect all crew IDs with no assigned tasks
    // for (const auto& [crew_id, duty_periods] : solution.crew_dutyperiods) {
    //     // Only select crews with tasks
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

// Ruin and Recreate Optimization
SolutionState SolutionConstructor::ruin_and_recreate(SolutionState& initial_solution, double ruin_percentage) {
    // Create a copy of the solution
    SolutionState current_solution = initial_solution;
    
    // Select crews to ruin
    std::vector<std::string> crews_to_ruin = select_crews_to_ruin(current_solution, ruin_percentage);
    
    // If no crews were selected, return the original solution
    if (crews_to_ruin.empty()) {
        return current_solution;
    }
    
   // std::cout << "Selected " << crews_to_ruin.size() << " crews for ruin and recreate optimization" << std::endl;
    
    // Execute ruin operation
    ruin_solution(current_solution, crews_to_ruin);
    
    // Execute recreate operation
    recreate_solution(current_solution, crews_to_ruin);
    
    // Recalculate solution score
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

// Select crews to ruin
std::vector<std::string> SolutionConstructor::select_crews_to_ruin(const SolutionState& solution, double percentage) {
    std::vector<std::string> all_crew_ids;
    std::vector<std::string> selected_crews;
    
    // // Collect all crew IDs with assigned tasks
    // for (const auto& [crew_id, duty_periods] : solution.crew_dutyperiods) {
    //     // Only select crews with tasks
    //     if (!duty_periods.empty()) {
    //         all_crew_ids.push_back(crew_id);
    //     }
    // }
    
    // // If no crews have tasks, return empty list
    // if (all_crew_ids.empty()) {
    //     return selected_crews;
    // }

    for(const auto& [crew_id, crew] : data_.getCrews()){
        if(crew.qualifications.size() > 0 && crew.groundDuties.size() < 1){
            all_crew_ids.push_back(crew_id);
        }
    }
    
    // Calculate the number of crews to select
    int num_crews_to_select = std::max(1, static_cast<int>(all_crew_ids.size() * percentage));
    
    // Randomly select the specified number of crews
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, all_crew_ids.size() - 1);
    
    // Use set to avoid duplicate selection
    std::set<std::string> selected_set;
    while (selected_set.size() < num_crews_to_select) {
        int random_index = dis(gen);
        selected_set.insert(all_crew_ids[random_index]);
    }
    
    // Convert set to vector
    selected_crews.assign(selected_set.begin(), selected_set.end());

    return selected_crews;
}

// Ruin phase: remove schedules for some crews
void SolutionConstructor::ruin_solution(SolutionState& solution, const std::vector<std::string>& selected_crews) {
    // For each selected crew
    for (const auto& crew_id : selected_crews) {
        // Get all tasks for this crew
        if (solution.crew_dutyperiods.find(crew_id) != solution.crew_dutyperiods.end()) {
            const auto& duty_periods = solution.crew_dutyperiods[crew_id];
            
            // Traverse all duty periods
            for (const auto& duty_period : duty_periods) {
                // Traverse all tasks in the duty period
                for (const auto& task : duty_period.tasks) {
                    // If it is a flight task, remove from flight assignments
                    if (std::holds_alternative<Flight>(task)) {
                        const auto& flight = std::get<Flight>(task);
                        solution.flight_assignments.erase(flight.id);
                    }
                    // Note: Ground duties and bus tasks remain unchanged as they are fixed
                }
            }
            
            // Clear duty periods for this crew
            solution.crew_dutyperiods.erase(crew_id);
            
            // Clear cycles for this crew
            if (solution.crew_cycles.find(crew_id) != solution.crew_cycles.end()) {
                solution.crew_cycles.erase(crew_id);
            }
        }
    }
    
    // std::cout << "Completed ruin phase, removed schedules for selected crews" << std::endl;
}

// Recreate phase: reschedule flights for the selected crews
void SolutionConstructor::recreate_solution(SolutionState& solution, const std::vector<std::string>& selected_crews) {
    const auto& crews = data_.getCrews();
    

    // For each selected crew
    for (const auto& crew_id : selected_crews) {
        // Ensure crew exists in data
        if (crews.find(crew_id) != crews.end()) {
            const auto& crew = crews.at(crew_id);
            
            // Create crew schedule builder
            CrewSchedule crew_schedule_builder(data_, crew, 
                solution.crew_dutyperiods[crew_id], 
                solution.flight_assignments,
                solution.crew_cycles[crew_id]);
            
            // Construct new schedule for this crew
            crew_schedule_builder.construct_schedule_DFS();
        }
    }
    
    // std::cout << "Completed recreate phase, rescheduled flights for selected crews" << std::endl;
}

// Calculate acceptance probability for new solution (Simulated Annealing)
double SolutionConstructor::calculate_acceptance_probability(double delta_energy, double temperature) {
    if (delta_energy <= 0) {
        // If new solution is better, always accept
        return 1.0;
    } else {
        // If new solution is worse, calculate probability based on difference and temperature
        return std::exp(-delta_energy / temperature);
    }
}

// Worker function for multi-threading: process a group of paths
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
    // Create independent random number generator for each thread
    std::random_device rd;
    std::mt19937 local_rng(rd());
    std::vector<std::uniform_real_distribution<double>> random_distributions;
    
    // Create independent random distribution for each path
    for (int i = start_idx; i < end_idx; ++i) {
        random_distributions.emplace_back(0.0, 1.0);
    }
    
    // Start iterative optimization
    for (int iter = 0; iter < max_iterations && !should_terminate; iter++) {
        // Perform one iteration for each path assigned to this thread
        for (int path_offset = 0; path_offset < (end_idx - start_idx); path_offset++) {
            int path_idx = start_idx + path_offset;
            
            // Current solution of the current path
            SolutionState& current_solution = paths[path_idx];
            
            // Current temperature and ruin percentage of the current path
            double& temperature = temperatures[path_idx];
            double& ruin_percentage = ruin_percentages[path_idx];
            
            // Generate new solution
            std::vector<std::string> crews_to_ruin = select_crews_to_ruin(current_solution, ruin_percentage);
            if (!crews_to_ruin.empty()) {
                // Create a copy of the current solution
                SolutionState candidate_solution = current_solution;
                
                // Execute ruin operation
                ruin_solution(candidate_solution, crews_to_ruin);
                
                // Execute recreate operation
                recreate_solution(candidate_solution, crews_to_ruin);
                
                // Recalculate solution score
                candidate_solution.score = 0.0;
                for (const auto& flight : candidate_solution.flight_assignments) {
                    for (const auto& task : flight.second) {
                        if (task.second) {
                            candidate_solution.score += 1;
                            break;
                        }
                    }
                }
                
                // Calculate energy difference (score difference)
                double delta_energy = current_solution.score - candidate_solution.score;
                
                // Decide whether to accept the new solution
                bool accept_new_solution = false;
                if (delta_energy <= 0) {
                    // If new solution is better, always accept
                    accept_new_solution = true;
                } else {
                    // If new solution is worse, decide based on simulated annealing criteria
                    double acceptance_probability = calculate_acceptance_probability(delta_energy, temperature);
                    double random_value = random_distributions[path_offset](local_rng);
                    accept_new_solution = (random_value < acceptance_probability);
                }
                
                // Update current solution
                if (accept_new_solution) {
                    current_solution = candidate_solution;
                    
                    // If new solution is better than global best, update global best
                    {
                        // Use mutex to protect access to global best solution
                        std::lock_guard<std::mutex> lock(global_best_mutex);
                        if (current_solution.score > global_best_solution.score) {
                            global_best_solution = current_solution;
                            std::cout << "Thread " << std::this_thread::get_id() << " Path " << path_idx + 1 
                                      << " Found new global best solution, Score: " << global_best_solution.score 
                                      << ", Iteration: " << global_iteration_counter.load() 
                                      << ", Temperature: " << temperature << std::endl;
                                ReportGenerator::generate_schedule_report(global_best_solution, data_, "heuristic/report/" + data_version_ + "/schedule_report.txt", std::chrono::steady_clock::now());
                                ReportGenerator::generate_submission_csv(global_best_solution, "heuristic/report/" + data_version_ + "/rosterResult.csv");
                                ReportGenerator::validate_crew_flight_consistency(global_best_solution, "heuristic/report/" + data_version_ + "/crew_flight_consistency.txt");

                        }
                    }
                }
            }
            
            // Decrease temperature
            temperature = std::max(min_temperature, temperature * cooling_rate);
            
            // Dynamically adjust ruin percentage
            if (iter % 50 == 0) {
                // Adjust ruin percentage every 50 iterations
                if (temperature > initial_temperature * 0.5) {
                    // When temperature is high, increase ruin percentage to promote exploration
                    ruin_percentage = std::min(0.3, ruin_percentage * 1.1);
                } else {
                    // When temperature is low, decrease ruin percentage to promote local search
                    ruin_percentage = std::max(0.02, ruin_percentage * 0.9);
                }
            }
        }
        
        // Increment global iteration counter
        int current_iteration = ++global_iteration_counter;
        
        // Output progress every 100 global iterations
        if (current_iteration % 100 == 0 && start_idx == 0) {  // Only let the first thread output progress
            std::lock_guard<std::mutex> lock(global_best_mutex);
            std::cout << "Completed Iteration: " << current_iteration << "/" << (max_iterations * paths.size()) 
                      << ", Current Global Best Score: " << global_best_solution.score << std::endl;
        }
    }
}

// Multi-threaded version of Simulated Annealing Multi-path Ruin and Recreate Optimization
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
    // Determine number of threads to use
    if (num_threads <= 0) {
        num_threads = std::thread::hardware_concurrency();
        // If hardware supported threads cannot be determined, default to 4
        if (num_threads == 0) {
            num_threads = 4;
        }
    }
    
    std::cout << "Using " << num_threads << " threads for parallel simulated annealing multi-path optimization" << std::endl;
    std::cout << "Creating " << num_paths << " exploration paths" << std::endl;
    
    // Create multiple exploration paths, each starting from the initial solution
    std::vector<SolutionState> paths(num_paths, initial_solution);
    std::vector<double> temperatures(num_paths, initial_temperature);
    std::vector<double> ruin_percentages(num_paths, initial_ruin_percentage);
    
    // Record global best solution
    SolutionState global_best_solution = initial_solution;
    
    // Create mutex to protect access to global best solution
    std::mutex global_best_mutex;
    
    // Create atomic boolean to notify all threads to terminate
    std::atomic<bool> should_terminate(false);
    
    // Create atomic integer to track global iteration count
    std::atomic<int> global_iteration_counter(0);
    
    // Create thread pool
    std::vector<std::thread> threads;
    
    // Calculate number of paths processed by each thread
    int paths_per_thread = num_paths / num_threads;
    int remaining_paths = num_paths % num_threads;
    
    // Start threads
    int start_idx = 0;
    for (int t = 0; t < num_threads; ++t) {
        // Calculate path range for this thread
        int end_idx = start_idx + paths_per_thread + (t < remaining_paths ? 1 : 0);
        
        // Create and start thread
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
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Final path crossover: Find the best path
    double best_path_score = paths[0].score;
    int best_path_idx = 0;
    
    for (int i = 1; i < num_paths; i++) {
        if (paths[i].score > best_path_score) {
            best_path_score = paths[i].score;
            best_path_idx = i;
        }
    }
    
    // If the best path is better than the global best solution, update global best
    if (best_path_score > global_best_solution.score) {
        global_best_solution = paths[best_path_idx];
    }
    
    std::cout << "Parallel simulated annealing multi-path optimization completed" << std::endl;
    std::cout << "Global Best Solution Score: " << global_best_solution.score << std::endl;
    
    return global_best_solution;
}

// Simulated Annealing Multi-path Ruin and Recreate Optimization (Single-threaded version)
SolutionState SolutionConstructor::simulated_annealing_ruin_recreate(
    SolutionState& initial_solution, 
    int num_paths,
    int max_iterations,
    double initial_temperature,
    double cooling_rate,
    double min_temperature,
    double initial_ruin_percentage
) {
    // Create multiple exploration paths, each starting from the initial solution
    std::vector<SolutionState> paths(num_paths, initial_solution);
    std::vector<double> temperatures(num_paths, initial_temperature);
    std::vector<double> ruin_percentages(num_paths, initial_ruin_percentage);
    
    // Record global best solution
    SolutionState global_best_solution = initial_solution;
    
    // Create independent random number generator for each path
    std::vector<std::uniform_real_distribution<double>> random_distributions(num_paths, std::uniform_real_distribution<double>(0.0, 1.0));
    
    // Start iterative optimization
    for (int iter = 0; iter < max_iterations; iter++) {
        // Perform one iteration for each path
        for (int path_idx = 0; path_idx < num_paths; path_idx++) {
            // Current solution of the current path
            SolutionState& current_solution = paths[path_idx];
            
            // Current temperature and ruin percentage of the current path
            double& temperature = temperatures[path_idx];
            double& ruin_percentage = ruin_percentages[path_idx];
            
            // Generate new solution
            std::vector<std::string> crews_to_ruin = select_crews_to_ruin(current_solution, ruin_percentage);
            if (!crews_to_ruin.empty()) {
                // Create a copy of the current solution
                SolutionState candidate_solution = current_solution;
                
                // Execute ruin operation
                ruin_solution(candidate_solution, crews_to_ruin);
                
                // Execute recreate operation
                recreate_solution(candidate_solution, crews_to_ruin);
                
                // Recalculate solution score
                candidate_solution.score = 0.0;
                for (const auto& flight : candidate_solution.flight_assignments) {
                    for (const auto& task : flight.second) {
                        if (task.second) {
                            candidate_solution.score += 1;
                            break;
                        }
                    }
                }
                
                // Calculate energy difference (score difference)
                double delta_energy = current_solution.score - candidate_solution.score;
                
                // Decide whether to accept the new solution
                bool accept_new_solution = false;
                if (delta_energy <= 0) {
                    // If new solution is better, always accept
                    accept_new_solution = true;
                } else {
                    // If new solution is worse, decide based on simulated annealing criteria
                    double acceptance_probability = calculate_acceptance_probability(delta_energy, temperature);
                    double random_value = random_distributions[path_idx](rng_);
                    accept_new_solution = (random_value < acceptance_probability);
                }
                
                // Update current solution
                if (accept_new_solution) {
                    current_solution = candidate_solution;
                    
                    // If new solution is better than global best, update global best
                    if (current_solution.score > global_best_solution.score) {
                        global_best_solution = current_solution;
                        std::cout << "Path " << path_idx + 1 << " Found new global best solution, Score: " 
                                  << global_best_solution.score << ", Iteration: " << iter + 1 
                                  << ", Temperature: " << temperature << std::endl;
                    }
                }
            }
            
            // Decrease temperature
            temperature = std::max(min_temperature, temperature * cooling_rate);
            
            // Dynamically adjust ruin percentage
            if (iter % 50 == 0) {
                // Adjust ruin percentage every 50 iterations
                if (temperature > initial_temperature * 0.5) {
                    // When temperature is high, increase ruin percentage to promote exploration
                    ruin_percentage = std::min(0.3, ruin_percentage * 1.1);
                } else {
                    // When temperature is low, decrease ruin percentage to promote local search
                    ruin_percentage = std::max(0.02, ruin_percentage * 0.9);
                }
            }
        }
        
        // Output progress every 100 iterations
        if ((iter + 1) % 100 == 0) {
            std::cout << "Completed Iteration: " << iter + 1 << "/" << max_iterations 
                      << ", Current Global Best Score: " << global_best_solution.score << std::endl;
        }
        
        // Perform path crossover every 200 iterations (inject global best into worst path)
        if ((iter + 1) % 200 == 0 && iter > 0) {
            // Find the worst performing path
            int worst_path_idx = 0;
            double worst_score = paths[0].score;
            
            for (int i = 1; i < num_paths; i++) {
                if (paths[i].score < worst_score) {
                    worst_score = paths[i].score;
                    worst_path_idx = i;
                }
            }
            
            // Inject global best solution into the worst performing path
            paths[worst_path_idx] = global_best_solution;
            // Reset temperature and ruin percentage for this path to allow more exploration
            temperatures[worst_path_idx] = initial_temperature * 0.5;
            ruin_percentages[worst_path_idx] = initial_ruin_percentage;
            
            std::cout << "Path Crossover: Injecting global best solution into path " << worst_path_idx + 1 << std::endl;
        }
    }
    
    return global_best_solution;
}
