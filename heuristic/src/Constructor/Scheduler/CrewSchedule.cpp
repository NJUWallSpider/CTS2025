#include "CrewSchedule.hpp"
#include "../../Loader/Utils.h"
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <variant>
#include <map>
#include <limits>
#include <algorithm>
#include <numeric>
#include "../SolutionState.hpp"



CrewSchedule::CrewSchedule(const DataLoader& data, const Crew& crew, std::vector<DutyPeriod>& crew_duty_periods, std::map<std::string, std::vector<std::pair<std::string, bool>>>& flight_assignments,
std::vector<Cycle>& cycles, std::string start_str)
    : data_(data), crew_(crew), duty_periods_(crew_duty_periods), flight_assignments(flight_assignments), cycles_(cycles), start_str_(start_str) {
    
    for (const auto& [_, crew_data] : data.getCrews()) {
        all_bases_.insert(crew_data.base);
    }

    initialize_crew_state();
}


void CrewSchedule::construct_schedule_DFS() {

    
    /////////////////SEARCH/////////////////////
    //STEP 1. Find all possible next tasks (flights and buses) from the CURRENT airport and the last task end time
    std::vector<std::variant<Flight, Bus, GroundDuty>> candidates;
    int flight_count = 0;
    int bus_count = 0;
    for (const auto& flight : data_.getFlights()) {
        // if (flight.std >= last_task_end_time_ ) {
        if (flight.depaAirport == current_airport_ && flight.std >= last_task_end_time_ ) {
            candidates.emplace_back(flight);
            flight_count++;
        }
    }

    for (const auto& bus : data_.getBuses()) {
        if (bus.depaAirport == current_airport_ && bus.td >= last_task_end_time_) {
            candidates.emplace_back(bus);
            bus_count++;
        }
    }

    // for (const auto& ground_duty : crew_.groundDuties) {
    //     if ( ground_duty.start_time > last_task_end_time_) {
    //         candidates.emplace_back(ground_duty);
    //     }
    // }

    std::vector<std::string> layover_spots = data_.getLayoverStations();

    // bool maybe_the_last_flight = (duty_periods_.back().is_FDuty && duty_periods_.back().can_be_extended && duty_periods_.back().taskCount >= 2 );
    bool maybe_the_last_flight = false;
    // Sort candidates based on the new rules
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        
            bool a_is_flight = std::holds_alternative<Flight>(a);
            bool b_is_flight = std::holds_alternative<Flight>(b);

            // Prioritize flights that return to the crew's base
            if(maybe_the_last_flight){
                if (a_is_flight && b_is_flight) {
                    const auto& flight_a = std::get<Flight>(a);
                    const auto& flight_b = std::get<Flight>(b);
                    bool a_returns_to_base = (flight_a.arriAirport == crew_.base);
                    bool b_returns_to_base = (flight_b.arriAirport == crew_.base);
                    if (a_returns_to_base != b_returns_to_base) {
                        return a_returns_to_base;
                    }
                    bool a_is_layover = std::find(layover_spots.begin(), layover_spots.end(), flight_a.arriAirport) != layover_spots.end();
                    bool b_is_layover = std::find(layover_spots.begin(), layover_spots.end(), flight_b.arriAirport) != layover_spots.end();
                    if (a_is_layover != b_is_layover) {
                        return a_is_layover;
                    }
                }
            }

            if (a_is_flight != b_is_flight) return a_is_flight;

            bool a_is_bus = std::holds_alternative<Bus>(a);
            bool b_is_bus = std::holds_alternative<Bus>(b);

            if (a_is_bus && !b_is_bus) return true;  // a (bus) comes before b (flight)
            if (!a_is_bus && b_is_bus) return false; // b (bus) comes before a (flight)

            if (a_is_bus && b_is_bus) {
                const auto& bus_a = std::get<Bus>(a);
                const auto& bus_b = std::get<Bus>(b);
                bool a_to_hub = all_bases_.count(bus_a.arriAirport) > 0;
                bool b_to_hub = all_bases_.count(bus_b.arriAirport) > 0;
                if (a_to_hub != b_to_hub) {
                    return a_to_hub; // Prioritize hub destination
                }
            }
    
        
        return get_start_time(a) < get_start_time(b);
    });

    // 如果没有候选任务，检查当前路径是否为最佳路径
    if (candidates.empty()) {
        update_best_path();
        return;
    }

    // STEP2. 尝试前MAX_BRANCHES个有效的候选任务
    int valid_branches = 0;
    for (auto& candidate_task : candidates) {
        if (valid_branches >= MAX_BRANCHES) break;

        // 保存当前状态
        std::vector<DutyPeriod> backup_duty_periods = duty_periods_;
        std::vector<Cycle> backup_cycles = cycles_;
        std::string backup_current_airport = current_airport_;
        TimePoint backup_last_task_end_time = last_task_end_time_;

        // 检查操作有效性
        if (Check_Action_validity(candidate_task)) {
            Action(candidate_task);
            /////////// GLOBLE STATE ///////////
            // 1. update the last task end time
            last_task_end_time_ = get_end_time(duty_periods_.back().tasks.back());
            // 2. update the current airport
            current_airport_ = get_arrival_airport(duty_periods_.back().tasks.back());
            /////////// DUTY PERIOD STATE ///////////
            Update_DutyPeriod();
            
            if (Check_Cycle_validity()) {
                action_cycle();
                Update_Cycle();
                
                // 递归搜索下一层
                valid_branches++;
                construct_schedule_DFS();
                
                // 恢复状态以探索其他分支
                duty_periods_ = backup_duty_periods;
                cycles_ = backup_cycles;
                current_airport_ = backup_current_airport;
                last_task_end_time_ = backup_last_task_end_time;
            } else {
                // 如果操作无效，恢复状态
                duty_periods_ = backup_duty_periods;
                cycles_ = backup_cycles;
                current_airport_ = backup_current_airport;
                last_task_end_time_ = backup_last_task_end_time;
            }
        }
    }

    // 如果没有找到有效的分支，检查当前路径是否为最佳路径
    if (valid_branches == 0) {
        update_best_path();
    }
    
    return;
}

// 实现束搜索方法
void CrewSchedule::construct_schedule_beam_search() {
    // 创建优先队列，保存当前层的最佳路径
    std::priority_queue<PathState, std::vector<PathState>, PathComparator> beam;
    
    // 初始路径
    PathState initial_path;
    initial_path.duty_periods = duty_periods_;
    initial_path.cycles = cycles_;
    initial_path.current_airport = current_airport_;
    initial_path.last_task_end_time = last_task_end_time_;
    initial_path.total_flight_time = calculate_total_flight_time(duty_periods_);
    
    beam.push(initial_path);
    
    // 最大搜索深度限制
    for (int depth = 0; depth < MAX_DEPTH; depth++) {
        // 保存下一层的候选路径
        std::priority_queue<PathState, std::vector<PathState>, PathComparator> next_beam;
        
        // 处理当前层的每个路径
        int paths_processed = 0;
        while (!beam.empty() && paths_processed < BEAM_WIDTH) {
            PathState current_path = beam.top();
            beam.pop();
            paths_processed++;
            
            // 恢复当前路径的状态
            duty_periods_ = current_path.duty_periods;
            cycles_ = current_path.cycles;
            current_airport_ = current_path.current_airport;
            last_task_end_time_ = current_path.last_task_end_time;
            
            // 查找所有可能的下一个任务
            std::vector<std::variant<Flight, Bus, GroundDuty>> candidates;
            for (const auto& flight : data_.getFlights()) {
                if (flight.depaAirport == current_airport_ && flight.std >= last_task_end_time_) {
                    candidates.emplace_back(flight);
                }
            }
            
            for (const auto& bus : data_.getBuses()) {
                if (bus.depaAirport == current_airport_ && bus.td >= last_task_end_time_) {
                    candidates.emplace_back(bus);
                }
            }
            
            // 如果没有候选任务，将当前路径视为可能的最佳路径
            if (candidates.empty()) {
                if (current_path.total_flight_time > best_path_.total_flight_time) {
                    best_path_ = current_path;
                }
                continue;
            }
            
            // 对候选任务进行排序（与DFS中相同的排序规则）
            std::vector<std::string> layover_spots = data_.getLayoverStations();
            bool maybe_the_last_flight = false;
            
            std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
                bool a_is_flight = std::holds_alternative<Flight>(a);
                bool b_is_flight = std::holds_alternative<Flight>(b);

                if(maybe_the_last_flight){
                    if (a_is_flight && b_is_flight) {
                        const auto& flight_a = std::get<Flight>(a);
                        const auto& flight_b = std::get<Flight>(b);
                        bool a_returns_to_base = (flight_a.arriAirport == crew_.base);
                        bool b_returns_to_base = (flight_b.arriAirport == crew_.base);
                        if (a_returns_to_base != b_returns_to_base) {
                            return a_returns_to_base;
                        }
                        bool a_is_layover = std::find(layover_spots.begin(), layover_spots.end(), flight_a.arriAirport) != layover_spots.end();
                        bool b_is_layover = std::find(layover_spots.begin(), layover_spots.end(), flight_b.arriAirport) != layover_spots.end();
                        if (a_is_layover != b_is_layover) {
                            return a_is_layover;
                        }
                    }
                }

                if (a_is_flight != b_is_flight) return a_is_flight;

                bool a_is_bus = std::holds_alternative<Bus>(a);
                bool b_is_bus = std::holds_alternative<Bus>(b);

                if (a_is_bus && !b_is_bus) return true;
                if (!a_is_bus && b_is_bus) return false;

                if (a_is_bus && b_is_bus) {
                    const auto& bus_a = std::get<Bus>(a);
                    const auto& bus_b = std::get<Bus>(b);
                    bool a_to_hub = all_bases_.count(bus_a.arriAirport) > 0;
                    bool b_to_hub = all_bases_.count(bus_b.arriAirport) > 0;
                    if (a_to_hub != b_to_hub) {
                        return a_to_hub;
                    }
                }
                
                return get_start_time(a) < get_start_time(b);
            });
            
            // 尝试每个候选任务，最多尝试MAX_BRANCHES个
            int valid_branches = 0;
            for (auto& candidate_task : candidates) {
                if (valid_branches >= MAX_BRANCHES) break;
                
                // 保存当前状态
                std::vector<DutyPeriod> backup_duty_periods = duty_periods_;
                std::vector<Cycle> backup_cycles = cycles_;
                std::string backup_current_airport = current_airport_;
                TimePoint backup_last_task_end_time = last_task_end_time_;
                
                // 检查操作有效性
                if (Check_Action_validity(candidate_task)) {
                    Action(candidate_task);
                    // 更新全局状态
                    last_task_end_time_ = get_end_time(duty_periods_.back().tasks.back());
                    current_airport_ = get_arrival_airport(duty_periods_.back().tasks.back());
                    Update_DutyPeriod();
                    
                    if (Check_Cycle_validity()) {
                        action_cycle();
                        Update_Cycle();
                        
                        // 创建新路径状态
                        PathState new_path(duty_periods_, cycles_, current_airport_, 
                                          last_task_end_time_, calculate_total_flight_time(duty_periods_));
                        
                        // 添加到下一层的候选路径中
                        next_beam.push(new_path);
                        valid_branches++;
                        
                        // 如果当前路径比最佳路径更好，更新最佳路径
                        if (new_path.total_flight_time > best_path_.total_flight_time) {
                            best_path_ = new_path;
                        }
                    }
                    
                    // 恢复状态以尝试下一个候选任务
                    duty_periods_ = backup_duty_periods;
                    cycles_ = backup_cycles;
                    current_airport_ = backup_current_airport;
                    last_task_end_time_ = backup_last_task_end_time;
                }
            }
            
            // 如果没有找到有效分支，当前路径可能是最佳路径
            if (valid_branches == 0 && current_path.total_flight_time > best_path_.total_flight_time) {
                best_path_ = current_path;
            }
        }
        
        // 如果下一层没有有效路径，搜索结束
        if (next_beam.empty()) {
            break;
        }
        
        // 更新当前层为下一层
        beam = next_beam;
    }
}

void CrewSchedule::assign_tasks_to_crew() {
    // 初始化最佳路径为空
    best_path_ = PathState();
    
    // 执行束搜索而非DFS
    construct_schedule_DFS();
    
    // 应用找到的最佳路径
    apply_best_path();
    
    // 确保最佳路径被反映到solution中
    // 由于duty_periods_和cycles_是引用，它们已经被apply_best_path更新
    // 但我们需要更新flight_assignments
    
    // 先清除该机组人员的所有航班分配
    std::vector<std::string> flights_to_remove;
    for(auto& [flight_id, assignments] : flight_assignments) {
        for(auto it = assignments.begin(); it != assignments.end();) {
            if(it->first == crew_.id) {
                it = assignments.erase(it);
            } else {
                ++it;
            }
        }
        
        // 如果该航班没有分配给任何机组人员，记录下来以便后续删除
        if(assignments.empty()) {
            flights_to_remove.push_back(flight_id);
        }
    }
    
    // 删除没有分配的航班
    for(const auto& flight_id : flights_to_remove) {
        flight_assignments.erase(flight_id);
    }
    
    // 重新添加最佳路径中的航班分配
    for(const auto& dp : duty_periods_) {
        for(const auto& task : dp.tasks) {
            if(std::holds_alternative<Flight>(task)) {
                const auto& flight = std::get<Flight>(task);
                bool is_qualified = crew_.qualifications.find(flight.id) != crew_.qualifications.end();
                flight_assignments[flight.id].push_back({crew_.id, is_qualified});
            }
        }
    }
}

// 计算路径的总飞行时间
std::chrono::minutes CrewSchedule::calculate_total_flight_time(const std::vector<DutyPeriod>& duty_periods) {
    std::chrono::minutes total_time(0);
    
    for (const auto& dp : duty_periods) {
        total_time += dp.total_flight_time;
    }
    
    return total_time;
}

// 更新最佳路径
void CrewSchedule::update_best_path() {
    std::chrono::minutes current_flight_time = calculate_total_flight_time(duty_periods_);
    
    // 如果当前路径的总飞行时间更长，或者最佳路径还未初始化
    if (best_path_.duty_periods.empty() || current_flight_time > best_path_.total_flight_time) {
        best_path_.duty_periods = duty_periods_;
        best_path_.cycles = cycles_;
        best_path_.current_airport = current_airport_;
        best_path_.last_task_end_time = last_task_end_time_;
        best_path_.total_flight_time = current_flight_time;
    }
}

// 应用最佳路径
void CrewSchedule::apply_best_path() {
    if (!best_path_.duty_periods.empty()) {
        duty_periods_ = best_path_.duty_periods;
        cycles_ = best_path_.cycles;
        current_airport_ = best_path_.current_airport;
        last_task_end_time_ = best_path_.last_task_end_time;
    }
}


void CrewSchedule::delete_redundant_buses(){
    // 如果没有duty periods，直接返回
    if (duty_periods_.empty()) {
        return;
    }
    
    // 从最后一个duty period开始逆序遍历
    for (int dp_idx = duty_periods_.size() - 1; dp_idx >= 0; --dp_idx) {
        auto& dp = duty_periods_[dp_idx];
        
        // 从最后一个任务开始逆序遍历
        for (int task_idx = dp.tasks.size() - 1; task_idx >= 0; --task_idx) {
            const auto& task = dp.tasks[task_idx];
            
            // 检查当前任务是否为巴士任务
            if (std::holds_alternative<Bus>(task)) {
                // 删除巴士任务
                dp.tasks.erase(dp.tasks.begin() + task_idx);
                
                // 更新duty period的统计信息
                dp.taskCount--;
                dp.total_task_time -= get_task_duration(task);
                
                // 如果duty period变为空，删除整个duty period
                if (dp.tasks.empty()) {
                    duty_periods_.erase(duty_periods_.begin() + dp_idx);
                    break;
                }
            } else if (std::holds_alternative<Flight>(task)) {
                // 遇到第一个航班任务，停止删除
                return;
            } else {
                // 对于其他类型的任务（如GroundDuty），继续遍历
                continue;
            }
        }
    }
}
    
   // --- Getter Helper Functions ---
TimePoint get_start_time(const std::variant<Flight, Bus, GroundDuty>& task) {
    if (std::holds_alternative<Flight>(task)) {
        return std::get<Flight>(task).std;
    }
    else if (std::holds_alternative<Bus>(task)) {
        return std::get<Bus>(task).td;
    }
    else if (std::holds_alternative<GroundDuty>(task)) {
        return std::get<GroundDuty>(task).start_time;
    }
    return TimePoint();
}

TimePoint get_end_time(const std::variant<Flight, Bus, GroundDuty>& task) {
    if (std::holds_alternative<Flight>(task)) {
        return std::get<Flight>(task).sta;
    }
    else if (std::holds_alternative<Bus>(task)) {
        return std::get<Bus>(task).ta;
    }
    else if (std::holds_alternative<GroundDuty>(task)) {
        return std::get<GroundDuty>(task).end_time;
    }
    return TimePoint();
}

std::string get_arrival_airport(const std::variant<Flight, Bus, GroundDuty>& task) {
    if (std::holds_alternative<Flight>(task)) {
        return std::get<Flight>(task).arriAirport;
    }
    else if(std::holds_alternative<Bus>(task)){
        return std::get<Bus>(task).arriAirport;
    }
    else if(std::holds_alternative<GroundDuty>(task)){
        return std::get<GroundDuty>(task).airport;
    }
    return "";
}

// get the lastest ground duty of the crew according to the lastest task end time
GroundDuty CrewSchedule::get_lastest_ground_duty(){
    for(auto& ground_duty : crew_.groundDuties){
        if(ground_duty.start_time > last_task_end_time_){
            return ground_duty;
        }
    }
    return GroundDuty();
}

std::chrono::minutes get_task_duration(const std::variant<Flight, Bus, GroundDuty>& task){
    if(std::holds_alternative<Flight>(task)){
        return std::chrono::minutes(std::get<Flight>(task).flyTime_mins);
    }
    else if(std::holds_alternative<Bus>(task)){
        return std::chrono::duration_cast<std::chrono::minutes>(std::get<Bus>(task).ta - std::get<Bus>(task).td);
    }
    else if(std::holds_alternative<GroundDuty>(task)){
        return std::chrono::duration_cast<std::chrono::minutes>(std::get<GroundDuty>(task).end_time - std::get<GroundDuty>(task).start_time);
    }
    return std::chrono::minutes(0);
}





