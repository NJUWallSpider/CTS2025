#pragma once

#include "../data_model/SchedulingData.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <functional>
#include <thread>

// Forward declaration
class MasterProblem;

// Node type definition - Represents (Airport, TimePoint) combination
struct NetworkNode {
    std::string airport;
    time_point time;
    
    // For comparison and hashing
    bool operator==(const NetworkNode& other) const {
        return airport == other.airport && time == other.time;
    }
};

// Define hash function for NetworkNode
struct NetworkNodeHash {
    std::size_t operator()(const NetworkNode& node) const {
        return std::hash<std::string>{}(node.airport) ^ 
               std::hash<std::size_t>{}(std::chrono::system_clock::to_time_t(node.time));
    }
};

// Edge Info - Stores the best FDP connecting two nodes
struct EdgeInfo {
    int best_fdp_idx = -1;  // Index of optimal FDP in sorted_fdps
    double reward = 0.0;    // Reward value of optimal FDP
    bool operator==(const EdgeInfo& other) const {
        return best_fdp_idx == other.best_fdp_idx && std::abs(reward - other.reward) < 1e-8;
    }
};

// FDP Network Cache Structure
class FDPNetwork {
public:
    FDPNetwork() = default;
    std::vector<FDP> sorted_fdps;           // List of all available FDPs
    std::vector<double> rewards;            // Reward value for each FDP
    
    // Node collection
    std::vector<NetworkNode> nodes;
    
    // Adjacency list representation of directed graph - Mapping from node index to (target node index, edge info)
    std::vector<std::vector<std::pair<size_t, EdgeInfo>>> graph;
    
    // Source and Sink indices
    size_t source;
    size_t sink;
    
    // Mapping from FDP index to corresponding edge (for fast updates)
    std::unordered_map<int, std::pair<size_t, size_t>> fdp_to_edge;

    // Mapping from node index to FDP index
    std::unordered_map<NetworkNode, std::vector<int>, NetworkNodeHash> node_to_fdp_start;
    std::unordered_map<NetworkNode, std::vector<int>, NetworkNodeHash> node_to_fdp_end;
};

class SubproblemSolver {
public:
    // Constructor, receives data and reference to master problem
    SubproblemSolver(const SchedulingData& data, const MasterProblem& master, std::string fdp_path, 
                    double non_base_rejection_prob = 0.0, int beam_width = 20);
    
    // Update dual values
    void updateDuals();
    
    // Clear all cache data
    void clearCache();
    
    // Clear only crew-specific cache
    void clearCrewSpecificCache();

    // Solve subproblem for a specific crew
    bool solveForCrew(const std::string& crew_id);
    bool solveForCrewWithDuals(const std::string& crew_id, 
                               const std::unordered_map<std::string, double>& flight_duals);
    
    // Get optimal FDP
    const FDP& getBestFDP() const;
    
    // Get reduced cost of optimal solution
    double getReducedCost() const;
    
    // Precompute FDP networks for all crews and save
    void precomputeAllFDPNetworks();

    void precomputeAllFDPNetworksParallel(int num_threads = std::thread::hardware_concurrency());
    
    // Test serialization and deserialization of FDP network
    bool testSerialization(const std::string& crew_id, FDPNetwork& original_network);
    
private:

    // Use std::chrono to define time point type
    using time_point = std::chrono::system_clock::time_point;
    

    // Filter valid FDPs executable by crew
    std::vector<FDP> filterValidFDPs(const std::string& crew_id);
    
    // Calculate reward value for FDP
    double calculateFDPReward(const FDP& fdp, const std::unordered_map<std::string, double>& flight_duals) const;
    
    // Construct and solve longest path problem
    std::vector<FDP> solveLongestPath(const std::string& crew_id, 
                                     const std::vector<FDP>& valid_fdps,
                                     double crew_dual);
    
    // Helper function: Group FDPs by start airport
    std::unordered_map<std::string, std::vector<std::pair<size_t, const FDP*>>>
    groupFDPsByStartAirport(const std::vector<FDP>& fdps) const;
    
    // Build FDP Network
    FDPNetwork buildFDPNetwork(const std::string& crew_id, const std::vector<FDP>& valid_fdps);
    
    // Update network reward values
    void updateNetworkRewards(FDPNetwork& network);
    
    // Solve longest path on network
    std::vector<FDP> solveLongestPathWithNetwork(const FDPNetwork& network, double crew_dual, std::string crew_id);
    
    // Get flight IDs contained in FDP (using cache)
    std::unordered_set<std::string> getCachedFlightIds(const FDP& fdp) const;
    
    // File operation: Get network file path
    std::string getNetworkFilePath(const std::string& crew_id) const;
    
    // Serialize FDP network to file
    bool serializeFDPNetwork(const std::string& crew_id, const FDPNetwork& network, 
                              const std::string& filename) const;
    
    // Deserialize FDP network from file
    bool deserializeFDPNetwork(const std::string& crew_id, const std::string& filename, 
                                FDPNetwork& network);
    
    // Serialize single FDP
    std::string serializeFDP(const FDP& fdp) const;
    
    // Deserialize single FDP
    FDP deserializeFDP(const std::string& str) const;
    
    // Compare if two Tasks are identical (for testing)
    bool compareTasks(const Task& t1, const Task& t2) const;
    
    // Compare if two FDPs are identical (for testing)
    bool compareFDPs(const FDP& f1, const FDP& f2) const;
    
    // Compare if two FDP networks are identical (for testing)
    bool compareNetworks(const FDPNetwork& n1, const FDPNetwork& n2) const;
    
private:
    // Reference to project data
    const SchedulingData& data_;
    
    // Reference to master problem
    const MasterProblem& master_;
    
    // Cache dual values for each flight
    std::unordered_map<std::string, double> flight_duals_cache_;
    
    // Cache valid FDPs executable by each crew
    std::unordered_map<std::string, std::vector<FDP>> crew_valid_fdps_cache_;
    
    // Cache flight IDs contained in FDP
    mutable std::unordered_map<std::string, std::unordered_set<std::string>> fdp_flight_ids_cache_;
    
    // Hash function for FDP pointer pair
    struct PairFDPPtrHash {
        std::size_t operator()(const std::pair<const FDP*, const FDP*>& p) const {
            return std::hash<const FDP*>{}(p.first) ^ std::hash<const FDP*>{}(p.second);
        }
    };
    
    // Cache FDP connectivity check results
    mutable std::unordered_map<std::pair<const FDP*, const FDP*>, bool, PairFDPPtrHash> connectivity_cache_;
    
    // Optimal FDP
    FDP best_fdp_;
    
    // Reduced cost of optimal solution
    double reduced_cost_;
    
    // FDP network file storage directory
    std::string network_directory_;
    
    // Probability of rejecting FDP ending at non-base airport
    double non_base_rejection_prob_;
    
    // Beam search width
    int beam_width_;
};