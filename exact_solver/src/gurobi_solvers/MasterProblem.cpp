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
        // 设置Gurobi环境
        env_.set(GRB_IntParam_OutputFlag, 0); // 禁用Gurobi输出，可根据需要调整
        model_ = std::make_unique<GRBModel>(env_);
    } catch (GRBException& e) {
        std::cerr << "Gurobi错误: " << e.getMessage() << std::endl;
        throw;
    }
}

MasterProblem::~MasterProblem() {
    // 智能指针会自动释放model_
}

std::string MasterProblem::getMPSFilePath() const {
    // 创建MPS文件目录
    std::filesystem::path mps_dir = "exact_solver/mps/" + data_version_;
    std::filesystem::create_directories(mps_dir);
    
    // 返回当前迭代的MPS文件路径
    return (mps_dir / ("master2.mps")).string();
}

void MasterProblem::exportMPSFile() const {
    try {
        std::string mps_path = getMPSFilePath();
        model_->write(mps_path);
        std::cout << "已导出MPS文件: " << mps_path << std::endl;
    } catch (GRBException& e) {
        std::cerr << "导出MPS文件时出错: " << e.getMessage() << std::endl;
    }
}

bool MasterProblem::tryLoadFromMPSFile() {
    try {
        std::string mps_path = getMPSFilePath();
        if (std::filesystem::exists(mps_path)) {
            model_ = std::make_unique<GRBModel>(env_, mps_path);
            std::cout << "已从MPS文件加载模型: " << mps_path << std::endl;
            return true;
        }
    } catch (GRBException& e) {
        std::cerr << "加载MPS文件时出错: " << e.getMessage() << std::endl;
    }
    return false;
}

void MasterProblem::initialize() {
    try {
        // 尝试从MPS文件加载模型
        if (tryLoadFromMPSFile()) {
            // 清空列管理相关的数据结构
            columns_.clear();
            column_pool_.clear();

            // 关联变量和约束
            // 1. 关联航班覆盖变量
            for (const auto& [flight_id, flight] : data_.get_all_flights()) {
                GRBVar var = model_->getVarByName("y_" + flight_id);
                if (var.get(GRB_StringAttr_VarName) != "") {
                    flight_vars_[flight_id] = var;
                }
            }

            // 2. 关联航班覆盖约束
            for (const auto& [flight_id, flight] : data_.get_all_flights()) {
                GRBConstr constr = model_->getConstrByName("cover_" + flight_id);
                if (constr.get(GRB_StringAttr_ConstrName) != "") {
                    flight_constrs_[flight_id] = constr;
                }
            }

            // 3. 关联机长资源约束（只关联已存在的约束）
            for (int i = 0; i < model_->get(GRB_IntAttr_NumConstrs); i++) {
                GRBConstr constr = model_->getConstr(i);
                std::string constr_name = constr.get(GRB_StringAttr_ConstrName);
                if (constr_name.substr(0, 5) == "crew_") {
                    std::string crew_id = constr_name.substr(5);
                    crew_constrs_[crew_id] = constr;
                }
            }

            // 先优化模型以获取初始解
            model_->optimize();

            // 4. 关联飞行周期变量并重建pairing_info_和列管理系统
            int var_idx = 0;
            for (int i = 0; i < model_->get(GRB_IntAttr_NumVars); i++) {
                GRBVar var = model_->getVar(i);
                std::string var_name = var.get(GRB_StringAttr_VarName);
                if (var_name.substr(0, 2) == "x_") {
                    // 解析变量名以获取机长ID
                    size_t first_underscore = var_name.find('_');
                    size_t second_underscore = var_name.find('_', first_underscore + 1);
                    if (second_underscore != std::string::npos) {
                        std::string crew_id = var_name.substr(second_underscore + 1);
                        pairing_vars_.push_back(var);
                        
                        // 更新机长使用记录
                        crew_usage_[crew_id].push_back(var_idx);
                        
                        // 收集该变量覆盖的航班
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

                        // 根据覆盖的航班重建FDP
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

                        // 按时间排序任务
                        std::sort(reconstructed_fdp.tasks.begin(), reconstructed_fdp.tasks.end(),
                            [](const Task& a, const Task& b) {
                                return a.start_time < b.start_time;
                            });

                        // 更新pairing_info_
                        pairing_info_.push_back({crew_id, reconstructed_fdp});

                        // 更新added_columns_
                        added_columns_.insert(generateColumnHash(crew_id, reconstructed_fdp));

                        // 初始化列信息
                        ColumnInfo col_info(var_idx, crew_id, reconstructed_fdp);
                        
                        // 获取当前解值，初始化列状态
                        if (model_->get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
                            double value = var.get(GRB_DoubleAttr_X);
                            if (value < 1e-6) {
                                col_info.zero_value_count = 1;
                            }
                        }
                        
                        // 获取变量上界
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

        // 如果没有MPS文件，创建新模型
        // 创建航班覆盖变量 y_i
        for (const auto& [flight_id, flight] : data_.get_all_flights()) {
            flight_vars_[flight_id] = model_->addVar(0.0, 1.0, 1.0, GRB_CONTINUOUS, "y_" + flight_id);
        }

        // 设置目标函数：最大化覆盖的航班数
        GRBLinExpr obj = 0;
        for (const auto& [flight_id, var] : flight_vars_) {
            obj += var;
        }
        model_->setObjective(obj, GRB_MAXIMIZE);

        // 初始化时添加 0 >= y_i 约束
        for (const auto& [flight_id, var] : flight_vars_) {
            flight_constrs_[flight_id] = model_->addConstr(0.0 == var, "cover_" + flight_id);
        }
        
        // 更新模型以包含新变量
        model_->update();

        // 导出初始MPS文件
        exportMPSFile();
    } catch (GRBException& e) {
        std::cerr << "初始化主问题时出错: " << e.getMessage() << std::endl;
        throw;
    }
}

void MasterProblem::solve() {
    try {

        // 求解原始整数模型
        model_->optimize();

        // 检查LP求解状态
        if (model_->get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
            // 更新对偶值
            flight_duals_.clear();
            crew_duals_.clear();
            
            // 获取航班覆盖约束的对偶值
            for (const auto& [flight_id, constr] : flight_constrs_) {
                flight_duals_[flight_id] = constr.get(GRB_DoubleAttr_Pi);
            }
            
            // 获取机长资源约束的对偶值
            for (const auto& [crew_id, constr] : crew_constrs_) {
                crew_duals_[crew_id] = constr.get(GRB_DoubleAttr_Pi);
            }

            //  // 更新列状态并进行列管理
            // updateColumnStatus();
            // manageColumns();
            
            iteration_count_++;
            
            // 导出当前迭代的MPS文件
            exportMPSFile();
        } else {
            std::cerr << "LP松弛求解未达到最优状态: " << model_->get(GRB_IntAttr_Status) << std::endl;
            // 如果LP松弛不可行，设置所有对偶值为0
            for (const auto& [flight_id, _] : flight_constrs_) {
                flight_duals_[flight_id] = 0.0;
            }
            
            for (const auto& [crew_id, _] : crew_constrs_) {
                crew_duals_[crew_id] = 0.0;
            }
        }
    } catch (GRBException& e) {
        std::cerr << "求解主问题时出错: " << e.getMessage() << std::endl;
        throw;
    }
}

double MasterProblem::getFlightDual(const std::string& flight_id) const {
    auto it = flight_duals_.find(flight_id);
    if (it != flight_duals_.end()) {
        return it->second;
    }
    return 0.0; // 如果找不到，返回0
}

double MasterProblem::getCrewDual(const std::string& crew_id) const {
    auto it = crew_duals_.find(crew_id);
    if (it != crew_duals_.end()) {
        return it->second;
    }
    return 0.0; // 如果找不到，返回0
}

int MasterProblem::addColumn(const std::string& crew_id, const FDP& fdp) {
    try {
        // 检查列是否已存在
        if (columnExists(crew_id, fdp)) {
            return -1;
        }
        
        // 获取该FDP覆盖的航班
        std::vector<std::string> covered_flights = getFlightsFromFDP(fdp);
        
        // 如果FDP不覆盖任何航班，跳过添加
        if (covered_flights.empty()) {
            return -1;
        }
        
        // 创建新的飞行周期分配变量 x_jk
        int col_idx = pairing_vars_.size();
        std::string var_name = "x_" + std::to_string(col_idx) + "_" + crew_id;
        GRBVar new_var = model_->addVar(0.0, 1.0, 0.0, GRB_CONTINUOUS, var_name);
        
        // 更新航班覆盖约束
        for (const std::string& flight_id : covered_flights) {
            // 约束已存在，直接更新约束系数
            model_->chgCoeff(flight_constrs_[flight_id], new_var, 1.0);
            
            // 记录该航班被哪个变量覆盖
            flight_coverage_[flight_id].push_back(col_idx);
        }
        
        // 更新机长资源约束
        auto crew_constr_it = crew_constrs_.find(crew_id);
        if (crew_constr_it == crew_constrs_.end()) {
            // 如果约束不存在，创建新约束: sum(x_jk) <= 1
            GRBLinExpr expr = 0;
            expr += new_var;
            GRBConstr constr = model_->addConstr(expr <= 1.0, "crew_" + crew_id);
            crew_constrs_[crew_id] = constr;
        } else {
            // 如果约束已存在，直接更新约束系数
            model_->chgCoeff(crew_constr_it->second, new_var, 1.0);
        }
        
        // 记录该机长被哪个变量使用
        crew_usage_[crew_id].push_back(col_idx);
        
        // 保存变量和对应的信息
        pairing_vars_.push_back(new_var);
        pairing_info_.push_back({crew_id, fdp});
        
        // 将列的哈希值添加到已添加列集合中
        added_columns_.insert(generateColumnHash(crew_id, fdp));

        // 添加列信息到列管理系统
        columns_.emplace_back(col_idx, crew_id, fdp);
        
        // 如果活跃列数量超过限制，触发列管理
        if (columns_.size() - column_pool_.size() > MAX_ACTIVE_COLUMNS) {
            deactivateColumns();
        }
        
        // 更新模型
        model_->update();
        return 1;
    } catch (GRBException& e) {
        std::cerr << "添加列时出错: " << e.getMessage() << std::endl;
        throw;
    }
    return -1;
}

bool MasterProblem::columnExists(const std::string& crew_id, const FDP& fdp) const {
    // 生成列的哈希值并检查是否已存在
    std::string column_hash = generateColumnHash(crew_id, fdp);
    return added_columns_.find(column_hash) != added_columns_.end();
}

std::string MasterProblem::generateColumnHash(const std::string& crew_id, const FDP& fdp) const {
    // 生成一个唯一标识列的哈希字符串
    std::stringstream ss;
    
    // 添加机长ID
    ss << "crew:" << crew_id << ";";
    
    // 添加FDP中的所有航班ID（按字母顺序排序以确保一致性）
    auto flight_ids = getFlightsFromFDP(fdp);
    std::sort(flight_ids.begin(), flight_ids.end());
    
    ss << "flights:";
    for (const auto& flight_id : flight_ids) {
        ss << flight_id << ",";
    }
    
    return ss.str();
}

std::vector<std::string> MasterProblem::getFlightsFromFDP(const FDP& fdp) const {
    // 从FDP中提取所有航班ID
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
        std::cerr << "获取目标值时出错: " << e.getMessage() << std::endl;
        return 0.0;
    }
}

bool MasterProblem::isConverged() const {
    return converged_;
}

void MasterProblem::printSolution() const {
    try {
        // if (model_->get(GRB_IntAttr_Status) == GRB_OPTIMAL) {
            std::cout << "===== 最终排班方案 =====" << std::endl;
            std::cout << "覆盖的航班数: " << getObjectiveValue() << std::endl;
            
            // 输出每个机长的排班
            std::map<std::string, std::vector<FDP>> crew_assignments;
            
            for (size_t i = 0; i < pairing_vars_.size(); ++i) {
                if (pairing_vars_[i].get(GRB_DoubleAttr_X) > 0.5) {  // 如果变量被选中
                    const auto& [crew_id, fdp] = pairing_info_[i];
                    crew_assignments[crew_id].push_back(fdp);
                }
            }
            
            // 统计未覆盖的航班
            std::set<std::string> uncovered_flights;
            for (const auto& [flight_id, var] : flight_vars_) {
                if (var.get(GRB_DoubleAttr_X) < 0.5) {  // 如果航班未被覆盖
                    uncovered_flights.insert(flight_id);
                }
            }
            
            // 处理机组起始机场到base机场的置位任务
            std::cout << "\n处理机组起始机场到base机场的置位任务..." << std::endl;
            for (auto& [crew_id, fdps] : crew_assignments) {
                // 获取机组信息
                const Crew* crew = data_.get_crew(crew_id);
                if (!crew) continue;
                
                std::string base = crew->base;
                std::string initial_station = crew->initial_station;
                
                // 如果起始机场和base机场一样，跳过
                if (initial_station == base) continue;
                
                // 检查是否有占位任务
                if (crew->ground_duties.empty()) continue;
                
                // 找到最早的占位任务
                auto earliest_duty = std::min_element(crew->ground_duties.begin(), crew->ground_duties.end(),
                    [](const GroundDuty& a, const GroundDuty& b) {
                        return a.start_time < b.start_time;
                    });
                
               auto fdp = std::min_element(fdps.begin(), fdps.end(),
                    [](const FDP& a, const FDP& b) {
                        return a.get_start_time() < b.get_start_time();
                    });
                    
                // 如果FDP的首个任务开始时间晚于最早占位任务的开始时间，需要添加置位
                if (fdp->get_start_time() > earliest_duty->start_time) {
                    // 寻找从起始机场到base机场的bus置位任务
                    const auto& all_buses = data_.get_all_buses();
                    Task best_positioning_task;
                    bool found_positioning = false;
                    
                    for (const auto& [bus_id, bus] : all_buses) {
                        // 检查是否是bus任务，且从起始机场到base机场
                        if (bus.depa_airport == initial_station && bus.arri_airport == base) {
                            // 检查结束时间是否早于最早占位任务的开始时间
                            if (bus.ta <= earliest_duty->start_time) {
                                // 创建置位任务
                                Task positioning_task{
                                    bus.id, "bus", bus.depa_airport, bus.arri_airport,
                                    bus.td, bus.ta, std::chrono::minutes(0), ""
                                };
                                
                                // 如果还没找到置位任务，或者这个任务比之前的更好（结束时间更晚）
                                if (!found_positioning || positioning_task.end_time > best_positioning_task.end_time) {
                                    best_positioning_task = positioning_task;
                                    found_positioning = true;
                                }
                            }
                        }
                    }
                    
                    // 如果找到了合适的置位任务，添加到FDP开头
                    if (found_positioning) {
                        std::vector<Task> new_tasks;
                        new_tasks.push_back(best_positioning_task);
                        new_tasks.insert(new_tasks.end(), fdp->tasks.begin(), fdp->tasks.end());
                        fdp->tasks = new_tasks;
                        
                        std::cout << "为机组 " << crew_id << " 的FDP添加了从 " 
                                    << initial_station << " 到 " << base << " 的置位任务: " 
                                    << best_positioning_task.id << std::endl;
                    }
                }
            }
            
            // // 输出每个机长的排班
            // std::cout << "\n机长排班详情:" << std::endl;
            // for (const auto& [crew_id, fdps] : crew_assignments) {
            //     std::cout << "机长 " << crew_id << " 的排班:" << std::endl;
            //     for (const auto& fdp : fdps) {
            //         std::cout << "  " << fdp.to_string() << std::endl;
            //     }
            //     std::cout << std::endl;
            // }
            
            // // 输出未覆盖的航班
            // std::cout << "\n未覆盖的航班 (" << uncovered_flights.size() << "):" << std::endl;
            // for (const auto& flight_id : uncovered_flights) {
            //     std::cout << "  " << flight_id << std::endl;
            // }

            // 保存结果到CSV文件
        
            std::string report_path = "exact_solver/report/" + data_version_ + "/rosterResult.csv";
            std::filesystem::create_directories("exact_solver/report/" + data_version_);
            std::ofstream out_file(report_path);
            if (!out_file) {
                throw std::runtime_error("无法创建输出文件");
            }

            // 写入CSV头
            out_file << "crewId,taskId,isDDH\n";

            // 写入每个机长的任务
            for (const auto& [crew_id, fdps] : crew_assignments) {
                for (const auto& fdp : fdps) {
                    for (const auto& task : fdp.tasks) {
                        // isDDH为1表示是巴士任务或定位飞行
                        int is_ddh = (task.task_type == "bus" || task.task_type.find("positioning") != std::string::npos) ? 1 : 0;
                        out_file << crew_id << "," << task.id << "," << is_ddh << "\n";
                    }
                }
            }

            out_file.close();
            std::cout << "\n结果已保存到 exact_solver/report/" + data_version_ + "/rosterResult.csv" << std::endl;

            // 生成可读报告
            ReportGenerator::generate_readable_report(crew_assignments, "solution_report.txt", uncovered_flights);

        // } else {
        //     std::cout << "模型未达到最优解，无法输出方案" << std::endl;
        // }
    } catch (GRBException& e) {
        std::cerr << "输出解决方案时出错: " << e.getMessage() << std::endl;
    } catch (std::exception& e) {
        std::cerr << "保存文件时出错: " << e.what() << std::endl;
    }
}

void MasterProblem::convertToIntegerProgram() {
    try {
        // 将所有飞行周期分配变量转换为二进制变量
        for (auto& var : pairing_vars_) {
            var.set(GRB_CharAttr_VType, GRB_BINARY);
        }
        
        // 将所有航班覆盖变量转换为二进制变量
        for (auto& [flight_id, var] : flight_vars_) {
            var.set(GRB_CharAttr_VType, GRB_BINARY);
        }
        
        // 更新模型以应用变更
        model_->update();
        
        // 设置求解时间限制（例如7200秒，即2小时）
        model_->set(GRB_DoubleParam_TimeLimit, 900);
        
        // 设置MIP Gap（例如0.01，即1%）
        model_->set(GRB_DoubleParam_MIPGap, 0.01);
        
        // 启用Gurobi输出
        model_->set(GRB_IntParam_OutputFlag, 1);
        
    } catch (GRBException& e) {
        std::cerr << "转换为整数规划时出错: " << e.getMessage() << std::endl;
        throw;
    }
}

void MasterProblem::solveIntegerProgram() {
    try {
        std::cout << "\n开始转换为整数规划..." << std::endl;
        convertToIntegerProgram();
        
        std::cout << "开始求解整数规划..." << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 求解整数规划
        model_->optimize();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
        
        // 检查求解状态
        int status = model_->get(GRB_IntAttr_Status);
        if (status == GRB_OPTIMAL) {
            std::cout << "找到最优解！" << std::endl;
        } else if (status == GRB_TIME_LIMIT) {
            std::cout << "达到时间限制，返回当前最优解。" << std::endl;
        } else {
            std::cout << "求解状态: " << status << std::endl;
        }
        
        // 输出求解信息
        double obj_val = model_->get(GRB_DoubleAttr_ObjVal);
        double best_bound = model_->get(GRB_DoubleAttr_ObjBound);
        double gap = model_->get(GRB_DoubleAttr_MIPGap);
        
        std::cout << "\n===== 整数规划求解结果 =====" << std::endl;
        std::cout << "目标值: " << obj_val << std::endl;
        std::cout << "最优界: " << best_bound << std::endl;
        std::cout << "Gap: " << (gap * 100) << "%" << std::endl;
        std::cout << "求解时间: " << duration << " 秒" << std::endl;
        
    } catch (GRBException& e) {
        std::cerr << "求解整数规划时出错: " << e.getMessage() << std::endl;
        throw;
    }
}

void MasterProblem::updateColumnStatus() {
    // 检查是否有任何活跃列
    bool has_active_columns = false;
    for (const auto& col : columns_) {
        if (col.is_active) {
            has_active_columns = true;
            break;
        }
    }

    // 如果没有活跃列，重新激活所有列
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

    // 更新所有列的状态信息
    for (auto& col : columns_) {
        if (!col.is_active) continue;  // 跳过非活跃列

        // 获取列的当前解值
        double value = pairing_vars_[col.index].get(GRB_DoubleAttr_X);
        
        // 更新连续解值为0的计数
        if (value < 1e-6) {
            col.zero_value_count++;
        } else {
            col.zero_value_count = 0;
            col.age = 0;  // 重置年龄，因为列被使用了
        }

        // 更新年龄
        col.age++;

        // 计算并更新检验数
        col.last_reduced_cost = calculateReducedCost(col);
    }
}

double MasterProblem::calculateReducedCost(const ColumnInfo& col) const {
    double reduced_cost = 0.0;
    
    // 获取该列覆盖的航班
    const auto& fdp = col.fdp;
    auto covered_flights = fdp.get_included_flight_ids();
    
    // 计算检验数：sum(π_i) - ρ_k
    for (const auto& flight_id : covered_flights) {
        auto it = flight_duals_.find(flight_id);
        if (it != flight_duals_.end()) {
            reduced_cost += it->second;
        }
    }
    
    // 减去机长的对偶值
    auto it_crew = crew_duals_.find(col.crew_id);
    if (it_crew != crew_duals_.end()) {
        reduced_cost -= it_crew->second;
    }
    
    return reduced_cost;
}

void MasterProblem::manageColumns() {
    // 每隔REACTIVATION_INTERVAL次迭代，尝试重激活列
    if (iteration_count_ % REACTIVATION_INTERVAL == 0) {
        reactivateColumns();
    }
    
    // 检查是否需要停用一些列
    deactivateColumns();
}

void MasterProblem::deactivateColumns() {
    // 计算当前活跃列的数量
    int active_count = 0;
    for (const auto& col : columns_) {
        if (col.is_active) active_count++;
    }

    // 如果活跃列数量已经很少，不进行停用
    if (active_count < MAX_ACTIVE_COLUMNS / 2) {
        return;
    }

    for (auto& col : columns_) {
        if (!col.is_active) continue;  // 跳过已经非活跃的列
        
        // 检查是否满足停用条件
        bool should_deactivate = false;
        
        // 条件1：连续多次解值为0且检验数很差
        if (col.zero_value_count >= MAX_ZERO_VALUE_COUNT && 
            col.last_reduced_cost < REDUCED_COST_THRESHOLD) {
            should_deactivate = true;
        }
        
        // 条件2：年龄过大且从未被使用且检验数差
        if (col.age >= MAX_AGE && col.zero_value_count == col.age && 
            col.last_reduced_cost < 0) {
            should_deactivate = true;
        }
        
        if (should_deactivate) {
            // 将列移入列池
            col.is_active = false;
            column_pool_.push_back(col.index);
            
            // 在Gurobi模型中将变量的上界设为0，实际上将其从问题中移除
            pairing_vars_[col.index].set(GRB_DoubleAttr_UB, 0.0);
        }
    }
    model_->update();
}

void MasterProblem::reactivateColumns() {
    // 使用当前的对偶值重新评估列池中的列
    std::vector<int> to_reactivate;
    
    for (auto it = column_pool_.begin(); it != column_pool_.end();) {
        auto& col = columns_[*it];
        
        // 计算检验数
        double reduced_cost = calculateReducedCost(col);
        
        // 如果检验数变好了，考虑重新激活
        if (reduced_cost > 0) {
            col.is_active = true;
            col.zero_value_count = 0;  // 重置计数器
            col.age = 0;               // 重置年龄
            col.last_reduced_cost = reduced_cost;
            
            // 恢复变量的上界
            pairing_vars_[col.index].set(GRB_DoubleAttr_UB, 1.0);
            
            // 从列池中移除
            it = column_pool_.erase(it);
        } else {
            ++it;
        }
    }
    model_->update();
}