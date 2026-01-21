#pragma once

#include "gurobi_c++.h"
#include "../data_model/SchedulingData.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <deque>

// Column Status Information
struct ColumnInfo {
    int index;                          // Index in pairing_vars_
    std::string crew_id;                // Crew ID
    FDP fdp;                           // Flight Duty Period
    int zero_value_count;              // Number of consecutive zero values
    int age;                           // Age (number of iterations)
    double last_reduced_cost;          // Last reduced cost
    bool is_active;                    // Is in active set

    ColumnInfo(int idx, const std::string& cid, const FDP& f) 
        : index(idx), crew_id(cid), fdp(f), zero_value_count(0), age(0), 
          last_reduced_cost(0.0), is_active(true) {}
};

class MasterProblem {
public:
    MasterProblem(const SchedulingData& data, std::string data_version);
    ~MasterProblem();

    // Initialize Master Problem Model
    void initialize();
    
    // Solve current Master Problem
    void solve();
    
    // Solve Integer Program
    void solveIntegerProgram();
    
    // Get Flight Dual Value
    double getFlightDual(const std::string& flight_id) const;
    
    // Get Crew Dual Value
    double getCrewDual(const std::string& crew_id) const;
    
    // Add new column (Flight Pairing) to Master Problem
    int addColumn(const std::string& crew_id, const FDP& fdp);
    
    // Check if column already exists
    bool columnExists(const std::string& crew_id, const FDP& fdp) const;
    
    // Get objective value of current solution (number of covered flights)
    double getObjectiveValue() const;
    
    // Check if algorithm converged
    bool isConverged() const;
    
    // Print final solution
    void printSolution() const;

private:
    std::string data_version_;
    // Reference to external data
    const SchedulingData& data_;
    
    // Gurobi environment and model
    GRBEnv env_;
    std::unique_ptr<GRBModel> model_;
    
    // Flight coverage variables y_i
    std::unordered_map<std::string, GRBVar> flight_vars_;
    
    // Flight pairing assignment variables x_jk
    std::vector<GRBVar> pairing_vars_;
    
    // Record Crew and FDP for each variable
    std::vector<std::pair<std::string, FDP>> pairing_info_;
    
    // Record which variables cover each flight
    std::unordered_map<std::string, std::vector<int>> flight_coverage_;
    
    // Record which variables use each crew
    std::unordered_map<std::string, std::vector<int>> crew_usage_;
    
    // Record dual values
    std::unordered_map<std::string, double> flight_duals_;
    std::unordered_map<std::string, double> crew_duals_;
    
    // Store constraint objects for direct access instead of name lookup
    std::unordered_map<std::string, GRBConstr> flight_constrs_;
    std::unordered_map<std::string, GRBConstr> crew_constrs_;
    
    // Hash set of added columns to check for duplicates
    std::unordered_set<std::string> added_columns_;
    
    // Helper function to extract covered flights from FDP
    std::vector<std::string> getFlightsFromFDP(const FDP& fdp) const;
    
    // Helper function to generate unique identifier for FDP
    std::string generateColumnHash(const std::string& crew_id, const FDP& fdp) const;
    
    // Iteration counter and convergence flag
    int iteration_count_;
    bool converged_;

    // Column management parameters
    static constexpr int MAX_ZERO_VALUE_COUNT = 10;   // Max consecutive zero value count
    static constexpr int MAX_AGE = 50;               // Max age
    static constexpr double REDUCED_COST_THRESHOLD = -10.0; // Reduced cost threshold
    static constexpr int REACTIVATION_INTERVAL = 5;   // Reactivation check interval
    static constexpr int MAX_ACTIVE_COLUMNS = 2500;   // Max number of active columns

    // Column management data structures
    std::vector<ColumnInfo> columns_;                // Info of all columns
    std::deque<int> column_pool_;                   // Column pool (stores indices of inactive columns)

    // Column management methods
    void manageColumns();                           // Main logic for column management
    void updateColumnStatus();                      // Update column status info
    void deactivateColumns();                       // Move inactive columns to pool
    void reactivateColumns();                       // Reactivate promising columns from pool
    double calculateReducedCost(const ColumnInfo& col) const;  // Calculate reduced cost for column

    // New: Convert variables to integer variables
    void convertToIntegerProgram();

    // New: Export MPS file
    void exportMPSFile() const;
    // New: Try to load model from MPS file
    bool tryLoadFromMPSFile();
    // New: Get MPS file path
    std::string getMPSFilePath() const;
};
