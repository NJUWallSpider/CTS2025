#include "CrewSchedule.hpp"
#include "../SolutionState.hpp"
#include "../../Loader/Utils.h"
#include <chrono>
#include <iostream>
#include <vector>
#include <variant>
#include <map>
#include <limits>
#include <algorithm>
#include <numeric>


void CrewSchedule::Action(std::variant<Flight, Bus, GroundDuty>& candidate_task){
    // If the candidate task is a flight
    if(std::holds_alternative<Flight>(candidate_task)){
        action_add_flight(std::get<Flight>(candidate_task));
    }
    // If the candidate task is a bus
    else if(std::holds_alternative<Bus>(candidate_task)){
        action_add_bus(std::get<Bus>(candidate_task));
    }
    else if(std::holds_alternative<GroundDuty>(candidate_task)){
        action_add_ground_duty(std::get<GroundDuty>(candidate_task));
    }
}

void CrewSchedule::action_add_flight(const Flight& flight){

    // get the last Duty Period
    DutyPeriod& last_duty_period = duty_periods_.back();
    // if the lastest duty period is a FDuty and extendable
    if(last_duty_period.tasks.empty()){
        last_duty_period.tasks.emplace_back(flight);
        return;
    }
    
    if(last_duty_period.is_FDuty && last_duty_period.can_be_extended){
        // start a new FDutyP: check the rest time:
        if (flight.std - last_duty_period.endTime > MIN_REST_BEFORE_FDUTY){

            DutyPeriod new_duty_period;
            new_duty_period.tasks.emplace_back(flight);
            duty_periods_.emplace_back(new_duty_period);
        }
        else{
        // add the flight to the last duty period
        last_duty_period.tasks.emplace_back(flight);
        }

    }
    // if the lastest duty period is a FDuty and not extendable
    else if(last_duty_period.is_FDuty && !last_duty_period.can_be_extended){
        // start a new FDutyP
        DutyPeriod new_duty_period;
        new_duty_period.tasks.emplace_back(flight);
        duty_periods_.emplace_back(new_duty_period);

        
    }
    // if the lastest duty period is a NFDuty
    else{
        // start a new FDutyP
        DutyPeriod new_duty_period;
        new_duty_period.tasks.emplace_back(flight);
        duty_periods_.emplace_back(new_duty_period);
    }


}

void CrewSchedule::action_add_bus(const Bus& bus){
    // get the last Duty Period
    DutyPeriod& last_duty_period = duty_periods_.back();
    // if the lastest duty period is a FDuty and extendable
    if(last_duty_period.tasks.empty() || (last_duty_period.is_FDuty && last_duty_period.can_be_extended)){
        // add the bus to the last duty period
        last_duty_period.tasks.emplace_back(bus);
    }
    // if the lastest duty period is a FDuty and not extendable
    else if(last_duty_period.is_FDuty && !last_duty_period.can_be_extended){
        // start a new FDutyP
        DutyPeriod new_duty_period;
        new_duty_period.tasks.emplace_back(bus);
        duty_periods_.emplace_back(new_duty_period);
    }
    // if the lastest duty period is a NFDuty
    else{
        // add the bus to the last duty period
        last_duty_period.tasks.emplace_back(bus);
    }
}

void CrewSchedule::action_add_ground_duty(const GroundDuty& ground_duty){

    // get the last Duty Period
    DutyPeriod& last_duty_period = duty_periods_.back();

    // if the lastest duty period is a FDuty
    if(last_duty_period.is_FDuty){
        // start a new NFDP
        DutyPeriod new_duty_period;
        new_duty_period.tasks.emplace_back(ground_duty);
        duty_periods_.emplace_back(new_duty_period);
    }
    // if the lastest duty period is a NFDuty
    else{
        // add the ground duty to the last duty period
        last_duty_period.tasks.emplace_back(ground_duty);
    }
}

void CrewSchedule::action_remove_last_task() {
    // get the last Duty Period
    DutyPeriod& last_duty_period = duty_periods_.back();

    // remove the last task from the last duty period
    last_duty_period.tasks.pop_back();

    // if the last task is a flight
    if (std::holds_alternative<Flight>(last_duty_period.tasks.back())) {
        // remove the <current Crew_id, bool> from the piloted flights vector
        auto& assignments = flight_assignments[std::get<Flight>(last_duty_period.tasks.back()).id];
        // find the current Crew_id in the piloted flights vector
        // crew_assignment: <Crew_id, bool>
        for(auto& crew_assignment : assignments){
            if(crew_assignment.first == crew_.id){
                // remove the <current Crew_id, bool> from the piloted flights vector
                assignments.erase(std::remove(assignments.begin(), assignments.end(), crew_assignment), assignments.end());
                break;
            }
        }
    }
}

void CrewSchedule::action_allocate_pilot(){
    // suppose the lastest task is a flight
    Flight& flight = std::get<Flight>(duty_periods_.back().tasks.back());
    // if the flight is assigned
    if(flight_assignments.find(flight.id) != flight_assignments.end()){
        // Check all crews piloted to this flight
        bool has_qualified_crew = false;
        for (auto& crew_assignment : flight_assignments[flight.id]) {
            if (crew_assignment.second) {
                has_qualified_crew = true;
                break;
            }
        }
        // If there's already a qualified crew, this crew will be non-piloted
        if (has_qualified_crew) {
            flight_assignments[flight.id].emplace_back(crew_.id, false);
        }
        // if there's no a qualified crew piloted to this flight, and the crew is qualified for this flight, then this crew is the crew to carry out this flight
        else if(crew_.qualifications.find(flight.id) != crew_.qualifications.end()){
            flight_assignments[flight.id].emplace_back(crew_.id, true);
        }
        //if there's no a qualified crew piloted to this flight, and the crew is not qualified for this flight, then this crew is a DDH to the flight
        else{
            flight_assignments[flight.id].emplace_back(crew_.id, false);  
        }
    }
    // if the flight is not assigned to any crew
    else{
        // if the crew is qualified, then the flight is piloted to this crew
        if(crew_.qualifications.find(flight.id) != crew_.qualifications.end()){
            flight_assignments[flight.id].emplace_back(crew_.id, true);
        }
        // if the crew is not qualified, then the flight is a DDH to the crew
        else{
            flight_assignments[flight.id].emplace_back(crew_.id, false);
        }
    }
    }


// void CrewSchedule::action_cycle(){
//     // get the latest cycle
//     Cycle& latest_cycle = cycles_.back();
//     // get the latest duty period
//     DutyPeriod& latest_duty_period = duty_periods_.back();
//     // if the latest duty is a FDuty
//     if(latest_duty_period.is_FDuty){
//         // if the crossed calendar days between the end time of the last FDP and the start time of the current cycle
//         // is greater than MAX CYCLE CALENDAR DAYS, then try to add a new cycle
//         if(latest_duty_period.endTime - latest_cycle.startTime > MAX_CYCLE_CALENDAR_DAYS){
//             // if the crossed calendar days between the start time of the current FDP and the end time of the second latest DP > MIN_REST_CALENDAR_DAYS, then try to add a new cycle
//             // get the second latest duty period
//             const auto& second_latest_duty_period = duty_periods_[duty_periods_.size() - 2];
//             if(latest_duty_period.startTime - second_latest_duty_period.endTime > MIN_REST_CALENDAR_DAYS){
//                 // start a new cycle
//                 Cycle new_cycle;
//                 // add the current duty period to the new cycle
//                 new_cycle.duty_periods.emplace_back(latest_duty_period);
//                 // add the new cycle to the cycles vector
//                 cycles_.emplace_back(new_cycle);
                
//             }

//         }
//         else{
//             // add the current duty period to the current cycle
//             latest_cycle.duty_periods.emplace_back(latest_duty_period);
//         }

//     }
//     else{
//         latest_cycle.duty_periods.emplace_back(latest_duty_period);
//     }
    
//     // allocate the pilot or DDH
//     if(std::holds_alternative<Flight>(duty_periods_.back().tasks.back())){
//         action_allocate_pilot();
//     }
// }

void CrewSchedule::action_cycle(){
    // get the latest cycle
    Cycle& latest_cycle = cycles_.back();
    // get the latest duty period
    DutyPeriod& latest_duty_period = duty_periods_.back();
    // if the latest duty is a FDuty
    if(latest_duty_period.is_FDuty){
        // get the start and end day of the cycle
        auto cycle_start_day = std::chrono::floor<std::chrono::days>(latest_cycle.startTime);
        auto cycle_end_day = std::chrono::floor<std::chrono::days>(latest_duty_period.endTime) + std::chrono::days(1);
        auto cycle_calendar_days = std::chrono::duration_cast<std::chrono::days>(cycle_end_day - cycle_start_day).count() ;
        
        // if the crossed calendar days between the end time of the last FDP and the start time of the current cycle is greater than MAX CYCLE CALENDAR DAYS
        if(cycle_calendar_days > std::chrono::duration_cast<std::chrono::days>(MAX_CYCLE_CALENDAR_DAYS).count() && latest_cycle.duty_periods.size() > 0){
            // if the crossed calendar days between the start time of the current FDP and the end time of the second latest DP > MIN_REST_CALENDAR_DAYS, then try to add a new cycle
            // get the second latest duty period
            // Check if there are at least 2 duty periods before accessing
   

            auto rest_start_day = std::chrono::floor<std::chrono::days>(get_rest_start_point()) + std::chrono::days(1);

            // Get the start of the day for the latest duty period start time
            auto rest_end_day = std::chrono::floor<std::chrono::days>(latest_duty_period.startTime);
            // Check if the rest period spans at least 2 calendar days
            auto rest_calendar_days = std::chrono::duration_cast<std::chrono::days>(rest_end_day - rest_start_day).count();
            
            if(rest_calendar_days >= std::chrono::duration_cast<std::chrono::days>(MIN_REST_CALENDAR_DAYS).count()){
                // start a new cycle
                Cycle new_cycle;
                // add the current duty period to the new cycle
                new_cycle.duty_periods.emplace_back(latest_duty_period);
                // add the new cycle to the cycles vector
                cycles_.emplace_back(new_cycle); 
                
            }
            else{ return ;}
            
        }
        else{
            // add the current duty period to the current cycle
            latest_cycle.duty_periods.emplace_back(latest_duty_period);
        }

    }
    else{
        latest_cycle.duty_periods.emplace_back(latest_duty_period);
    }


    // allocate the pilot or DDH
    if(std::holds_alternative<Flight>(duty_periods_.back().tasks.back())){
        action_allocate_pilot();
    }
}


void CrewSchedule::action_try_add_positioning(const GroundDuty& ground_duty){
    DutyPeriod& last_duty_period = duty_periods_.back();
    if(last_duty_period.is_FDuty){
        // get the lastest task
        std::variant<Flight, Bus, GroundDuty>& lastest_task = last_duty_period.tasks.back();
        // if the lastest task is a flight
        if(std::holds_alternative<Flight>(lastest_task)){
            // get the flight
            Flight& flight = std::get<Flight>(lastest_task);
            // when the spot connection is not met ..., A]  (A->B) [B
            if(flight.arriAirport != ground_duty.airport){
                // firstly, try to find a BUS (A->B) 
                for(const auto& bus : data_.getBuses()){
                    if(bus.depaAirport == flight.arriAirport && bus.arriAirport == ground_duty.airport && bus.td >= flight.sta && bus.ta <= ground_duty.start_time){
                            // add the first found bus to the last duty period
                            action_add_bus(bus);
                            return;
                        
                    }
                }

            }
            std::vector<std::string> layover_spots = data_.getLayoverStations();
            if(std::find(layover_spots.begin(), layover_spots.end(), flight.arriAirport) == layover_spots.end()){

            }
        }
    }
}