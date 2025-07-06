#include "MasterProblem.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <functional>
#include <fstream>
#include <chrono>
#include "../data_model/ReportGenerator.hpp"

MasterProblem::MasterProblem(const SchedulingData& data)
    : data_(data), env_(), iteration_count_(0), converged_(false) {
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

void MasterProblem::initialize() {
    try {
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
            
            iteration_count_++;
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
            // std::cout << "列已存在，跳过添加" << std::endl;
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
            std::ofstream out_file("submission.csv");
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
            std::cout << "\n结果已保存到 submission.csv" << std::endl;

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
        model_->set(GRB_DoubleParam_TimeLimit, 300);
        
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