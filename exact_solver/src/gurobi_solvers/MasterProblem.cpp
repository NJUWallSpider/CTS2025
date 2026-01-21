#include "MasterProblem.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <functional>
#include <fstream>
#include <chrono>
#include <filesystem>
#include "../data_model/ReportGenerator.hpp"

MasterProblem::MasterProblem(const SchedulingData& data, std::string data_version)
    : data_(data), env_(), iteration_count_(0), converged_(false), data_version_(data_version) {
    try {
        // Set Gurobi environment
        env_.set(GRB_IntParam_OutputFlag, 0); // Disable Gurobi output, adjust as needed
        model_ = std::make_unique<GRBModel>(env_);
    } catch (GRBException& e) {
        std::cerr << "Gurobi Error: " << e.getMessage() << std::endl;
        throw;
    }
}

MasterProblem::~MasterProblem() {
    // Smart pointer will automatically release model_
}

std::string MasterProblem::getMPSFilePath() const {
    // Create MPS file directory
    std::filesystem::path mps_dir = "exact_solver/mps/" + data_version_;
    std::filesystem::create_directories(mps_dir);
    
    // Return MPS file path for current iteration
    return (mps_dir / ("master2.mps")).string();
}

void MasterProblem::exportMPSFile() const {
    try {
        std::string mps_path = getMPSFilePath();
        model_->write(mps_path);
        std::cout << "MPS file exported: " << mps_path << std::endl;
    } catch (GRBException& e) {
        std::cerr << "Error exporting MPS file: " << e.getMessage() << std::endl;
    }
}

bool MasterProblem::tryLoadFromMPSFile() {
    try {
        std::string mps_path = getMPSFilePath();
        if (std::filesystem::exists(mps_path)) {
            model_ = std::make_unique<GRBModel>(env_, mps_path);
            std::cout << "Model loaded from MPS file: " << mps_path << std::endl;
            return true;
        }
    } catch (GRBException& e) {
        std::cerr << "Error loading MPS file: " << e.getMessage() << std::endl;
    }
    return false;
}

void MasterProblem::initialize() {
    try {
        // Try to load model from MPS file
        if (tryLoadFromMPSFile()) {
            // Clear column management data structures
            columns_.clear();
            column_pool_.clear();

            // Link variables and constraints
            // 1. Link flight coverage variables
            for (const auto& [flight_id, flight] : data_.get_all_flights()) {
                GRBVar var = model_->getVarByName("y_" + flight_id);
                if (var.get(GRB_StringAttr_VarName) != "") {
                    flight_vars_[flight_id] = var;
                }
            }

            // 2. Link flight coverage constraints
            for (const auto& [flight_id, flight] : data_.get_all_flights()) {
                GRBConstr constr = model_->getConstrByName("cover_" + flight_id);
                if (constr.get(GRB_StringAttr_ConstrName) != "") {
                    flight_constrs_[flight_id] = constr;
                }
            }

            // 3. Link crew resource constraints (only link existing constraints)
            for (int i = 0; i < model_->get(GRB_IntAttr_NumConstrs); i++) {
                GRBConstr constr = model_->getConstr(i);
                std::string constr_name = constr.get(GRB_StringAttr_ConstrName);
                if (constr_name.substr(0, 5) == "crew_") {
                    std::string crew_id = constr_name.substr(5);
                    crew_constrs_[crew_id] = constr;
                }
            }

            // Optimize model first to get initial solution
            model_->optimize();

            // 4. Link flight pairing variables and rebuild pairing_info_ and column management system
            int var_idx = 0;
            for (int i = 0; i < model_->get(GRB_IntAttr_NumVars); i++) {
                GRBVar var = model_->getVar(i);
                std::string var_name = var.get(GRB_StringAttr_VarName);
                if (var_name.substr(0, 2) == "x_") {
                    // Parse variable name to get crew ID
                    size_t first_underscore = var_name.find('_');
                    size_t second_underscore = var_name.find('_', first_underscore + 1);
                    if (second_underscore != std::string::npos) {
                        std::string crew_id = var_name.substr(second_underscore + 1);
                        pairing_vars_.push_back(var);
                        
                        // Update crew usage record
                        crew_usage_[crew_id].push_back(var_idx);
                        
                        // Collect flights covered by this variable
                        std::vector<std::string> covered_flights;
                        for (int j = 0; j < model_->get(GRB_IntAttr_NumConstrs); j++) {
                            GRBConstr constr = model_->getConstr(j);
                            double coeff = model_->getCoeff(constr, var);
                            if (coeff > 0) {
                                std::string constr_name = constr.get(GRB_StringAttr_ConstrName);
                                if (constr_name.substr(0, 6) == "cover_") {
                                    std::string flight_id = constr_name.substr(6);
                                    covered_flights.push_back(flight_id);
                                    flight_coverage_[flight_id].push_back(var_idx);
                                }
                            }
                        }

                        // Reconstruct FDP based on covered flights
                        FDP reconstructed_fdp;
                        for (const auto& flight_id : covered_flights) {
                            const Flight* flight = data_.get_flight(flight_id);
                            if (flight) {
                                Task task;
                                task.id = flight_id;
                                task.task_type = "flight";
                                task.start_airport = flight->depa_airport;
                                task.end_airport = flight->arri_airport;
                                task.start_time = flight->std;
                                task.end_time = flight->sta;
                                task.fly_time = std::chrono::minutes(flight->fly_time);
                                task.aircraft_no = flight->aircraft_no;
                                reconstructed_fdp.tasks.push_back(task);
                            }
                        }

                        // Sort tasks by time
                        std::sort(reconstructed_fdp.tasks.begin(), reconstructed_fdp.tasks.end(),
                            [](const Task& a, const Task& b) {
                                return a.start_time < b.start_time;
                            });

                        // Update pairing_info_
                        pairing_info_.push_back({crew_id, reconstructed_fdp});

                        // Update added_columns_
                        added_columns_.insert(generateColumnHash(crew_id, reconstructed_fdp));

                        // Initialize column info
                        ColumnInfo col_info(var_idx, crew_id, reconstructed_fdp);
                        
                        // Get current solution value, initialize column status
                        if (model_->get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
                            double value = var.get(GRB_DoubleAttr_X);
                            if (value < 1e-6) {
                                col_info.zero_value_count = 1;
                            }
                        }
                        
                        // Get variable upper bound
                        double ub = var.get(GRB_DoubleAttr_UB);
                        if (ub < 1e-6) {
                            col_info.is_active = false;
                            column_pool_.push_back(var_idx);
                        }

                        columns_.push_back(col_info);
                        var_idx++;
                    }
                }
            }

            model_->update();
            return;
        }

        // If no MPS file, create new model
        // Create flight coverage variables y_i
        for (const auto& [flight_id, flight] : data_.get_all_flights()) {
            flight_vars_[flight_id] = model_->addVar(0.0, 1.0, 1.0, GRB_CONTINUOUS, "y_" + flight_id);
        }

        // Set objective function: Maximize number of covered flights
        GRBLinExpr obj = 0;
        for (const auto& [flight_id, var] : flight_vars_) {
            obj += var;
        }
        model_->setObjective(obj, GRB_MAXIMIZE);

        // Add 0 >= y_i constraint during initialization
        for (const auto& [flight_id, var] : flight_vars_) {
            flight_constrs_[flight_id] = model_->addConstr(0.0 == var, "cover_" + flight_id);
        }
        
        // Update model to include new variables
        model_->update();

        // Export initial MPS file
        exportMPSFile();
    } catch (GRBException& e) {
        std::cerr << "Error initializing Master Problem: " << e.getMessage() << std::endl;
        throw;
    }
}

void MasterProblem::solve() {
    try {

        // Solve original integer model
        model_->optimize();

        // Check LP solve status
        if (model_->get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
            // Update dual values
            flight_duals_.clear();
            crew_duals_.clear();
            
            // Get dual values of flight coverage constraints
            for (const auto& [flight_id, constr] : flight_constrs_) {
                flight_duals_[flight_id] = constr.get(GRB_DoubleAttr_Pi);
            }
            
            // Get dual values of crew resource constraints
            for (const auto& [crew_id, constr] : crew_constrs_) {
                crew_duals_[crew_id] = constr.get(GRB_DoubleAttr_Pi);
            }

            //  // Update column status and manage columns
            // updateColumnStatus();
            // manageColumns();
            
            iteration_count_++;
            
            // Export MPS file for current iteration
            exportMPSFile();
        } else {
            std::cerr << "LP relaxation did not reach optimal status: " << model_->get(GRB_IntAttr_Status) << std::endl;
            // If LP relaxation is infeasible, set all dual values to 0
            for (const auto& [flight_id, _] : flight_constrs_) {
                flight_duals_[flight_id] = 0.0;
            }
            
            for (const auto& [crew_id, _] : crew_constrs_) {
                crew_duals_[crew_id] = 0.0;
            }
        }
    } catch (GRBException& e) {
        std::cerr << "Error solving Master Problem: " << e.getMessage() << std::endl;
        throw;
    }
}

double MasterProblem::getFlightDual(const std::string& flight_id) const {
    auto it = flight_duals_.find(flight_id);
    if (it != flight_duals_.end()) {
        return it->second;
    }
    return 0.0; // If not found, return 0
}

double MasterProblem::getCrewDual(const std::string& crew_id) const {
    auto it = crew_duals_.find(crew_id);
    if (it != crew_duals_.end()) {
        return it->second;
    }
    return 0.0; // If not found, return 0
}

int MasterProblem::addColumn(const std::string& crew_id, const FDP& fdp) {
    try {
        // Check if column already exists
        if (columnExists(crew_id, fdp)) {
            return -1;
        }
        
        // Get flights covered by this FDP
        std::vector<std::string> covered_flights = getFlightsFromFDP(fdp);
        
        // If FDP covers no flights, skip adding
        if (covered_flights.empty()) {
            return -1;
        }
        
        // Create new flight pairing assignment variable x_jk
        int col_idx = pairing_vars_.size();
        std::string var_name = "x_" + std::to_string(col_idx) + "_" + crew_id;
        GRBVar new_var = model_->addVar(0.0, 1.0, 0.0, GRB_CONTINUOUS, var_name);
        
        // Update flight coverage constraints
        for (const std::string& flight_id : covered_flights) {
            // Constraint exists, update constraint coefficient directly
            model_->chgCoeff(flight_constrs_[flight_id], new_var, 1.0);
            
            // Record which variable covers this flight
            flight_coverage_[flight_id].push_back(col_idx);
        }
        
        // Update crew resource constraints
        auto crew_constr_it = crew_constrs_.find(crew_id);
        if (crew_constr_it == crew_constrs_.end()) {
            // If constraint does not exist, create new constraint: sum(x_jk) <= 1
            GRBLinExpr expr = 0;
            expr += new_var;
            GRBConstr constr = model_->addConstr(expr <= 1.0, "crew_" + crew_id);
            crew_constrs_[crew_id] = constr;
        } else {
            // If constraint exists, update constraint coefficient directly
            model_->chgCoeff(crew_constr_it->second, new_var, 1.0);
        }
        
        // Record which variable uses this crew
        crew_usage_[crew_id].push_back(col_idx);
        
        // Save variable and corresponding info
        pairing_vars_.push_back(new_var);
        pairing_info_.push_back({crew_id, fdp});
        
        // Add column hash to added columns set
        added_columns_.insert(generateColumnHash(crew_id, fdp));

        // Add column info to column management system
        columns_.emplace_back(col_idx, crew_id, fdp);
        
        // If active columns exceed limit, trigger column management
        if (columns_.size() - column_pool_.size() > MAX_ACTIVE_COLUMNS) {
            deactivateColumns();
        }
        
        // Update model
        model_->update();
        return 1;
    } catch (GRBException& e) {
        std::cerr << "Error adding column: " << e.getMessage() << std::endl;
        throw;
    }
    return -1;
}

bool MasterProblem::columnExists(const std::string& crew_id, const FDP& fdp) const {
    // Generate column hash and check if it exists
    std::string column_hash = generateColumnHash(crew_id, fdp);
    return added_columns_.find(column_hash) != added_columns_.end();
}

std::string MasterProblem::generateColumnHash(const std::string& crew_id, const FDP& fdp) const {
    // Generate a hash string uniquely identifying the column
    std::stringstream ss;
    
    // Add crew ID
    ss << "crew:" << crew_id << ";";
    
    // Add all flight IDs in FDP (sorted alphabetically to ensure consistency)
    auto flight_ids = getFlightsFromFDP(fdp);
    std::sort(flight_ids.begin(), flight_ids.end());
    
    ss << "flights:";
    for (const auto& flight_id : flight_ids) {
        ss << flight_id << ",";
    }
    
    return ss.str();
}

std::vector<std::string> MasterProblem::getFlightsFromFDP(const FDP& fdp) const {
    // Extract all flight IDs from FDP
    std::vector<std::string> flight_ids;
    auto included_flights = fdp.get_included_flight_ids();
    flight_ids.reserve(included_flights.size());
    
    for (const auto& flight_id : included_flights) {
        flight_ids.push_back(flight_id);
    }
    
    return flight_ids;
}

double MasterProblem::getObjectiveValue() const {
    try {
        if (model_->get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
            return model_->get(GRB_DoubleAttr_ObjVal);
        }
        return 0.0;
    } catch (GRBException& e) {
        std::cerr << "Error getting objective value: " << e.getMessage() << std::endl;
        return 0.0;
    }
}

bool MasterProblem::isConverged() const {
    return converged_;
}

void MasterProblem::printSolution() const {
    try {
        // if (model_->get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
            std::cout << "===== Final Scheduling Plan =====" << std::endl;
            std::cout << "Covered Flights: " << getObjectiveValue() << std::endl;
            
            // Output schedule for each crew
            std::map<std::string, std::vector<FDP>> crew_assignments;
            
            for (size_t i = 0; i < pairing_vars_.size(); ++i) {
                if (pairing_vars_[i].get(GRB_DoubleAttr_X) > 0.5) {  // If variable is selected
                    const auto& [crew_id, fdp] = pairing_info_[i];
                    crew_assignments[crew_id].push_back(fdp);
                }
            }
            
            // Count uncovered flights
            std::set<std::string> uncovered_flights;
            for (const auto& [flight_id, var] : flight_vars_) {
                if (var.get(GRB_DoubleAttr_X) < 0.5) {  // If flight is not covered
                    uncovered_flights.insert(flight_id);
                }
            }
            
            // Process positioning tasks from crew initial station to base
            std::cout << "\nProcessing positioning tasks from crew initial station to base..." << std::endl;
            for (auto& [crew_id, fdps] : crew_assignments) {
                // Get crew info
                const Crew* crew = data_.get_crew(crew_id);
                if (!crew) continue;
                
                std::string base = crew->base;
                std::string initial_station = crew->initial_station;
                
                // If initial station and base are the same, skip
                if (initial_station == base) continue;
                
                // Check if there are ground duties
                if (crew->ground_duties.empty()) continue;
                
                // Find the earliest ground duty
                auto earliest_duty = std::min_element(crew->ground_duties.begin(), crew->ground_duties.end(),
                    [](const GroundDuty& a, const GroundDuty& b) {
                        return a.start_time < b.start_time;
                    });
                
               auto fdp = std::min_element(fdps.begin(), fdps.end(),
                    [](const FDP& a, const FDP& b) {
                        return a.get_start_time() < b.get_start_time();
                    });
                    
                // If start time of first task in FDP is later than start time of earliest ground duty, need to add positioning
                if (fdp->get_start_time() > earliest_duty->start_time) {
                    // Find bus positioning task from initial station to base
                    const auto& all_buses = data_.get_all_buses();
                    Task best_positioning_task;
                    bool found_positioning = false;
                    
                    for (const auto& [bus_id, bus] : all_buses) {
                        // Check if it is a bus task, and from initial station to base
                        if (bus.depa_airport == initial_station && bus.arri_airport == base) {
                            // Check if end time is earlier than start time of earliest ground duty
                            if (bus.ta <= earliest_duty->start_time) {
                                // Create positioning task
                                Task positioning_task{
                                    bus.id, "bus", bus.depa_airport, bus.arri_airport,
                                    bus.td, bus.ta, std::chrono::minutes(0), ""
                                };
                                
                                // If positioning task not found yet, or this task is better (ends later)
                                if (!found_positioning || positioning_task.end_time > best_positioning_task.end_time) {
                                    best_positioning_task = positioning_task;
                                    found_positioning = true;
                                }
                            }
                        }
                    }
                    
                    // If suitable positioning task found, add to beginning of FDP
                    if (found_positioning) {
                        std::vector<Task> new_tasks;
                        new_tasks.push_back(best_positioning_task);
                        new_tasks.insert(new_tasks.end(), fdp->tasks.begin(), fdp->tasks.end());
                        fdp->tasks = new_tasks;
                        
                        std::cout << "Added positioning task from " 
                                    << initial_station << " to " << base << " for crew " << crew_id << ": " 
                                    << best_positioning_task.id << std::endl;
                    }
                }
            }
            
            // // Output schedule for each crew
            // std::cout << "\nCrew Schedule Details:" << std::endl;
            // for (const auto& [crew_id, fdps] : crew_assignments) {
            //     std::cout << "Schedule for Crew " << crew_id << ":" << std::endl;
            //     for (const auto& fdp : fdps) {
            //         std::cout << "  " << fdp.to_string() << std::endl;
            //     }
            //     std::cout << std::endl;
            // }
            
            // // Output uncovered flights
            // std::cout << "\nUncovered Flights (" << uncovered_flights.size() << ") :" << std::endl;
            // for (const auto& flight_id : uncovered_flights) {
            //     std::cout << "  " << flight_id << std::endl;
            // }

            // Save result to CSV file
        
            std::string report_path = "exact_solver/report/" + data_version_ + "/rosterResult.csv";
            std::filesystem::create_directories("exact_solver/report/" + data_version_);
            std::ofstream out_file(report_path);
            if (!out_file) {
                throw std::runtime_error("Cannot create output file");
            }

            // Write CSV header
            out_file << "crewId,taskId,isDDH\n";

            // Write tasks for each crew
            for (const auto& [crew_id, fdps] : crew_assignments) {
                for (const auto& fdp : fdps) {
                    for (const auto& task : fdp.tasks) {
                        // isDDH=1 indicates bus task or positioning flight
                        int is_ddh = (task.task_type == "bus" || task.task_type.find("positioning") != std::string::npos) ? 1 : 0;
                        out_file << crew_id << "," << task.id << "," << is_ddh << "\n";
                    }
                }
            }

            out_file.close();
            std::cout << "\nResult saved to " << report_path << std::endl;

            // Generate readable report
            ReportGenerator::generate_readable_report(crew_assignments, "solution_report.txt", uncovered_flights);

        // } else {
        //     std::cout << "Model did not reach optimal solution, cannot output plan" << std::endl;
        // }
    } catch (GRBException& e) {
        std::cerr << "Error outputting solution: " << e.getMessage() << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Error saving file: " << e.what() << std::endl;
    }
}

void MasterProblem::convertToIntegerProgram() {
    try {
        // Convert all flight pairing assignment variables to binary variables
        for (auto& var : pairing_vars_) {
            var.set(GRB_CharAttr_VType, GRB_BINARY);
        }
        
        // Convert all flight coverage variables to binary variables
        for (auto& [flight_id, var] : flight_vars_) {
            var.set(GRB_CharAttr_VType, GRB_BINARY);
        }
        
        // Update model to apply changes
        model_->update();
        
        // Set solve time limit (e.g., 7200 seconds, i.e., 2 hours)
        model_->set(GRB_DoubleParam_TimeLimit, 900);
        
        // Set MIP Gap (e.g., 0.01, i.e., 1%)
        // model_->set(GRB_DoubleParam_MIPGap, 0.01);
        
        // Enable Gurobi output
        model_->set(GRB_IntParam_OutputFlag, 1);
        
    } catch (GRBException& e) {
        std::cerr << "Error converting to Integer Program: " << e.getMessage() << std::endl;
        throw;
    }
}

void MasterProblem::solveIntegerProgram() {
    try {
        std::cout << "\nStarting conversion to Integer Program..." << std::endl;
        convertToIntegerProgram();
        
        std::cout << "Starting to solve Integer Program..." << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Solve Integer Program
        model_->optimize();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
        
        // Check solve status
        int status = model_->get(GRB_IntAttr_Status);
        if (status == GRB_OPTIMAL) {
            std::cout << "Optimal solution found!" << std::endl;
        } else if (status == GRB_TIME_LIMIT) {
            std::cout << "Time limit reached, returning current best solution." << std::endl;
        } else {
            std::cout << "Solve status: " << status << std::endl;
        }
        
        // Output solve information
        double obj_val = model_->get(GRB_DoubleAttr_ObjVal);
        double best_bound = model_->get(GRB_DoubleAttr_ObjBound);
        double gap = model_->get(GRB_DoubleAttr_MIPGap);
        
        std::cout << "\n===== Integer Program Solve Results =====" << std::endl;
        std::cout << "Objective Value: " << obj_val << std::endl;
        std::cout << "Best Bound: " << best_bound << std::endl;
        std::cout << "Gap: " << (gap * 100) << "%" << std::endl;
        std::cout << "Solve Time: " << duration << " seconds" << std::endl;
        
    } catch (GRBException& e) {
        std::cerr << "Error solving Integer Program: " << e.getMessage() << std::endl;
        throw;
    }
}

void MasterProblem::updateColumnStatus() {
    // Check if there are any active columns
    bool has_active_columns = false;
    for (const auto& col : columns_) {
        if (col.is_active) {
            has_active_columns = true;
            break;
        }
    }

    // If no active columns, reactivate all columns
    if (!has_active_columns) {
        for (auto& col : columns_) {
            col.is_active = true;
            col.zero_value_count = 0;
            col.age = 0;
            pairing_vars_[col.index].set(GRB_DoubleAttr_UB, 1.0);
        }
        column_pool_.clear();
        model_->update();
        return;
    }

    // Update status info for all columns
    for (auto& col : columns_) {
        if (!col.is_active) continue;  // Skip inactive columns

        // Get current solution value of column
        double value = pairing_vars_[col.index].get(GRB_DoubleAttr_X);
        
        // Update consecutive zero value count
        if (value < 1e-6) {
            col.zero_value_count++;
        } else {
            col.zero_value_count = 0;
            col.age = 0;  // Reset age since column was used
        }

        // Update age
        col.age++;

        // Calculate and update reduced cost
        col.last_reduced_cost = calculateReducedCost(col);
    }
}

double MasterProblem::calculateReducedCost(const ColumnInfo& col) const {
    double reduced_cost = 0.0;
    
    // Get flights covered by this column
    const auto& fdp = col.fdp;
    auto covered_flights = fdp.get_included_flight_ids();
    
    // Calculate reduced cost: sum(π_i) - ρ_k
    for (const auto& flight_id : covered_flights) {
        auto it = flight_duals_.find(flight_id);
        if (it != flight_duals_.end()) {
            reduced_cost += it->second;
        }
    }
    
    // Subtract crew dual value
    auto it_crew = crew_duals_.find(col.crew_id);
    if (it_crew != crew_duals_.end()) {
        reduced_cost -= it_crew->second;
    }
    
    return reduced_cost;
}

void MasterProblem::manageColumns() {
    // Try to reactivate columns every REACTIVATION_INTERVAL iterations
    if (iteration_count_ % REACTIVATION_INTERVAL == 0) {
        reactivateColumns();
    }
    
    // Check if some columns need to be deactivated
    deactivateColumns();
}

void MasterProblem::deactivateColumns() {
    // Calculate current number of active columns
    int active_count = 0;
    for (const auto& col : columns_) {
        if (col.is_active) active_count++;
    }

    // If active columns are already few, do not deactivate
    if (active_count < MAX_ACTIVE_COLUMNS / 2) {
        return;
    }

    for (auto& col : columns_) {
        if (!col.is_active) continue;  // Skip already inactive columns
        
        // Check if deactivation conditions are met
        bool should_deactivate = false;
        
        // Condition 1: Consecutive zero values and poor reduced cost
        if (col.zero_value_count >= MAX_ZERO_VALUE_COUNT && 
            col.last_reduced_cost < REDUCED_COST_THRESHOLD) {
            should_deactivate = true;
        }
        
        // Condition 2: Age is too high, never used, and poor reduced cost
        if (col.age >= MAX_AGE && col.zero_value_count == col.age && 
            col.last_reduced_cost < 0) {
            should_deactivate = true;
        }
        
        if (should_deactivate) {
            // Move column to pool
            col.is_active = false;
            column_pool_.push_back(col.index);
            
            // Set variable upper bound to 0 in Gurobi model, effectively removing it from problem
            pairing_vars_[col.index].set(GRB_DoubleAttr_UB, 0.0);
        }
    }
    model_->update();
}

void MasterProblem::reactivateColumns() {
    // Re-evaluate columns in pool using current dual values
    std::vector<int> to_reactivate;
    
    for (auto it = column_pool_.begin(); it != column_pool_.end();) {
        auto& col = columns_[*it];
        
        // Calculate reduced cost
        double reduced_cost = calculateReducedCost(col);
        
        // If reduced cost improves, consider reactivating
        if (reduced_cost > 0) {
            col.is_active = true;
            col.zero_value_count = 0;  // Reset counter
            col.age = 0;               // Reset age
            col.last_reduced_cost = reduced_cost;
            
            // Restore variable upper bound
            pairing_vars_[col.index].set(GRB_DoubleAttr_UB, 1.0);
            
            // Remove from pool
            it = column_pool_.erase(it);
        } else {
            ++it;
        }
    }
    model_->update();
}
