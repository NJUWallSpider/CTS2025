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
std::vector<Cycle>& cycles, const std::map<std::string, std::vector<Turnaround>>& crew_turnarounds)
    : data_(data), crew_(crew), duty_periods_(crew_duty_periods), flight_assignments(flight_assignments), cycles_(cycles), crew_turnarounds_(crew_turnarounds) {
    
    for (const auto& [_, crew_data] : data.getCrews()) {
        all_bases_.insert(crew_data.base);
    }

    initialize_crew_state();
}


void CrewSchedule::construct_schedule_DFS() {
    // STEP 1. Filter and sort turnaround candidates
    std::vector<Turnaround> candidates;
    filter_turnaround_candidates(candidates);

    std::sort(candidates.begin(), candidates.end(), [&](const Turnaround& a, const Turnaround& b) {
        if(a.flights.size() != b.flights.size()){
            return a.flights.size() > b.flights.size();
        }
        
        bool a_return_to_base = a.endAirport == crew_.base;
        bool b_return_to_base = b.endAirport == crew_.base;
        if(a_return_to_base != b_return_to_base){
            return a_return_to_base;
        }

        

        return a.startTime < b.startTime;
    });

    // STEP 2. Iterate through sorted candidates
    for (auto& turnaround : candidates) {
        std::vector<DutyPeriod> backup_duty_periods = duty_periods_;
        std::string backup_current_airport = current_airport_;
        TimePoint backup_last_task_end_time = last_task_end_time_;
        std::map<std::string, std::vector<std::pair<std::string, bool>>> backup_flight_assignments = flight_assignments;

        if (Check_Action_validity(turnaround)) {
            Action_TA(turnaround);

            last_task_end_time_ = get_end_time(duty_periods_.back().tasks.back());
            current_airport_ = get_arrival_airport(duty_periods_.back().tasks.back());
            Update_DutyPeriod();

            if (Check_Cycle_validity()) {
                action_cycle();
                Update_Cycle();
                construct_schedule_DFS();
                return;
            } else {
                duty_periods_ = backup_duty_periods;
                current_airport_ = backup_current_airport;
                last_task_end_time_ = backup_last_task_end_time;
                flight_assignments = backup_flight_assignments;
            }
        }
    }

    // No valid path found, so we check for bus positioning to base
    if (current_airport_ != crew_.base) {
        for (const auto& bus : data_.getBuses()) {
            if (bus.depaAirport == current_airport_ && bus.arriAirport == crew_.base && bus.td > last_task_end_time_) {
                if (Check_ddh_Bus_validity(bus)) {
                    action_add_bus(bus);
                    Update_DutyPeriod();
                    return; // End of schedule for this crew
                }
            }
        }
    }
    return;
}

void CrewSchedule::Action_TA(const Turnaround& turnaround){
    if(duty_periods_.back().tasks.empty()){
        for (const auto* flight : turnaround.flights) {
            duty_periods_.back().tasks.emplace_back(*flight);
            flight_assignments[flight->id].emplace_back(crew_.id, true);
        }
    }
    else{
        DutyPeriod new_duty_period;
        for (const auto* flight : turnaround.flights) {
            new_duty_period.tasks.emplace_back(*flight);
            flight_assignments[flight->id].emplace_back(crew_.id, true);
        }
        duty_periods_.emplace_back(new_duty_period);
    }
}

void CrewSchedule::filter_turnaround_candidates(std::vector<Turnaround>& candidates) {
    // Check if there are pre-computed turnarounds for the current airport
    auto airport_it = crew_turnarounds_.find(current_airport_);
    if (airport_it == crew_turnarounds_.end()) {
        return; // No turnarounds available from current airport
    }
    
    const auto& turnarounds_from_airport = airport_it->second;
    
    for (const auto& turnaround : turnarounds_from_airport) {
        // Check if the turnaround starts after the last task end time
        if (turnaround.startTime < last_task_end_time_) {
            continue;
        }
        
        // Check if any flights in the turnaround are already assigned
        bool has_assigned_flight = false;
        for (const auto* flight : turnaround.flights) {
            if (flight_assignments.count(flight->id)) {
                has_assigned_flight = true;
                break;
            }
        }
        
        if (has_assigned_flight) {
            continue; // Skip turnarounds with already assigned flights
        }
        
        // Add the valid turnaround to candidates
        candidates.push_back(turnaround);
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





