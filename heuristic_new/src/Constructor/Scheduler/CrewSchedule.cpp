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
std::vector<Cycle>& cycles)
    : data_(data), crew_(crew), duty_periods_(crew_duty_periods), flight_assignments(flight_assignments), cycles_(cycles) {
    
    for (const auto& [_, crew_data] : data.getCrews()) {
        all_bases_.insert(crew_data.base);
    }

    initialize_crew_state();
}


void CrewSchedule::construct_schedule_DFS() {
    // STEP 1. Generate and sort turnaround candidates
    std::vector<Turnaround> candidates;
    generate_turnaround_candidates(candidates);
    add_bus_to_turnarounds(candidates);

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
        // Only add positioning bus if it exists
        if (turnaround.positioning_bus != nullptr) {
            duty_periods_.back().tasks.emplace_back(*turnaround.positioning_bus);
        }
    }
    else{
        DutyPeriod new_duty_period;
        for (const auto* flight : turnaround.flights) {
            new_duty_period.tasks.emplace_back(*flight);
            flight_assignments[flight->id].emplace_back(crew_.id, true);
        }
        // Only add positioning bus if it exists
        if (turnaround.positioning_bus != nullptr) {
            new_duty_period.tasks.emplace_back(*turnaround.positioning_bus);
        }
        duty_periods_.emplace_back(new_duty_period);
    }
}

void CrewSchedule::generate_turnaround_candidates(std::vector<Turnaround>& candidates) {
    const auto& aircraft_routes = data_.getAircraftRoutes();
    for (const auto& flight : data_.getFlights()) {
        if (flight_assignments.count(flight.id)) {
            continue; // Skip assigned flights
        }

        if (crew_.qualifications.find(flight.id) == crew_.qualifications.end()) {
            continue; // Skip flights crew is not qualified for
        }
        if (flight.depaAirport != current_airport_) {
            continue;
        }

        if (flight.std < last_task_end_time_) {
            continue;
        }

        Turnaround current_turnaround;
        current_turnaround.flights.push_back(&flight);
        current_turnaround.startTime = flight.std;
        current_turnaround.total_flight_time = std::chrono::minutes(flight.flyTime_mins);

        const Flight* current_flight_in_turnaround = &flight;
        while (true) {
            auto route_it = aircraft_routes.find(current_flight_in_turnaround->aircraftNo);
            if (route_it == aircraft_routes.end()) break;

            auto conn_it = std::find_if(route_it->second.connections.begin(), route_it->second.connections.end(), 
                [&](const AircraftFlightConnection& conn){
                return conn.flight->id == current_flight_in_turnaround->id && conn.next_flight != nullptr;
            });

            if (conn_it == route_it->second.connections.end()) break;

            const Flight* next_flight = conn_it->next_flight;
            if (flight_assignments.count(next_flight->id)) break;
            if (crew_.qualifications.find(next_flight->id) == crew_.qualifications.end()) break;

            auto new_total_flight_time = current_turnaround.total_flight_time + std::chrono::minutes(next_flight->flyTime_mins);
            auto new_duty_time = std::chrono::duration_cast<std::chrono::minutes>(next_flight->sta - current_turnaround.startTime);

            if (new_total_flight_time > MAX_FLY_TIME_PER_DUTY || new_duty_time > MAX_DUTY_TIME_PER_FLIGHT_DUTY) break;
            
            current_turnaround.flights.push_back(next_flight);
            current_turnaround.total_flight_time = new_total_flight_time;
            current_flight_in_turnaround = next_flight;
        }

        current_turnaround.endTime = current_flight_in_turnaround->sta;
        current_turnaround.startAirport = current_turnaround.flights.front()->depaAirport;
        current_turnaround.endAirport = current_turnaround.flights.back()->arriAirport;
        candidates.push_back(current_turnaround);
    }
}
void CrewSchedule::add_bus_to_turnarounds(std::vector<Turnaround>& candidates){
    
    for (auto& turnaround : candidates) {
        // If turnaround already returns to base, keep it as is
        if (turnaround.endAirport == crew_.base) {
            continue;
        } else {
            // Try to find a bus that can take crew back to base
            bool found_valid_bus = false;
            
            for (const auto& bus : data_.getBuses()) {
                // Check if bus goes from turnaround end to crew base
                if (bus.depaAirport == turnaround.endAirport && 
                    bus.arriAirport == crew_.base &&
                    bus.td >= turnaround.endTime &&
                    bus.td < turnaround.endTime + MIN_REST_BEFORE_FDUTY) {
                    
                    // Basic timing check - ensure bus departure is reasonable after turnaround end
                    auto connection_time = bus.td - turnaround.endTime;
                    if (connection_time >= MIN_CONNECTION_TIME_BUS) {
                        // Create a new turnaround with the positioning bus
           
                        turnaround.positioning_bus = &bus;
                        turnaround.endTime = bus.ta;
                        turnaround.endAirport = bus.arriAirport; // This should now be crew_.base
                        
                        found_valid_bus = true;
                        break; // Take the first valid bus found
                    }
                }
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





