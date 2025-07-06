#include "CrewSchedule.hpp"
#include "../../Loader/Utils.h"
#include <chrono>
#include <iostream>
#include <vector>
#include <variant>
#include <map>
#include <limits>
#include <algorithm>
#include <numeric>
#include "../SolutionState.hpp"



CrewSchedule::CrewSchedule(const DataLoader& data, const Crew& crew,std::vector<DutyPeriod>& crew_duty_periods, std::map<std::string, std::vector<std::pair<std::string, bool>>>& flight_assignments,
std::vector<Cycle>& cycles)
    : data_(data), crew_(crew), duty_periods_(crew_duty_periods), flight_assignments(flight_assignments), cycles_(cycles) {
    initialize_crew_state();
}


void CrewSchedule::construct_schedule_DFS() {
    /////////////////END CONDITION///////////////
    
    /////////////////SEARCH/////////////////////
    //STEP 1. Find all possible next tasks (flights and buses) from the CURRENT airport and the last task end time
    std::vector<std::variant<Flight, Bus, GroundDuty>> candidates;
    for (const auto& flight : data_.getFlights()) {
        if (flight.depaAirport == current_airport_ && flight.std >= last_task_end_time_ ) {
            candidates.emplace_back(flight);
        }
    }
    for (const auto& bus : data_.getBuses()) {
        if (bus.depaAirport == current_airport_ && bus.td >= last_task_end_time_) {
            candidates.emplace_back(bus);
        }
    }
    for (const auto& ground_duty : crew_.groundDuties) {
        if ( ground_duty.start_time > last_task_end_time_) {
            candidates.emplace_back(ground_duty);
        }
    }

    // Sort candidates with flights first, then buses, then ground duties, each sorted chronologically
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        
        // First compare by task type priority (flight > bus > ground duty)
        bool a_is_flight = std::holds_alternative<Flight>(a);
        bool b_is_flight = std::holds_alternative<Flight>(b);
        if (a_is_flight != b_is_flight) return a_is_flight;

        bool a_is_bus = std::holds_alternative<Bus>(a);
        bool b_is_bus = std::holds_alternative<Bus>(b);
        if (a_is_bus != b_is_bus) return a_is_bus;

        // If same type, sort chronologically by start time
        return get_start_time(a) < get_start_time(b);
    });

    // STEP2. Iterate through sorted candidates.
    for (auto& candidate_task : candidates) {
        // copy the current state
        std::vector<DutyPeriod> backup_duty_periods = duty_periods_;
        std::vector<Cycle> backup_cycles = cycles_;
        std::string backup_current_airport = current_airport_;
        TimePoint backup_last_task_end_time = last_task_end_time_;

        // check the validity of the action
        if (Check_Action_validity(candidate_task)) {
            Action(candidate_task);
            /////////// GLOBLE STATE ///////////
            // 1. update the last task end time
            last_task_end_time_ = get_end_time(duty_periods_.back().tasks.back());
            // 2. update the current airport
            current_airport_ = get_arrival_airport(duty_periods_.back().tasks.back());
            /////////// DUTY PERIOD STATE ///////////
            Update_DutyPeriod();
            // the validity of teh Layover and Cycle should be checked after the DP is updated
            // if(Check_LayOver_validity() && Check_Cycle_validity()){
            if(Check_Cycle_validity()){
                action_cycle();
                Update_Cycle();
                // --- Recurse ---
                construct_schedule_DFS();
                return ;
            }
            // if the action is not valid, backtrack to the previous state
            else{
                duty_periods_ = backup_duty_periods;
                cycles_ = backup_cycles;
                current_airport_ = backup_current_airport;
                last_task_end_time_ = backup_last_task_end_time;
            }
            
            // // --- Backtrack by restoring state ---
            // current_airport_ = backup_current_airport;
            // last_task_end_time_ = backup_last_task_end_time;
            // duty_periods_ = backup_duty_periods;
            // cycles_ = backup_cycles;
        }
    }

    // No valid chronological path found from this state, so we just return.
    return;
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





