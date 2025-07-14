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

    
    /////////////////SEARCH/////////////////////
    //STEP 1. Find all possible next tasks (flights and buses) from the CURRENT airport and the last task end time
    std::vector<std::variant<Flight, Bus, GroundDuty>> candidates;
    int flight_count = 0;
    int bus_count = 0;

    DutyPeriod last_duty_period = duty_periods_.back();
    // if((!last_duty_period.is_FDuty && current_airport_ != crew_.base ) || (last_duty_period.is_FDuty && last_duty_period.total_flight_time > std::chrono::hours(7) && std::find(all_bases_.begin(), all_bases_.end(), current_airport_) != all_bases_.end())){
    //     for (const auto& bus : data_.getBuses()) {
    //         if (bus.depaAirport == current_airport_ && bus.td >= last_task_end_time_ ) {
    //             candidates.emplace_back(bus);
    //             bus_count++;
    //         }
    //     }
    //     std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
    //         bool a_is_bus = std::holds_alternative<Bus>(a);
    //         bool b_is_bus = std::holds_alternative<Bus>(b);

    //         if (a_is_bus && b_is_bus) {
    //             const auto& bus_a = std::get<Bus>(a);
    //             const auto& bus_b = std::get<Bus>(b);

    //             // bool a_to_pointing = bus_a.arriAirport == unassigned_qualified_flight.depaAirport;
    //             // bool b_to_pointing = bus_b.arriAirport == unassigned_qualified_flight.depaAirport;
    //             // if(a_to_pointing != b_to_pointing){
    //             //     return a_to_pointing;
    //             // }

    //             bool a_to_base = bus_a.arriAirport == crew_.base;
    //             bool b_to_base = bus_b.arriAirport == crew_.base;
    //             if(a_to_base != b_to_base){
    //                 return a_to_base;
    //             }
                
    //             bool a_to_hub = all_bases_.count(bus_a.arriAirport) > 0;
    //             bool b_to_hub = all_bases_.count(bus_b.arriAirport) > 0;
    //             if (a_to_hub != b_to_hub) {
    //                 return a_to_hub; // Prioritize hub destination
    //             }
    //         }
    //         return get_start_time(a) < get_start_time(b);
    //     });

    // }
    // else{

        for (const auto& flight : data_.getFlights()) {
            // if (flight.std >= last_task_end_time_ ) {
            if (flight.depaAirport == current_airport_ && flight.std > last_task_end_time_ ) {
                candidates.emplace_back(flight);
                flight_count++;
            }
        }
        //&& std::find(crew_.qualifications.begin(), crew_.qualifications.end(), flight.id) != crew_.qualifications.end()

        for (const auto& bus : data_.getBuses()) {
            if (bus.depaAirport == current_airport_ && bus.td > last_task_end_time_ ) {
                candidates.emplace_back(bus);
                bus_count++;
            }
        }
        //&& bus.arriAirport == crew_.base

        // for (const auto& ground_duty : crew_.groundDuties) {
        //     if ( ground_duty.start_time > last_task_end_time_) {
        //         candidates.emplace_back(ground_duty);
        //     }
        // }
        std::string unassigned_qualified_flight_id = find_unassigned_qualified_flight();
        if(unassigned_qualified_flight_id == ""){
            if(current_airport_ == crew_.base){
                return ;
            }
        }
        // Flight unassigned_qualified_flight;
        // for(const auto& flight : data_.getFlights()){
        //     if(flight.id == unassigned_qualified_flight_id){
        //         unassigned_qualified_flight = flight;
        //         break;
        //     }
        // }

        
        // if the latest task is a flight, get the aircraftNo
        // std::string last_aircraftNo = "";
        // if(duty_periods_.back().is_FDuty && duty_periods_.back().can_be_extended){
        //     last_aircraftNo = std::get<Flight>(duty_periods_.back().tasks.back()).aircraftNo;
        // }

        std::vector<std::string> layover_spots = data_.getLayoverStations();
        std::string start_ =  "2025/5/1 00:00" ;     
        time_t start_time_t = Utils::parseTime(start_);
        TimePoint schedule_start_time = std::chrono::system_clock::from_time_t(start_time_t)+ std::chrono::hours(8);
    
        // bool maybe_the_last_flight = (duty_periods_.back().is_FDuty && duty_periods_.back().can_be_extended && duty_periods_.back().taskCount >= 2 );
        bool maybe_the_last_flight = false;
        // Sort candidates based on the new rules
        std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {


                bool a_is_flight = std::holds_alternative<Flight>(a);
                bool b_is_flight = std::holds_alternative<Flight>(b);
                bool a_is_bus = std::holds_alternative<Bus>(a);
                bool b_is_bus = std::holds_alternative<Bus>(b);

                // bool a_before_schedule = get_start_time(a) < schedule_start_time;
                // bool b_before_schedule = get_start_time(b) < schedule_start_time;
                // if(a_before_schedule != b_before_schedule){
                //     if(a_is_bus){
                //         return current_airport_ != crew_.base && get_arrival_airport(a) == crew_.base;
                //     }
                //     else{
                //         return current_airport_ != crew_.base && get_arrival_airport(b) == crew_.base;
                //     }
                // }

                // if(a_is_flight && b_is_flight){
                //     const auto& flight_a = std::get<Flight>(a);
                //     const auto& flight_b = std::get<Flight>(b);
                //     bool a_qualified = std::find(crew_.qualifications.begin(), crew_.qualifications.end(), flight_a.id) != crew_.qualifications.end();
                //     bool b_qualified = std::find(crew_.qualifications.begin(), crew_.qualifications.end(), flight_b.id) != crew_.qualifications.end();
                //     if(a_qualified != b_qualified){
                //         return a_qualified;
                //     }
                // }
                // if(a_is_flight != b_is_flight){
                //     if(a_is_flight){
                //         const auto& flight_a = std::get<Flight>(a);
                //         bool a_qualified = std::find(crew_.qualifications.begin(), crew_.qualifications.end(), flight_a.id) != crew_.qualifications.end();
                //         if(a_qualified){
                //             return true;
                //         }
                //     }
                //     else{
                //         const auto& flight_b = std::get<Flight>(b);
                //         bool b_qualified = std::find(crew_.qualifications.begin(), crew_.qualifications.end(), flight_b.id) != crew_.qualifications.end();
                //         if(b_qualified){
                //             return true;
                //         }
                //     }
                // }


                // // // Prioritize flights that return to the crew's base
                // // if(maybe_the_last_flight){
                // //     if (a_is_flight && b_is_flight) {
                // //         const auto& flight_a = std::get<Flight>(a);
                // //         const auto& flight_b = std::get<Flight>(b);
                // //         bool a_returns_to_base = (flight_a.arriAirport == crew_.base);
                // //         bool b_returns_to_base = (flight_b.arriAirport == crew_.base);
                // //         if (a_returns_to_base != b_returns_to_base) {
                // //             return a_returns_to_base;
                // //         }
                // //         bool a_is_layover = std::find(layover_spots.begin(), layover_spots.end(), flight_a.arriAirport) != layover_spots.end();
                // //         bool b_is_layover = std::find(layover_spots.begin(), layover_spots.end(), flight_b.arriAirport) != layover_spots.end();
                // //         if (a_is_layover != b_is_layover) {
                // //             return a_is_layover;
                // //         }
                // //     }
                // // }

                if (a_is_flight != b_is_flight) return a_is_flight;
                // if(a_is_flight && b_is_flight){
                //     const auto& flight_a = std::get<Flight>(a);
                //     const auto& flight_b = std::get<Flight>(b);
                //     bool a_same_aircraftNo = flight_a.aircraftNo == last_aircraftNo;
                //     bool b_same_aircraftNo = flight_b.aircraftNo == last_aircraftNo;
                //     if(a_same_aircraftNo != b_same_aircraftNo){
                //         return a_same_aircraftNo;
                //     }
                // }
                //  if(a_is_flight && b_is_flight){
                //     const auto& flight_a = std::get<Flight>(a);
                //     const auto& flight_b = std::get<Flight>(b);
                //     bool a_qualified = std::find(crew_.qualifications.begin(), crew_.qualifications.end(), flight_a.id) != crew_.qualifications.end();
                //     bool b_qualified = std::find(crew_.qualifications.begin(), crew_.qualifications.end(), flight_b.id) != crew_.qualifications.end();
                //     if(a_qualified != b_qualified){
                //         return a_qualified;
                //     }
                // }

                // if (a_is_bus && !b_is_bus) {
                //     return false;  
                // }
                // if (!a_is_bus && b_is_bus) {
                //     return true;  
                // }

                if (a_is_bus && b_is_bus) {
                    const auto& bus_a = std::get<Bus>(a);
                    const auto& bus_b = std::get<Bus>(b);

                    // bool a_to_pointing = bus_a.arriAirport == unassigned_qualified_flight.depaAirport;
                    // bool b_to_pointing = bus_b.arriAirport == unassigned_qualified_flight.depaAirport;
                    // if(a_to_pointing != b_to_pointing){
                    //     return a_to_pointing;
                    // }

                    bool a_to_base = bus_a.arriAirport == crew_.base;
                    bool b_to_base = bus_b.arriAirport == crew_.base;
                    if(a_to_base != b_to_base){
                        return a_to_base;
                    }
                    
                    bool a_to_hub = all_bases_.count(bus_a.arriAirport) > 0;
                    bool b_to_hub = all_bases_.count(bus_b.arriAirport) > 0;
                    if (a_to_hub != b_to_hub) {
                        return a_to_hub; // Prioritize hub destination
                    }
                }
        
            
            return get_start_time(a) < get_start_time(b);
        });

        // Bus detector_bus;
        // detector_bus.id = DETECTOR_BUS_ID;
        // detector_bus.depaAirport = current_airport_;
        // detector_bus.arriAirport = current_airport_;
        // std::string start_ =  "2026/12/30 00:00" ;     
        // time_t start_time_t = Utils::parseTime(start_);
        // TimePoint start_time = std::chrono::system_clock::from_time_t(start_time_t);
        // detector_bus.td = start_time + std::chrono::days(20);
        // detector_bus.ta = start_time + std::chrono::days(30);
        // candidates.emplace_back(std::move(detector_bus));






        //     // 1. Check if last duty period is FDP and cannot be extended
        // if (!duty_periods_.empty() && duty_periods_.back().is_FDuty && !duty_periods_.back().can_be_extended) {
        //     // 2. Extract buses from candidates
        //     std::vector<Bus> candidate_poses;
        //     for (const auto& candidate : candidates) {
        //         if (std::holds_alternative<Bus>(candidate)) {
        //             candidate_poses.push_back(std::get<Bus>(candidate));
        //         }
        //     }

        //     // 3. Sort buses according to the rules
        //     std::sort(candidate_poses.begin(), candidate_poses.end(), [&](const Bus& a, const Bus& b) {
        //         // Priority 1: Arrival airport is crew base
        //         if (a.arriAirport == crew_.base && b.arriAirport != crew_.base) return true;
        //         if (a.arriAirport != crew_.base && b.arriAirport == crew_.base) return false;

        //         // Priority 2: Arrival airport is in layover stations
        //         bool a_is_layover = std::find(layover_spots.begin(), layover_spots.end(), a.arriAirport) != layover_spots.end();
        //         bool b_is_layover = std::find(layover_spots.begin(), layover_spots.end(), b.arriAirport) != layover_spots.end();
        //         if (a_is_layover && !b_is_layover) return true;
        //         if (!a_is_layover && b_is_layover) return false;

        //         // Priority 3: Chronological by departure time
        //         return a.td < b.td;
        //     });

        //     // 4. Replace the bus candidates in the original candidates vector
        //     // Remove all buses from candidates
        //     candidates.erase(
        //         std::remove_if(candidates.begin(), candidates.end(),
        //             [](const auto& c) { return std::holds_alternative<Bus>(c); }),
        //         candidates.end()
        //     );
        //     // Insert sorted buses after flights (or at the appropriate position)
        //     // Find the position after the last flight
        //     auto insert_pos = std::find_if(candidates.begin(), candidates.end(),
        //         [](const auto& c) { return !std::holds_alternative<Flight>(c); });
        //     candidates.insert(insert_pos, candidate_poses.begin(), candidate_poses.end());
        // }

    //}

        // STEP2. Iterate through sorted candidates.
    for (auto& candidate_task : candidates) {
        // copy the current state
        std::vector<DutyPeriod> backup_duty_periods = duty_periods_;
        //std::vector<Cycle> backup_cycles = cycles_;
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
                //cycles_ = backup_cycles;
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





