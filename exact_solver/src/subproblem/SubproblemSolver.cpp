#include "SubproblemSolver.h"
#include "../gurobi_solvers/MasterProblem.h"
#include <algorithm>
#include <limits>
#include <map>
#include <queue>
#include <chrono>
#include <iostream>
#include <thread>
#include <mutex>
#include <future>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <random> // Added for random number generation

namespace fs = std::filesystem;

SubproblemSolver::SubproblemSolver(const SchedulingData& data, const MasterProblem& master, std::string fdp_path, 
                                 double non_base_rejection_prob, int beam_width) 
    : data_(data), master_(master), reduced_cost_(0.0), 
      non_base_rejection_prob_(non_base_rejection_prob), beam_width_(beam_width) {
    // Pre-cache dual values for all flights during initialization
    network_directory_ = fdp_path;
    updateDuals();
    
    // Attempt to load saved FDP networks
    // loadAllFDPNetworks(network_directory_);
}

void SubproblemSolver::updateDuals() {
    flight_duals_cache_.clear();
    for (const auto& flight : data_.get_all_flights()) {
        flight_duals_cache_[flight.first] = master_.getFlightDual(flight.first);
    }
}

void SubproblemSolver::clearCrewSpecificCache() {
    crew_valid_fdps_cache_.clear();
    fdp_flight_ids_cache_.clear();
    connectivity_cache_.clear();
}

void SubproblemSolver::clearCache() {
    clearCrewSpecificCache();
    flight_duals_cache_.clear();
}

bool SubproblemSolver::solveForCrew(const std::string& crew_id) {
    // Use cached dual values
    updateDuals();
    return solveForCrewWithDuals(crew_id, flight_duals_cache_);
}

bool SubproblemSolver::solveForCrewWithDuals(const std::string& crew_id, 
                                           const std::unordered_map<std::string, double>& flight_duals) {
    // Get crew dual value (opportunity cost)
    double crew_dual = master_.getCrewDual(crew_id);
    
    // Filter valid duty periods executable by this crew (use cache)
    std::vector<FDP> valid_fdps = filterValidFDPs(crew_id);

    
    // Solve longest path problem, get optimal flight cycle
    std::vector<FDP> best_path = solveLongestPath(crew_id, valid_fdps, crew_dual);
    
    // If no valuable path found
    if (best_path.empty()) {
        return false;
    }
    
    // If only one FDP, use it directly
    if (best_path.size() == 1) {
        best_fdp_ = best_path[0];
    } else {
        // If multiple FDPs, need to combine them into one flight cycle
        FDP combined_fdp;
        for (const auto& fdp : best_path) {
            combined_fdp.tasks.insert(combined_fdp.tasks.end(), fdp.tasks.begin(), fdp.tasks.end());
        }
        best_fdp_ = combined_fdp;
    }
    
    // Calculate final reduced cost (profitability)
    reduced_cost_ = 0.0;
    for (const auto& fdp : best_path) {
        reduced_cost_ += calculateFDPReward(fdp, flight_duals);
    }
    reduced_cost_ -= crew_dual;

    // Output solve info
    std::cout << "Crew " << crew_id << " subproblem solved, result: " << reduced_cost_ << std::endl;
    
    return reduced_cost_ > 0.0;
}


const FDP& SubproblemSolver::getBestFDP() const {
    return best_fdp_;
}

double SubproblemSolver::getReducedCost() const {
    return reduced_cost_;
}

std::vector<FDP> SubproblemSolver::filterValidFDPs(const std::string& crew_id) {
    
    std::vector<FDP> valid_fdps;
    
    // Get crew info
    const Crew* crew = data_.get_crew(crew_id);
    if (!crew) {
        std::cerr << "Crew not found: " << crew_id << std::endl;
        return valid_fdps;
    }
    
    // Get crew base
    std::string base = crew->base;
    
    // Get crew qualifications (executable flights)
    const auto& qualified_flights = crew->qualified_flights;
    
    // Get crew ground duties (placeholder tasks)
    const auto& ground_duties = crew->ground_duties;
    
    // Create random number generator
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);
    
    // Iterate all possible duty periods, filter those executable by this crew
    for (const auto& [key, fdps] : data_.all_valid_fdps_) {
        const auto& [airport, date] = key;
        
        for (const auto& fdp : fdps) {
            bool is_valid = true;
            
            // Check if crew is qualified for all flights in FDP
            for (const auto& task : fdp.tasks) {
                if (task.task_type == "flight") {
                    if (qualified_flights.find(task.id) == qualified_flights.end()) {
                        is_valid = false;
                        break;
                    }
                }
            }
            
            // Check if FDP ends at non-base airport, if so, reject based on probability
            if (fdp.get_end_airport() != base) {
                // Generate a random number, if less than rejection probability, reject the FDP
                if (dis(gen) < non_base_rejection_prob_) {
                    is_valid = false;
                }
            }
            
            // Check ground duty constraints
            const auto fdp_start = fdp.get_start_time();
            const auto fdp_end = fdp.get_end_time();
            
            for (size_t i = 0; i < ground_duties.size(); ++i) {
                const auto& duty = ground_duties[i];
                
                // Check if ground duty overlaps with FDP time
                if (duty.start_time < fdp_end && duty.end_time > fdp_start) {
                    // Case 1: Ground duty overlaps with FDP time
                    
                    // Check if it overlaps with any task in FDP
                    for (const auto& task : fdp.tasks) {
                        if (task.start_time < duty.end_time && task.end_time > duty.start_time) {
                            is_valid = false;
                            break;
                        }
                    }
                    
                    if (!is_valid) break;
                    
                    // Find FDP tasks before and after ground duty
                    const Task* prev_task = nullptr;
                    const Task* next_task = nullptr;
                    
                    for (size_t j = 0; j < fdp.tasks.size(); ++j) {
                        if (fdp.tasks[j].end_time <= duty.start_time) {
                            prev_task = &fdp.tasks[j];
                        }
                        if (fdp.tasks[j].start_time >= duty.end_time) {
                            next_task = &fdp.tasks[j];
                            break;
                        }
                    }
                    
                    // Check if airports before/after task are Base
                    if ((prev_task && prev_task->end_airport != base) || 
                        (next_task && next_task->start_airport != base)) {
                        is_valid = false;
                        break;
                    }
                } else {
                    // Case 2: Ground duty outside FDP time range
                    
                    // Check if within 12 hours before FDP starts
                    auto hours_before_fdp = std::chrono::duration_cast<std::chrono::hours>(
                        fdp_start - duty.end_time).count();
                    
                    if (hours_before_fdp >= 0 && hours_before_fdp <= 12 && duty.is_duty) {
                        is_valid = false;
                        break;
                    }
                    
                    // Check if within 12 hours after FDP ends
                    auto hours_after_fdp = std::chrono::duration_cast<std::chrono::hours>(
                        duty.start_time - fdp_end).count();
                    
                    if (hours_after_fdp >= 0 && hours_after_fdp <= 12) {
                        if (fdp.get_end_airport() != base) {
                            is_valid = false;
                            break;
                        }
                    }
                }
            }
            
            
            if (is_valid) {
                valid_fdps.push_back(fdp);
            }
        }
    }
    
    return valid_fdps;
}

std::unordered_set<std::string> SubproblemSolver::getCachedFlightIds(const FDP& fdp) const {
    // Use FDP task list as key
    std::string fdp_key;
    for (const auto& task : fdp.tasks) {
        fdp_key += task.id + "|";
    }
    
    auto it = fdp_flight_ids_cache_.find(fdp_key);
    if (it != fdp_flight_ids_cache_.end()) {
        return it->second;
    }
    
    // Since it's a const method, cannot modify cache, return calculation result directly
    return fdp.get_included_flight_ids();
}

double SubproblemSolver::calculateFDPReward(const FDP& fdp, const std::unordered_map<std::string, double>& flight_duals) const {
    double reward = 0.0;
    
    // Use cache to get flight IDs
    auto flight_ids = getCachedFlightIds(fdp);
    
    // Accumulate dual values of all flights in FDP
    for (const auto& flight_id : flight_ids) {
        auto it = flight_duals.find(flight_id);
        if (it != flight_duals.end()) {
            reward += -it->second;
        }
    }
    
    return reward;
}

std::vector<FDP> SubproblemSolver::solveLongestPath(const std::string& crew_id, 
                                                  const std::vector<FDP>& valid_fdps,
                                                  double crew_dual) {
    // If no valid FDPs, return empty result directly
    if (valid_fdps.empty()) {
        return {};
    }
    
    // Try to load network from file
    FDPNetwork network;
    std::string file_path = getNetworkFilePath(crew_id);
    
    bool network_loaded = false;
    if (fs::exists(file_path)) {
        network_loaded = deserializeFDPNetwork(crew_id, file_path, network);
    }
    
    // If load fails, build new network and save
    if (!network_loaded) {
        network = buildFDPNetwork(crew_id, valid_fdps);
        serializeFDPNetwork(crew_id, network, file_path);
    }
    
    // Update rewards in network
    updateNetworkRewards(network);
    
    // Solve longest path using network
    auto result = solveLongestPathWithNetwork(network, crew_dual, crew_id);
    
    // Explicitly release memory after use
    network = FDPNetwork(); // Clear network data
    
    return result;
}

FDPNetwork SubproblemSolver::buildFDPNetwork(const std::string& crew_id, const std::vector<FDP>& valid_fdps) {
    
    FDPNetwork network;
    
    // Sort FDPs by start time
    network.sorted_fdps = valid_fdps;
    std::sort(network.sorted_fdps.begin(), network.sorted_fdps.end(), 
              [](const FDP& a, const FDP& b) {
                  return a.get_start_time() < b.get_start_time();
              });
    
    for (int i = 0; i < network.sorted_fdps.size(); ++i) {
        network.sorted_fdps[i].id = i;
    }
    
    // Calculate reward for each FDP
    size_t n = network.sorted_fdps.size();
    network.rewards.resize(n);
    for (size_t i = 0; i < n; ++i) {
        network.rewards[i] = calculateFDPReward(network.sorted_fdps[i], flight_duals_cache_);
    }
    
    // Get crew info
    const Crew* crew = data_.get_crew(crew_id);
    if (!crew) {
        std::cerr << "Error: Crew " << crew_id << " not found" << std::endl;
        return network;
    }
    std::string base = crew->base;
    std::string initialStation = crew->initial_station;
    
    // Collect all unique (airport, timepoint) nodes
    std::unordered_map<NetworkNode, size_t, NetworkNodeHash> node_map;
    
    // Add source and sink nodes
    NetworkNode source_node = {"SOURCE", time_point()};
    NetworkNode sink_node = {"SINK", time_point()};
    
    // Add source and sink to node collection
    node_map[source_node] = 0;
    node_map[sink_node] = 1;
    network.nodes.push_back(source_node);
    network.nodes.push_back(sink_node);
    
    // Create nodes for start and end of each FDP
    for (size_t i = 0; i < n; ++i) {
        const FDP& fdp = network.sorted_fdps[i];
        
        // Create start node
        NetworkNode start_node = {fdp.get_start_airport(), fdp.get_start_time()};
        if (node_map.find(start_node) == node_map.end()) {
            node_map[start_node] = network.nodes.size();
            network.nodes.push_back(start_node);
        }
        
        // Create end node
        NetworkNode end_node = {fdp.get_end_airport(), fdp.get_end_time()};
        if (node_map.find(end_node) == node_map.end()) {
            node_map[end_node] = network.nodes.size();
            network.nodes.push_back(end_node);
        }

        // Create mapping from node to FDP
        network.node_to_fdp_start[start_node].push_back(i);
        network.node_to_fdp_end[end_node].push_back(i);
    }
    
    // Initialize graph structure
    network.source = 0;
    network.sink = 1;
    network.graph.resize(network.nodes.size());
    
    // Create edge for each FDP
    for (int i = 0; i < n; ++i) {
        const FDP& fdp = network.sorted_fdps[i];
        size_t start_node_idx = node_map[{fdp.get_start_airport(), fdp.get_start_time()}];
        size_t end_node_idx = node_map[{fdp.get_end_airport(), fdp.get_end_time()}];
        
        // Create edge from start to end
        EdgeInfo edge_info;
        edge_info.best_fdp_idx = static_cast<int>(i);
        edge_info.reward = network.rewards[i];
        
        // Add new edge
        network.graph[start_node_idx].push_back({end_node_idx, edge_info});
        
        // Record mapping from FDP to edge for fast update
        network.fdp_to_edge[i] = {start_node_idx, end_node_idx};
    }
    
    // Add edges from source to eligible start nodes
    for (size_t i = 2; i < network.nodes.size(); ++i) { // Skip source and sink
        const auto& node = network.nodes[i];
        std::string airport = node.airport;
        time_point time = node.time;
        
        // Check if any FDP starts at this node
        bool is_start_node = false;
        auto it = network.node_to_fdp_start.find(node);
        if (it != network.node_to_fdp_start.end()) {
            is_start_node = true;
        }
        
        if (!is_start_node) continue; // Skip if no FDP starts at this node
        
        bool canConnectFromSource = false;
        
        // Check if there is ground duty before this time point
        bool has_duty_before = false;
        for (const auto& duty : crew->ground_duties) {
            if (duty.end_time <= time) {
                has_duty_before = true;
                break;
            }
        }
        
        if (has_duty_before) {
            // If ground duty exists before, start must be base
            if (airport == base) {
                canConnectFromSource = true;
            }
        } else {
            // If no ground duty before, start must be initialStation
            if (airport == initialStation) {
                canConnectFromSource = true;
            }
        }
        
        if (canConnectFromSource) {
            // Create edge from source to start node
            EdgeInfo edge_info;
            edge_info.best_fdp_idx = -1; // No FDP corresponds to source -> start
            edge_info.reward = 0.0;
            
            network.graph[network.source].push_back({i, edge_info});
        }
    }
    
    // Add edges from eligible end nodes to sink
    for (size_t i = 2; i < network.nodes.size(); ++i) { // Skip source and sink
        const auto& node = network.nodes[i];
        std::string airport = node.airport;
        time_point time = node.time;
        
        // Check if any FDP ends at this node
        bool is_end_node = false;
        auto it = network.node_to_fdp_end.find(node);
        if (it != network.node_to_fdp_end.end()) {
            is_end_node = true;
        }
        
        if (!is_end_node) continue; // Skip if no FDP ends at this node
        
        bool canConnectToSink = false;
        
        // Check if there is ground duty after this time point
        bool has_duty_after = false;
        for (const auto& duty : crew->ground_duties) {
            if (duty.start_time >= time) {
                has_duty_after = true;
                break;
            }
        }
        
        if (has_duty_after) {
            // If ground duty exists after, end must be base
            if (airport == base) {
                canConnectToSink = true;
            }
        } else {
            // If no ground duty after, any end can connect to sink
            canConnectToSink = true;
        }
        
        if (canConnectToSink) {
            // Create edge from end node to sink
            EdgeInfo edge_info;
            edge_info.best_fdp_idx = -1; // No FDP corresponds to end -> sink
            edge_info.reward = 0.0;
            
            network.graph[i].push_back({network.sink, edge_info});
        }
    }
    
    return network;
}

void SubproblemSolver::updateNetworkRewards(FDPNetwork& network) {
    // Update reward for each FDP
    for (size_t i = 0; i < network.sorted_fdps.size(); ++i) {
        network.rewards[i] = calculateFDPReward(network.sorted_fdps[i], flight_duals_cache_);
        
        // Update all edges using this FDP
        int fdp_idx = static_cast<int>(i);
        auto pair = network.fdp_to_edge[fdp_idx];
        size_t from_node = pair.first;
        size_t to_node = pair.second;
                
        // Find and update edge reward
        for (auto& edge : network.graph[from_node]) {
            if (edge.first == to_node && edge.second.reward < network.rewards[fdp_idx]) {
                edge.second.best_fdp_idx = fdp_idx;
                edge.second.reward = network.rewards[fdp_idx];
                break;
            }
        }
    }
}

// Helper function: Calculate calendar days between two time points (inclusive)
auto calculateCalendarDays = [](time_point start, time_point end) {
    auto start_day = std::chrono::floor<std::chrono::days>(start);
    auto end_day = std::chrono::floor<std::chrono::days>(end);
    return (end_day - start_day).count() + 1;
};

// Label structure for recording path and resource consumption
struct Label {
    std::vector<int> path;
    double reward = 0.0;
    std::chrono::minutes flight_time{0};
    time_point start_time;
    time_point end_time;

    // Heuristic evaluation function: reward per unit time
    double get_profitability_metric() const {
        if (path.empty() || reward <= 0) {
            return std::numeric_limits<double>::lowest();
        }
        long calendar_duration_days = calculateCalendarDays(start_time, end_time);
        // Add 2 days mandatory rest time to calculate total occupied time
        return reward / (calendar_duration_days + 2.0);
    }

    // For sorting
    bool operator<(const Label& other) const {
        return get_profitability_metric() < other.get_profitability_metric();
    }
};

//==============================================================================
// New Implementation: Beam + DP combination for optimal cycle sequence
//==============================================================================
std::vector<FDP> SubproblemSolver::solveLongestPathWithNetwork(
        const FDPNetwork& network,
        double           crew_dual,
        std::string      crew_id) {

    if (network.sorted_fdps.empty()) return {};

    // ---------- 1. Generate "Candidate Flight Cycles" --------------------------------------
    struct CycleInfo {
        std::vector<int> fdps;           // FDP index sequence
        time_point       start_day_tp;   // Date 00:00
        time_point       end_day_tp;     // Date 23:59
        std::chrono::minutes fly_minutes{};
        double           reward  = 0.0;
        bool             can_reach_sink = false;  // Can reach sink (end at base airport)
        
        // Record cycle start/end airports for cycle-level connection check
        std::string      start_airport;
        std::string      end_airport;
        
        // Add comparison operator for sorting
        bool operator>(const CycleInfo& other) const {
            return reward > other.reward;
        }
        
        // Add comparison operator for priority queue (min heap, smaller reward at top)
        bool operator<(const CycleInfo& other) const {
            return reward < other.reward;
        }
    };
    std::vector<CycleInfo> cycles;
    const auto MAX_FLY   = std::chrono::hours(60);
    const int  MAX_DAY   = 4;                     // ≤4 calendar days

    // Get crew info
    const Crew* crew = data_.get_crew(crew_id);
    if (!crew) {
        std::cerr << "Error: Crew " << crew_id << " not found" << std::endl;
        return {};
    }
    const auto& ground_duties = crew->ground_duties;
    std::string base = crew->base;

    auto day_floor = [](const time_point& tp){
        return std::chrono::floor<std::chrono::days>(tp);
    };

    // Helper function to calculate full calendar days
    auto calculate_calendar_days = [](time_point start, time_point end) {
        auto start_day = std::chrono::floor<std::chrono::days>(start);
        auto end_day = std::chrono::floor<std::chrono::days>(end);
        return std::chrono::duration_cast<std::chrono::days>(end_day - start_day).count() -1;
    };

    // Get duties before and after FDP
    auto get_duties_before_fdp = [&ground_duties](const FDP& fdp) -> std::vector<const GroundDuty*> {
        std::vector<const GroundDuty*> duties_before;
        for (const auto& duty : ground_duties) {
            if (duty.end_time <= fdp.get_start_time()) {
                duties_before.push_back(&duty);
            }
        }
        // Sort descending by end time, so closest duty is first
        std::sort(duties_before.begin(), duties_before.end(), 
            [](const GroundDuty* a, const GroundDuty* b) {
                return a->end_time > b->end_time;
            });
        return duties_before;
    };

    auto get_duties_after_fdp = [&ground_duties](const FDP& fdp) -> std::vector<const GroundDuty*> {
        std::vector<const GroundDuty*> duties_after;
        for (const auto& duty : ground_duties) {
            if (duty.start_time >= fdp.get_end_time()) {
                duties_after.push_back(&duty);
            }
        }
        // Sort ascending by start time, so closest duty is first
        std::sort(duties_after.begin(), duties_after.end(), 
            [](const GroundDuty* a, const GroundDuty* b) {
                return a->start_time < b->start_time;
            });
        return duties_after;
    };

    // Preprocessing: Build time index for each airport
    std::unordered_map<std::string, std::map<time_point, size_t>> airport_time_index;
    for (size_t i = 2; i < network.nodes.size(); ++i) {
        const auto& node = network.nodes[i];
        if (i != network.source && i != network.sink) {
            airport_time_index[node.airport][node.time] = i;
        }
    }

    // Preprocessing: Cache whether each node can reach sink
    std::vector<bool> can_reach_sink(network.nodes.size(), false);
    for (size_t i = 0; i < network.nodes.size(); ++i) {
        for (const auto& edge : network.graph[i]) {
            if (edge.first == network.sink) {
                can_reach_sink[i] = true;
                break;
            }
        }
    }

    // Generate candidate cycles using new network structure
    // All paths starting from source
    for (const auto& source_edge : network.graph[network.source]) {
        size_t start_node_idx = source_edge.first;
        
        // Use Beam Search to find optimal paths from each start node
        std::vector<CycleInfo> beam;
        
        // Initialize beam search start
        for (const auto& first_edge : network.graph[start_node_idx]) {
            size_t next_node_idx = first_edge.first;
            int fdp_idx = first_edge.second.best_fdp_idx;
            if (fdp_idx == -1) continue;
            const FDP& first_fdp = network.sorted_fdps[fdp_idx];
            
            CycleInfo base_cycle;
            base_cycle.fdps = {fdp_idx};
            base_cycle.fly_minutes = first_fdp.get_flight_hours();
            base_cycle.reward = first_edge.second.reward;
            base_cycle.start_airport = first_fdp.get_start_airport();
            base_cycle.end_airport   = first_fdp.get_end_airport();
        
            // Check if closely connected ground duty exists before first FDP
            auto duties_before = get_duties_before_fdp(first_fdp);
            if (!duties_before.empty()) {
                const GroundDuty* closest_duty = duties_before[0];
                int calendar_days = calculate_calendar_days(closest_duty->end_time, first_fdp.get_start_time());
                if (calendar_days < 2) {
                    auto farthest_duty = duties_before.back();
                    base_cycle.start_day_tp = day_floor(farthest_duty->start_time);
                } else {
                    base_cycle.start_day_tp = day_floor(first_fdp.get_start_time());
                }
            } else {
                base_cycle.start_day_tp = day_floor(first_fdp.get_start_time());
            }
            
            // Check if closely connected ground duty exists after first FDP
            auto duties_after = get_duties_after_fdp(first_fdp);
            if (!duties_after.empty()) {
                const GroundDuty* closest_duty = duties_after[0];
                int calendar_days = calculate_calendar_days(first_fdp.get_end_time(), closest_duty->start_time);
                if (calendar_days < 2) {
                    auto farthest_duty = duties_after.back();
                    base_cycle.end_day_tp = day_floor(farthest_duty->end_time);
                } else {
                    base_cycle.end_day_tp = day_floor(first_fdp.get_end_time());
                }
            } else {
                base_cycle.end_day_tp = day_floor(first_fdp.get_end_time());
            }

            // Constraint 1: Cumulative flight time ≤ 60h
            if (base_cycle.fly_minutes > MAX_FLY) continue;

            // Constraint 2: Cycle span ≤ 4 days
            auto span = std::chrono::duration_cast<std::chrono::days>(base_cycle.end_day_tp - base_cycle.start_day_tp).count() + 1;
            if (span > MAX_DAY) continue;
            
            // Check first FDP
            if (can_reach_sink[next_node_idx]) {
                base_cycle.can_reach_sink = true;
            } 
            
            beam.push_back(std::move(base_cycle));
        }

        // Continue Beam Search process
        for (;;) {
            auto compare_cycles = [](const CycleInfo& a, const CycleInfo& b) {
                return a.reward > b.reward;
            };
            std::priority_queue<CycleInfo, std::vector<CycleInfo>, decltype(compare_cycles)> next_beam_pq(compare_cycles);
            
            for (const auto& cycle : beam) {
                // Get last FDP of current cycle
                int last_fdp_idx = cycle.fdps.back();
                const FDP& last_fdp = network.sorted_fdps[last_fdp_idx];
                
                // Find end node corresponding to last FDP
                auto pair_it = network.fdp_to_edge.find(last_fdp_idx);
                if (pair_it == network.fdp_to_edge.end()) {
                    continue;
                }
                auto pair = pair_it->second;
                size_t last_node_idx = pair.second;
                const NetworkNode& last_node = network.nodes[last_node_idx];
                
                // Use airport index to quickly find next possible node
                auto it_airport = airport_time_index.find(last_node.airport);
                if (it_airport == airport_time_index.end()) continue;
                
                // Find first node satisfying minimum rest time
                auto min_next_time = last_node.time + std::chrono::hours(12);
                auto it_time = it_airport->second.lower_bound(min_next_time);
                auto max_next_time = last_node.time + std::chrono::hours(48);
                while (it_time != it_airport->second.end() && it_time->first < max_next_time) {
                    size_t next_node_idx = it_time->second;
                    const NetworkNode& next_node = network.nodes[next_node_idx];
                    
                    // Check if any FDP starts at this node
                    auto it_start = network.node_to_fdp_start.find(next_node);
                    if (it_start == network.node_to_fdp_start.end() || it_start->second.empty()) {
                        ++it_time;
                        continue;
                    }
                    
                    // Check for ground duty between two nodes
                    bool is_valid = true;
                    for (const auto& duty : ground_duties) {
                        if (duty.start_time >= last_node.time && duty.end_time <= next_node.time) {
                            if (last_node.airport != base) {
                                is_valid = false;
                                break;
                            }
                        }
                    }
                    
                    if (!is_valid) {
                        break;
                    }
                    
                    // Collect all edges (without checking constraints first)
                    std::vector<std::pair<int, double>> all_edges;
                    all_edges.reserve(network.graph[next_node_idx].size());
                    
                    for (const auto& edge : network.graph[next_node_idx]) {
                        if (edge.first == network.sink) continue;
                        all_edges.emplace_back(edge.second.best_fdp_idx, edge.second.reward);
                    }
                    
                    // Sort edges by reward descending
                    std::sort(all_edges.begin(), all_edges.end(),
                             [](const auto& a, const auto& b) {
                                 return a.second > b.second;
                             });
                    
                    // Iterate sorted edges, count valid edges
                    int valid_edge_count = 0;
                    for (const auto& [fdp_idx, next_reward] : all_edges) {
                        // If enough valid edges found, break loop
                        if (valid_edge_count >= beam_width_) break;
                        
                        const FDP& next_fdp = network.sorted_fdps[fdp_idx];
                        
                        // Check constraints
                        // 1. Cumulative flight time constraint
                        auto total_fly_minutes = cycle.fly_minutes + next_fdp.get_flight_hours();
                        if (total_fly_minutes > MAX_FLY) continue;
                        
                        // 2. Calculate end date
                        time_point end_day_tp;
                        auto duties_after = get_duties_after_fdp(next_fdp);
                        if (!duties_after.empty()) {
                            const GroundDuty* closest_duty = duties_after[0];
                            int calendar_days = calculate_calendar_days(next_fdp.get_end_time(), closest_duty->start_time);
                            if (calendar_days < 2) {
                                auto farthest_duty = duties_after.back();
                                end_day_tp = day_floor(farthest_duty->end_time);
                            } else {
                                end_day_tp = day_floor(next_fdp.get_end_time());
                            }
                        } else {
                            end_day_tp = day_floor(next_fdp.get_end_time());
                        }
                        
                        // 3. Cycle span constraint
                        auto span = std::chrono::duration_cast<std::chrono::days>(
                            end_day_tp - cycle.start_day_tp).count() + 1;
                        if (span > MAX_DAY) continue;
                        
                        // Constraints satisfied, create new cycle
                        CycleInfo next_cycle = cycle;
                        next_cycle.fdps.push_back(fdp_idx);
                        next_cycle.fly_minutes += next_fdp.get_flight_hours();
                        next_cycle.reward += next_reward;
                        next_cycle.end_airport = next_fdp.get_end_airport();
                        next_cycle.end_day_tp = end_day_tp;
                        
                        // Use pre-computed can_reach_sink
                        if (can_reach_sink[next_node_idx]) {
                            next_cycle.can_reach_sink = true;
                        }
                        
                        // Priority queue mechanism
                        // If queue not full, push
                        if (next_beam_pq.size() < static_cast<size_t>(beam_width_)) {
                            next_beam_pq.push(std::move(next_cycle));
                        } 
                        // If queue full, but current better than worst in queue, replace
                        else if (next_cycle.reward > next_beam_pq.top().reward) {
                            next_beam_pq.pop(); // Remove worst
                            next_beam_pq.push(std::move(next_cycle)); // Push better one
                        }
                        // Else discard
                        
                        // Increment count
                        valid_edge_count++;
                    }
                    
                    ++it_time;
                }
            }
            
            if (next_beam_pq.empty()) break;
            
            // Transfer from PQ to beam
            beam.clear();
            while (!next_beam_pq.empty()) {
                beam.push_back(std::move(const_cast<CycleInfo&>(next_beam_pq.top())));
                next_beam_pq.pop();
            }
        }
        
        // Add all candidate cycles from this start to total collection
        for (auto& cycle : beam) {
            cycles.push_back(std::move(cycle));
        }
    }

    if (cycles.empty()) return {};

    std::vector<CycleInfo> valid_cycles;
    // Check if cycle starts from source
    for (const auto& cycle : cycles) {
        bool is_valid = true;
        auto [start_node_idx, a] = network.fdp_to_edge.at(cycle.fdps[0]);
        for (const auto& edge : network.graph[start_node_idx]) {
            if (edge.first == start_node_idx) {
                is_valid = false;
                break;
            }
        }
        auto [b, end_node_idx] = network.fdp_to_edge.at(cycle.fdps.back());
        if (!can_reach_sink[end_node_idx]) {
            is_valid = false;
        }
        if (is_valid) {
            valid_cycles.push_back(cycle);
        }
    }

    // Select optimal cycle
    int best_idx = -1;
    double best_reward = -1;
    for (int i = 0; i < valid_cycles.size(); ++i) {
        if (valid_cycles[i].reward > best_reward) {
            best_reward = valid_cycles[i].reward;
            best_idx = i;
        }
    }
    if (best_idx == -1) return {};
    std::vector<FDP> chosen_cycle;
    for (int fdp_idx : valid_cycles[best_idx].fdps) {
        chosen_cycle.push_back(network.sorted_fdps[fdp_idx]);
    }
    return chosen_cycle;
    
    // Check if cycle can reach sink

    // // ====================== Optimized Cycle-level DAG Longest Path =====================
    // const double NEG_INF = -1e100;
    // int n_cycles = static_cast<int>(cycles.size());
    // std::vector<double> dist(n_cycles, NEG_INF);
    // std::vector<int>    prev_idx(n_cycles, -1);

    // // Topological sort: sort by start date
    // std::vector<int> order(n_cycles);
    // std::iota(order.begin(), order.end(), 0);
    // std::sort(order.begin(), order.end(), [&](int a, int b){
    //     return cycles[a].start_day_tp < cycles[b].start_day_tp;
    // });

    // // Initialize: All cycles can be first cycle
    // for (int idx = 0; idx < n_cycles; ++idx) {
    //     dist[idx] = cycles[idx].reward;
    // }

    // // Use airport-based index for fast lookup (Key Optimization)
    // std::unordered_map<std::string, std::multimap<time_point, int>> airport_start_map;

    // for (int idx : order) {
    //     if (dist[idx] <= NEG_INF/2) continue;
        
    //     auto& cur_cycle = cycles[idx];
    //     // Key Optimization: Find connectable subsequent cycles
    //     auto it_air = airport_start_map.find(cur_cycle.end_airport);
    //     if (it_air != airport_start_map.end()) {
    //         // Corrected interval check: at least 2 full days
    //         auto min_start = cur_cycle.end_day_tp + std::chrono::days(3);
    //         auto& start_map = it_air->second;
    //         auto it_low = start_map.lower_bound(min_start);
            
    //         // Traverse all possible successor cycles
    //         for (auto it = it_low; it != start_map.end(); ++it) {
    //             int next_idx = it->second;
    //             // Candidate reward = current reward + successor reward
    //             double cand = dist[idx] + cycles[next_idx].reward;
                
    //             // Relaxation
    //             if (cand > dist[next_idx] + 1e-6) {
    //                 dist[next_idx] = cand;
    //                 prev_idx[next_idx] = idx;
    //             }
    //         }
    //     }
        
    //     // Add current cycle to index (for future cycles lookup)
    //     airport_start_map[cur_cycle.start_airport].insert(
    //         {cur_cycle.start_day_tp, idx});
    // }

    // // Select best cycle reaching sink
    // double best_total = NEG_INF;
    // int best_end = -1;
    // for (int i = 0; i < n_cycles; ++i) {
    //     if (!cycles[i].can_reach_sink) continue;
    //     if (dist[i] > best_total) {
    //         best_total = dist[i];
    //         best_end = i;
    //     }
    // }

    // if (best_end == -1 || best_total - crew_dual <= 1e-6) {
    //     return {};
    // }

    // // Backtrack
    // std::vector<int> path_indices;
    // for (int idx = best_end; idx != -1; idx = prev_idx[idx]) {
    //     path_indices.push_back(idx);
    // }
    // std::reverse(path_indices.begin(), path_indices.end());

    // std::vector<FDP> chosen_cycle;
    // for (int idx : path_indices) {
    //     for (int fdp_idx : cycles[idx].fdps) {
    //         if (fdp_idx == -1) continue;
    //         chosen_cycle.push_back(network.sorted_fdps[fdp_idx]);
    //     }
    // }
    return chosen_cycle;
}

void SubproblemSolver::precomputeAllFDPNetworks() {
    std::cout << "Starting to precompute FDP networks for all crews..." << std::endl;
    
    // Get all crew IDs
    std::vector<std::string> all_crew_ids;
    for (const auto& [crew_id, crew] : data_.get_all_crews()) {
        std::string file_path = getNetworkFilePath(crew_id);
        if (fs::exists(file_path)) {
            continue;
        }
        all_crew_ids.push_back(crew_id);
    }
    
    // Create network storage directory
    if (!fs::exists(network_directory_)) {
        try {
            fs::create_directories(network_directory_);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create network directory: " << e.what() << std::endl;
            return;
        }
    }
    
    // Single-threaded processing of all crew FDP networks
    size_t total_count = all_crew_ids.size();
    size_t processed_count = 0;
    
    for (const std::string& crew_id : all_crew_ids) {
        // Filter valid FDPs
        std::vector<FDP> valid_fdps = filterValidFDPs(crew_id);
        
        if (!valid_fdps.empty()) {
            // Build network
            FDPNetwork network = buildFDPNetwork(crew_id, valid_fdps);
            
            // Save to file
            std::string file_path = getNetworkFilePath(crew_id);
            serializeFDPNetwork(crew_id, network, file_path);

            // Validate network
            // FDPNetwork test_network;
            // if (!deserializeFDPNetwork(crew_id, file_path, test_network)) {
            //     std::cerr << "Deserialize network failed: " << crew_id << std::endl;
            //     continue;
            // }
            // if (!compareNetworks(network, test_network)) {
            //     std::cerr << "Network inconsistent: " << crew_id << std::endl;
            //     continue;
            // }
        }
        
        // Update progress
        processed_count++;
        
        std::cout << "Processed " << processed_count << " / " << total_count 
                 << " crew FDP networks (" 
                 << std::fixed << std::setprecision(1) 
                 << (100.0 * processed_count / total_count) << "%)" << std::endl;
    }
    
    std::cout << "All crew FDP networks computation completed and saved to " << network_directory_ << " directory" << std::endl;
}

void SubproblemSolver::precomputeAllFDPNetworksParallel(int num_threads) {
    std::cout << "Starting multi-threaded precomputation of all crew FDP networks, using " << num_threads << " threads..." << std::endl;
    
    // Get all crew IDs to process
    std::vector<std::string> all_crew_ids;
    {
        for (const auto& [crew_id, crew] : data_.get_all_crews()) {
            std::string file_path = getNetworkFilePath(crew_id);
            if (fs::exists(file_path)) {
                continue;
            }
            all_crew_ids.push_back(crew_id);
        }
    }
    
    // Create network storage directory
    if (!fs::exists(network_directory_)) {
        try {
            fs::create_directories(network_directory_);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create network directory: " << e.what() << std::endl;
            return;
        }
    }
    
    size_t total_count = all_crew_ids.size();
    std::atomic<size_t> processed_count(0);
    std::mutex cout_mutex;
    
    // Create task queue
    std::mutex queue_mutex;
    size_t next_index = 0;
    
    // Create thread pool
    std::vector<std::thread> threads;
    
    auto worker_function = [&]() {
        // Create an independent SubproblemSolver instance for each thread
        SubproblemSolver local_solver(data_, master_, network_directory_, 
                non_base_rejection_prob_, beam_width_);
                
        while (true) {
            // Get next crew ID to process
            std::string crew_id;
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (next_index >= all_crew_ids.size()) {
                    break;
                }
                crew_id = all_crew_ids[next_index++];
            }

            try {
                // Filter valid FDPs
                std::vector<FDP> valid_fdps = local_solver.filterValidFDPs(crew_id);
                
                if (!valid_fdps.empty()) {
                    // Build network
                    FDPNetwork network = local_solver.buildFDPNetwork(crew_id, valid_fdps);
                    
                    // Save to file
                    std::string file_path = getNetworkFilePath(crew_id);
                    std::string temp_file_path = file_path + ".tmp";

                    // Use RAII to ensure temp file cleanup
                    struct TempFileGuard {
                        std::string path;
                        ~TempFileGuard() {
                            if (fs::exists(path)) {
                                fs::remove(path);
                            }
                        }
                    } temp_file_guard{temp_file_path};
                    
                    // Try serialization and validation up to 3 times
                    bool success = false;
                    while (!success) {
                        // Serialize to temp file
                        if (!local_solver.serializeFDPNetwork(crew_id, network, temp_file_path)) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            continue;
                        }
                        
                        FDPNetwork test_network;
                        if (!local_solver.deserializeFDPNetwork(crew_id, temp_file_path, test_network)) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            continue;
                        }
                        
                        if (local_solver.compareNetworks(network, test_network)) {
                            try {
                                if (fs::exists(file_path)) {
                                    fs::remove(file_path);
                                }
                                fs::rename(temp_file_path, file_path);
                                success = true;
                                break;
                            } catch (const std::exception& e) {
                                std::lock_guard<std::mutex> lock(cout_mutex);
                                std::cerr << "File rename failed: " << e.what() << std::endl;
                            }
                        }
                    }
                }
                
                // Clear local cache
                local_solver.clearCache();
                
                // Clear possible large objects
                valid_fdps.clear();
                valid_fdps.shrink_to_fit();
                
                // Update progress
                size_t current = ++processed_count;
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout << "Processed " << current << " / " << total_count 
                             << " crew FDP networks (" 
                             << std::fixed << std::setprecision(1) 
                             << (100.0 * current / total_count) << "%)" << std::endl;
                }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cerr << "Error processing crew " << crew_id << ": " << e.what() << std::endl;
                // Ensure cache is cleared on exception
                local_solver.clearCache();
            }
        }
        
        // Ensure cleanup before thread ends
        local_solver.clearCache();
    };
    
    // Start worker threads
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_function);
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    std::cout << "All crew FDP networks computation completed and saved to " << network_directory_ << " directory" << std::endl;
}


std::string SubproblemSolver::getNetworkFilePath(const std::string& crew_id) const {
    return network_directory_ + "/" + crew_id + ".fdp";
}

bool SubproblemSolver::serializeFDPNetwork(const std::string& crew_id, const FDPNetwork& network, 
                                          const std::string& filename) const {
    try {
        // Create parent directory
        fs::path file_path(filename);
        fs::create_directories(file_path.parent_path());
        
        // Open file
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Cannot open file for writing: " << filename << std::endl;
            return false;
        }
        
        // Write network basic info
        size_t n_fdps = network.sorted_fdps.size();
        size_t n_nodes = network.nodes.size();
        file.write(reinterpret_cast<const char*>(&n_fdps), sizeof(n_fdps));
        file.write(reinterpret_cast<const char*>(&n_nodes), sizeof(n_nodes));
        file.write(reinterpret_cast<const char*>(&network.source), sizeof(network.source));
        file.write(reinterpret_cast<const char*>(&network.sink), sizeof(network.sink));
        
        // Write node info
        for (const auto& node : network.nodes) {
            // Write airport
            size_t airport_len = node.airport.size();
            file.write(reinterpret_cast<const char*>(&airport_len), sizeof(airport_len));
            file.write(node.airport.c_str(), airport_len);
            
            // Write time point
            auto duration = node.time.time_since_epoch();
            int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            file.write(reinterpret_cast<const char*>(&microseconds), sizeof(microseconds));
        }
        
        // Write FDP sequence
        for (const auto& fdp : network.sorted_fdps) {
            // Write task count
            size_t task_count = fdp.tasks.size();
            file.write(reinterpret_cast<const char*>(&task_count), sizeof(task_count));
            
            // Write each task
            for (const auto& task : fdp.tasks) {
                // Write string length and content
                auto writeString = [&file](const std::string& str) {
                    size_t len = str.size();
                    file.write(reinterpret_cast<const char*>(&len), sizeof(len));
                    file.write(str.c_str(), len);
                };
                
                writeString(task.id);
                writeString(task.task_type);
                writeString(task.start_airport);
                writeString(task.end_airport);
                writeString(task.aircraft_no);
                
                // Directly write time point timestamp (microseconds)
                auto writeTimePoint = [&file](const time_point& tp) {
                    auto duration = tp.time_since_epoch();
                    int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
                    file.write(reinterpret_cast<const char*>(&microseconds), sizeof(microseconds));
                };
                
                writeTimePoint(task.start_time);
                writeTimePoint(task.end_time);
                
                // Write flight time (minutes)
                int64_t fly_minutes = task.fly_time.count();
                file.write(reinterpret_cast<const char*>(&fly_minutes), sizeof(fly_minutes));
            }
        }
        
        // Write graph structure
        size_t graph_size = network.graph.size();
        file.write(reinterpret_cast<const char*>(&graph_size), sizeof(graph_size));
        
        for (const auto& adj_list : network.graph) {
            size_t adj_size = adj_list.size();
            file.write(reinterpret_cast<const char*>(&adj_size), sizeof(adj_size));
            
            for (const auto& edge : adj_list) {
                // Write target node index
                size_t target_node = edge.first;
                file.write(reinterpret_cast<const char*>(&target_node), sizeof(target_node));
                
                // Write edge info
                int best_fdp_idx = edge.second.best_fdp_idx;
                double reward = edge.second.reward;
                file.write(reinterpret_cast<const char*>(&best_fdp_idx), sizeof(best_fdp_idx));
                file.write(reinterpret_cast<const char*>(&reward), sizeof(reward));
            }
        }
        
        // Write FDP to edge map
        size_t fdp_to_edge_size = network.fdp_to_edge.size();
        file.write(reinterpret_cast<const char*>(&fdp_to_edge_size), sizeof(fdp_to_edge_size));
        
        for (const auto& [fdp_idx, edge_pair] : network.fdp_to_edge) {
            // Write FDP index
            file.write(reinterpret_cast<const char*>(&fdp_idx), sizeof(fdp_idx));
            
            // Write edge source and destination
            file.write(reinterpret_cast<const char*>(&edge_pair.first), sizeof(edge_pair.first));
            file.write(reinterpret_cast<const char*>(&edge_pair.second), sizeof(edge_pair.second));
        }

        // Write node_to_fdp_start
        size_t node_to_fdp_start_size = network.node_to_fdp_start.size();
        file.write(reinterpret_cast<const char*>(&node_to_fdp_start_size), sizeof(node_to_fdp_start_size));
        for (const auto& [node, fdp_vec] : network.node_to_fdp_start) {
            // Write node
            size_t airport_len = node.airport.size();
            file.write(reinterpret_cast<const char*>(&airport_len), sizeof(airport_len));
            file.write(node.airport.c_str(), airport_len);
            auto duration = node.time.time_since_epoch();
            int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            file.write(reinterpret_cast<const char*>(&microseconds), sizeof(microseconds));
            // Write vector<int>
            size_t vec_size = fdp_vec.size();
            file.write(reinterpret_cast<const char*>(&vec_size), sizeof(vec_size));
            for (int idx : fdp_vec) {
                file.write(reinterpret_cast<const char*>(&idx), sizeof(idx));
            }
        }
        // Write node_to_fdp_end
        size_t node_to_fdp_end_size = network.node_to_fdp_end.size();
        file.write(reinterpret_cast<const char*>(&node_to_fdp_end_size), sizeof(node_to_fdp_end_size));
        for (const auto& [node, fdp_vec] : network.node_to_fdp_end) {
            size_t airport_len = node.airport.size();
            file.write(reinterpret_cast<const char*>(&airport_len), sizeof(airport_len));
            file.write(node.airport.c_str(), airport_len);
            auto duration = node.time.time_since_epoch();
            int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            file.write(reinterpret_cast<const char*>(&microseconds), sizeof(microseconds));
            size_t vec_size = fdp_vec.size();
            file.write(reinterpret_cast<const char*>(&vec_size), sizeof(vec_size));
            for (int idx : fdp_vec) {
                file.write(reinterpret_cast<const char*>(&idx), sizeof(idx));
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Serialize FDP network failed: " << e.what() << std::endl;
        return false;
    }
}

bool SubproblemSolver::deserializeFDPNetwork(const std::string& crew_id, const std::string& filename, 
                                            FDPNetwork& network) {
    try {
        // Open file
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // Read network basic info
        size_t n_fdps, n_nodes;
        file.read(reinterpret_cast<char*>(&n_fdps), sizeof(n_fdps));
        file.read(reinterpret_cast<char*>(&n_nodes), sizeof(n_nodes));
        file.read(reinterpret_cast<char*>(&network.source), sizeof(network.source));
        file.read(reinterpret_cast<char*>(&network.sink), sizeof(network.sink));
        
        // Read node info
        network.nodes.clear();
        network.nodes.reserve(n_nodes);
        
        for (size_t i = 0; i < n_nodes; ++i) {
            NetworkNode node;
            
            // Read airport
            size_t airport_len;
            file.read(reinterpret_cast<char*>(&airport_len), sizeof(airport_len));
            node.airport.resize(airport_len);
            file.read(&node.airport[0], airport_len);
            
            // Read time point
            int64_t microseconds;
            file.read(reinterpret_cast<char*>(&microseconds), sizeof(microseconds));
            node.time = time_point(std::chrono::microseconds(microseconds));
            
            network.nodes.push_back(node);
        }
        
        // Read FDP sequence
        network.sorted_fdps.clear();
        network.sorted_fdps.reserve(n_fdps);
        
        for (size_t i = 0; i < n_fdps; ++i) {
            FDP fdp;
            
            // Read task count
            size_t task_count;
            file.read(reinterpret_cast<char*>(&task_count), sizeof(task_count));
            
            // Read each task
            for (size_t j = 0; j < task_count; ++j) {
                Task task;
                
                // Read string
                auto readString = [&file]() -> std::string {
                    size_t len;
                    file.read(reinterpret_cast<char*>(&len), sizeof(len));
                    std::string str(len, '\0');
                    file.read(&str[0], len);
                    return str;
                };
                
                task.id = readString();
                task.task_type = readString();
                task.start_airport = readString();
                task.end_airport = readString();
                task.aircraft_no = readString();
                
                // Read time point
                auto readTimePoint = [&file]() -> time_point {
                    int64_t microseconds;
                    file.read(reinterpret_cast<char*>(&microseconds), sizeof(microseconds));
                    return time_point(std::chrono::microseconds(microseconds));
                };
                
                task.start_time = readTimePoint();
                task.end_time = readTimePoint();
                
                // Read flight time
                int64_t fly_minutes;
                file.read(reinterpret_cast<char*>(&fly_minutes), sizeof(fly_minutes));
                task.fly_time = std::chrono::minutes(fly_minutes);
                
                fdp.tasks.push_back(task);
            }
            
            network.sorted_fdps.push_back(fdp);
        }
        
        // Read graph structure
        size_t graph_size;
        file.read(reinterpret_cast<char*>(&graph_size), sizeof(graph_size));
        
        network.graph.clear();
        network.graph.resize(graph_size);
        
        for (size_t i = 0; i < graph_size; ++i) {
            size_t adj_size;
            file.read(reinterpret_cast<char*>(&adj_size), sizeof(adj_size));
            
            network.graph[i].reserve(adj_size);
            for (size_t j = 0; j < adj_size; ++j) {
                // Read target node index
                size_t target_node;
                file.read(reinterpret_cast<char*>(&target_node), sizeof(target_node));
                
                // Read edge info
                EdgeInfo edge_info;
                file.read(reinterpret_cast<char*>(&edge_info.best_fdp_idx), sizeof(edge_info.best_fdp_idx));
                file.read(reinterpret_cast<char*>(&edge_info.reward), sizeof(edge_info.reward));
                
                network.graph[i].push_back({target_node, edge_info});
            }
        }
        
        // Read FDP to edge map
        network.fdp_to_edge.clear();
        size_t fdp_to_edge_size;
        file.read(reinterpret_cast<char*>(&fdp_to_edge_size), sizeof(fdp_to_edge_size));
        
        for (size_t i = 0; i < fdp_to_edge_size; ++i) {
            // Read FDP index
            int fdp_idx;
            file.read(reinterpret_cast<char*>(&fdp_idx), sizeof(fdp_idx));
            
            // Read edge source and destination
            size_t from_node, to_node;
            file.read(reinterpret_cast<char*>(&from_node), sizeof(from_node));
            file.read(reinterpret_cast<char*>(&to_node), sizeof(to_node));
            
            // Store FDP to edge map
            network.fdp_to_edge[fdp_idx] = {from_node, to_node};
        }
        
        // Initialize reward values
        network.rewards.resize(n_fdps, 0.0);

        // Read node_to_fdp_start
        network.node_to_fdp_start.clear();
        size_t node_to_fdp_start_size;
        file.read(reinterpret_cast<char*>(&node_to_fdp_start_size), sizeof(node_to_fdp_start_size));
        for (size_t i = 0; i < node_to_fdp_start_size; ++i) {
            NetworkNode node;
            size_t airport_len;
            file.read(reinterpret_cast<char*>(&airport_len), sizeof(airport_len));
            node.airport.resize(airport_len);
            file.read(&node.airport[0], airport_len);
            int64_t microseconds;
            file.read(reinterpret_cast<char*>(&microseconds), sizeof(microseconds));
            node.time = time_point(std::chrono::microseconds(microseconds));
            size_t vec_size;
            file.read(reinterpret_cast<char*>(&vec_size), sizeof(vec_size));
            std::vector<int> fdp_vec(vec_size);
            for (size_t j = 0; j < vec_size; ++j) {
                file.read(reinterpret_cast<char*>(&fdp_vec[j]), sizeof(fdp_vec[j]));
            }
            network.node_to_fdp_start[node] = fdp_vec;
        }
        // Read node_to_fdp_end
        network.node_to_fdp_end.clear();
        size_t node_to_fdp_end_size;
        file.read(reinterpret_cast<char*>(&node_to_fdp_end_size), sizeof(node_to_fdp_end_size));
        for (size_t i = 0; i < node_to_fdp_end_size; ++i) {
            NetworkNode node;
            size_t airport_len;
            file.read(reinterpret_cast<char*>(&airport_len), sizeof(airport_len));
            node.airport.resize(airport_len);
            file.read(&node.airport[0], airport_len);
            int64_t microseconds;
            file.read(reinterpret_cast<char*>(&microseconds), sizeof(microseconds));
            node.time = time_point(std::chrono::microseconds(microseconds));
            size_t vec_size;
            file.read(reinterpret_cast<char*>(&vec_size), sizeof(vec_size));
            std::vector<int> fdp_vec(vec_size);
            for (size_t j = 0; j < vec_size; ++j) {
                file.read(reinterpret_cast<char*>(&fdp_vec[j]), sizeof(fdp_vec[j]));
            }
            network.node_to_fdp_end[node] = fdp_vec;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Deserialize FDP network failed: " << e.what() << std::endl;
        return false;
    }
}

std::string SubproblemSolver::serializeFDP(const FDP& fdp) const {
    std::stringstream ss;
    
    // Write task count
    ss << fdp.tasks.size() << "|";
    
    // Serialize each task
    for (const auto& task : fdp.tasks) {
        ss << task.id << "|";
        ss << task.task_type << "|";
        ss << task.start_airport << "|";
        ss << task.end_airport << "|";
        
        // Convert time point to string, use GMT to avoid timezone issues
        auto to_time_string = [](const time_point& tp) {
            auto time_t = std::chrono::system_clock::to_time_t(tp);
            std::tm* tm = std::gmtime(&time_t);
            char buffer[32];
            std::strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", tm);
            return std::string(buffer);
        };
        
        ss << to_time_string(task.start_time) << "|";
        ss << to_time_string(task.end_time) << "|";
        ss << task.fly_time.count() << "|";
        ss << task.aircraft_no << "|";
    }
    
    return ss.str();
}

FDP SubproblemSolver::deserializeFDP(const std::string& str) const {
    FDP fdp;
    std::stringstream ss(str);
    std::string token;
    
    // Read task count
    std::getline(ss, token, '|');
    int task_count = std::stoi(token);
    
    // Parse each task
    for (int i = 0; i < task_count; ++i) {
        Task task;
        
        std::getline(ss, task.id, '|');
        std::getline(ss, task.task_type, '|');
        std::getline(ss, task.start_airport, '|');
        std::getline(ss, task.end_airport, '|');
        
        // Convert string to time point, use GMT to avoid timezone issues
        std::string time_str;
        std::getline(ss, time_str, '|');
        auto from_time_string = [](const std::string& time_str) {
            std::tm tm = {};
            std::istringstream iss(time_str);
            iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            // Convert GMT time to time point
            tm.tm_isdst = 0; // Disable DST
            return std::chrono::system_clock::from_time_t(timegm(&tm));
        };
        task.start_time = from_time_string(time_str);
        
        std::getline(ss, time_str, '|');
        task.end_time = from_time_string(time_str);
        
        std::string minutes_str;
        std::getline(ss, minutes_str, '|');
        task.fly_time = std::chrono::minutes(std::stoi(minutes_str));
        
        std::getline(ss, task.aircraft_no, '|');
        
        fdp.tasks.push_back(task);
    }
    
    return fdp;
}

// ==============================================================================
// Serialization Test
// ==============================================================================

bool SubproblemSolver::compareTasks(const Task& t1, const Task& t2) const {
    if (t1.id != t2.id) {
        // std::cout << "  - Task ID mismatch: " << t1.id << " vs " << t2.id << std::endl;
        return false;
    }
    if (t1.task_type != t2.task_type) {
        // std::cout << "  - Task type mismatch: " << t1.task_type << " vs " << t2.task_type << std::endl;
        return false;
    }
    if (t1.start_airport != t2.start_airport) {
        std::cout << "  - Task start_airport mismatch: " << t1.start_airport << " vs " << t2.start_airport << std::endl;
        return false;
    }
    if (t1.end_airport != t2.end_airport) {
        std::cout << "  - Task end_airport mismatch: " << t1.end_airport << " vs " << t2.end_airport << std::endl;
        return false;
    }
    if (t1.start_time != t2.start_time) {
        std::cout << "  - Task start_time mismatch" << std::endl;
        return false;
    }
    if (t1.end_time != t2.end_time) {
        // std::cout << "  - Task end_time mismatch" << std::endl;
        return false;
    }
    if (t1.fly_time != t2.fly_time) {
        std::cout << "  - Task fly_time mismatch" << std::endl;
        return false;
    }
    if (t1.aircraft_no != t2.aircraft_no) {
        std::cout << "  - Task aircraft_no mismatch: " << t1.aircraft_no << " vs " << t2.aircraft_no << std::endl;
        return false;
    }
    return true;
}

bool SubproblemSolver::compareFDPs(const FDP& f1, const FDP& f2) const {
    if (f1.tasks.size() != f2.tasks.size()) {
        // std::cout << " - FDP tasks size mismatch: " << f1.tasks.size() << " vs " << f2.tasks.size() << std::endl;
        return false;
    }
    for (size_t i = 0; i < f1.tasks.size(); ++i) {
        if (!compareTasks(f1.tasks[i], f2.tasks[i])) {
            // std::cout << " - Difference in Task at index " << i << std::endl;
            return false;
        }
    }
    return true;
}

bool SubproblemSolver::compareNetworks(const FDPNetwork& n1, const FDPNetwork& n2) const {
    bool is_identical = true;

    if (n1.source != n2.source) {
        std::cout << "Network source mismatch: " << n1.source << " vs " << n2.source << std::endl;
        is_identical = false;
    }

    if (n1.sink != n2.sink) {
        std::cout << "Network sink mismatch: " << n1.sink << " vs " << n2.sink << std::endl;
        is_identical = false;
    }

    if (n1.sorted_fdps.size() != n2.sorted_fdps.size()) {
        std::cout << "Network sorted_fdps size mismatch: " << n1.sorted_fdps.size() << " vs " << n2.sorted_fdps.size() << std::endl;
        return false; // Fatal difference, no need to continue
    }

    for (size_t i = 0; i < n1.sorted_fdps.size(); ++i) {
        if (!compareFDPs(n1.sorted_fdps[i], n2.sorted_fdps[i])) {
            std::cout << "Difference in FDP at index " << i << std::endl;
            is_identical = false;
        }
    }

    // Compare nodes
    if (n1.nodes.size() != n2.nodes.size()) {
        std::cout << "Network nodes size mismatch: " << n1.nodes.size() << " vs " << n2.nodes.size() << std::endl;
        return false; // Fatal difference
    }

    for (size_t i = 0; i < n1.nodes.size(); ++i) {
        const auto& node1 = n1.nodes[i];
        const auto& node2 = n2.nodes[i];
        
        if (node1.airport != node2.airport) {
            std::cout << "Node airport mismatch at index " << i << ": " << node1.airport << " vs " << node2.airport << std::endl;
            is_identical = false;
        }
        
        if (node1.time != node2.time) {
            std::cout << "Node time mismatch at index " << i << std::endl;
            is_identical = false;
        }
    }

    if (n1.graph.size() != n2.graph.size()) {
        std::cout << "Network graph size mismatch: " << n1.graph.size() << " vs " << n2.graph.size() << std::endl;
        return false; // Fatal difference
    }
    
    // Compare graph structure
    for (size_t i = 0; i < n1.graph.size(); ++i) {
        if (n1.graph[i].size() != n2.graph[i].size()) {
            std::cout << "Graph adjacency list size mismatch for node " << i << ": " 
                     << n1.graph[i].size() << " vs " << n2.graph[i].size() << std::endl;
            is_identical = false;
            continue;
        }
        
        // Create two maps to compare edges
        std::unordered_map<size_t, const EdgeInfo*> edges_map1, edges_map2;
        
        for (const auto& edge : n1.graph[i]) {
            edges_map1[edge.first] = &edge.second;
        }
        
        for (const auto& edge : n2.graph[i]) {
            edges_map2[edge.first] = &edge.second;
        }
        
        // Compare maps
        if (edges_map1.size() != edges_map2.size()) {
            std::cout << "Edge map size mismatch for node " << i << std::endl;
            is_identical = false;
            continue;
        }
        
        for (const auto& [target, info_ptr1] : edges_map1) {
            auto it = edges_map2.find(target);
            if (it == edges_map2.end()) {
                std::cout << "Edge to node " << target << " missing in second network for node " << i << std::endl;
                is_identical = false;
                continue;
            }
            
            const EdgeInfo* info_ptr2 = it->second;
            
            if (info_ptr1->best_fdp_idx != info_ptr2->best_fdp_idx) {
                std::cout << "Edge best_fdp_idx mismatch for edge " << i << "->" << target << ": " 
                         << info_ptr1->best_fdp_idx << " vs " << info_ptr2->best_fdp_idx << std::endl;
                is_identical = false;
            }
            
            if (std::abs(info_ptr1->reward - info_ptr2->reward) > 1e-6) {
                std::cout << "Edge reward mismatch for edge " << i << "->" << target << ": " 
                         << info_ptr1->reward << " vs " << info_ptr2->reward << std::endl;
                is_identical = false;
            }
        }
    }
    
    // Compare FDP to edge map
    if (n1.fdp_to_edge.size() != n2.fdp_to_edge.size()) {
        std::cout << "FDP to edge map size mismatch: " << n1.fdp_to_edge.size() << " vs " << n2.fdp_to_edge.size() << std::endl;
        is_identical = false;
    } else {
        for (const auto& [fdp_idx, pair1] : n1.fdp_to_edge) {
            auto it2 = n2.fdp_to_edge.find(fdp_idx);
            if (it2 == n2.fdp_to_edge.end()) {
                std::cout << "FDP index " << fdp_idx << " missing in second network's map" << std::endl;
                is_identical = false;
                continue;
            }
            const auto& pair2 = it2->second;
            if (pair1 != pair2) {
                std::cout << "Edge pair mismatch for FDP " << fdp_idx << ": (" << pair1.first << "," << pair1.second
                          << ") vs (" << pair2.first << "," << pair2.second << ")" << std::endl;
                is_identical = false;
            }
        }
    }

    // Compare node_to_fdp_start
    if (n1.node_to_fdp_start.size() != n2.node_to_fdp_start.size()) return false;
    for (const auto& [node, v1] : n1.node_to_fdp_start) {
        auto it = n2.node_to_fdp_start.find(node);
        if (it == n2.node_to_fdp_start.end() || v1 != it->second) return false;
    }
    // Compare node_to_fdp_end
    if (n1.node_to_fdp_end.size() != n2.node_to_fdp_end.size()) return false;
    for (const auto& [node, v1] : n1.node_to_fdp_end) {
        auto it = n2.node_to_fdp_end.find(node);
        if (it == n2.node_to_fdp_end.end() || v1 != it->second) return false;
    }

    return is_identical;
}


bool SubproblemSolver::testSerialization(const std::string& crew_id, FDPNetwork& original_network) {
    std::cout << "\n======================================================\n";
    std::cout << "Starting network serialization test for crew " << crew_id << std::endl;
    std::cout << "======================================================\n";

    // Print basic info of original network
    std::cout << "Original Network Info:" << std::endl;
    std::cout << " - FDP Count: " << original_network.sorted_fdps.size() << std::endl;
    std::cout << " - Source Index: " << original_network.source << std::endl;
    std::cout << " - Sink Index: " << original_network.sink << std::endl;
    std::cout << " - Source Connections: " << original_network.graph[original_network.source].size() << std::endl;
    
    // Count nodes connected to sink
    // size_t sink_connections = 0;
    // for (size_t i = 0; i < original_network.sorted_fdps.size(); ++i) {
    //     auto& adj = original_network.graph[i];
    //     if (std::find(adj.begin(), adj.end(), std::make_pair(original_network.sink, EdgeInfo())) != adj.end()) {
    //         sink_connections++;
    //     }
    // }
    // std::cout << " - Nodes connected to sink: " << sink_connections << std::endl;

    // 3. Serialize network to temp file
    std::string temp_filename = "temp_network_test_" + crew_id + ".fdp";
    std::cout << "Serializing network to temp file: " << temp_filename << "..." << std::endl;
    if (!serializeFDPNetwork(crew_id, original_network, temp_filename)) {
        std::cerr << "Serialize network failed. Test aborted." << std::endl;
        return false;
    }
    std::cout << "Serialization successful." << std::endl;

    // 4. Deserialize network from file
    FDPNetwork deserialized_network;
    std::cout << "Deserializing network from file..." << std::endl;
    if (!deserializeFDPNetwork(crew_id, temp_filename, deserialized_network)) {
        std::cerr << "Deserialize network failed. Test aborted." << std::endl;
        fs::remove(temp_filename);
        return false;
    }
    std::cout << "Deserialization successful." << std::endl;
    
    // Print deserialized network info
    std::cout << "Deserialized Network Info:" << std::endl;
    std::cout << " - FDP Count: " << deserialized_network.sorted_fdps.size() << std::endl;
    std::cout << " - Source Index: " << deserialized_network.source << std::endl;
    std::cout << " - Sink Index: " << deserialized_network.sink << std::endl;
    std::cout << " - Source Connections: " << deserialized_network.graph[deserialized_network.source].size() << std::endl;
    
    // // Count nodes connected to sink
    // sink_connections = 0;
    // for (size_t i = 0; i < deserialized_network.sorted_fdps.size(); ++i) {
    //     auto& adj = deserialized_network.graph[i];
    //     if (std::find(adj.begin(), adj.end(), deserialized_network.sink) != adj.end()) {
    //         sink_connections++;
    //     }
    // }
    // std::cout << " - Nodes connected to sink: " << sink_connections << std::endl;

    // 5. Compare two networks
    std::cout << "\n--- Comparing Original and Deserialized Networks ---\n" << std::endl;
    bool are_identical = compareNetworks(original_network, deserialized_network);

    // 6. Cleanup temp file
    fs::remove(temp_filename);

    std::cout << "\n--- Test Result ---\n";
    if (are_identical) {
        std::cout << "Success: Original and deserialized networks are identical." << std::endl;
        return true;
    } else {
        std::cout << "Failure: Differences found between original and deserialized networks." << std::endl;
        return false;
    }
}