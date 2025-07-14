#include "CrewSchedule.hpp"
#include "../../Loader/Utils.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <variant>
#include <map>
#include <limits>
#include <algorithm>
#include <numeric>
#include <random>

// the candidate already qualified the spot connection, chronologically sorted
bool CrewSchedule::Check_Action_validity(const Turnaround& candidate_turnaround) {

    if (duty_periods_.back().tasks.empty()) {
        return true;
        }

    // get the lastest task
    const auto& last_task_end_time = get_end_time(duty_periods_.back().tasks.back());

    if(candidate_turnaround.startTime - last_task_end_time >= MIN_REST_BEFORE_FDUTY){
        return true;
    }

    return false;
}
// if OK, return true
bool CrewSchedule::check_Ground_Duty_validity(const std::variant<Flight, Bus, GroundDuty>& candidate_task){
    // if the candidate task is the ground duty of the crew, directly return true
    if(std::holds_alternative<GroundDuty>(candidate_task) ){
        return true;
    }

    // get the lastest ground duty [(TASK) , ..., <GRD>]
    const auto& last_ground_duty = get_lastest_ground_duty();
    // If no ground duty exists, return true
    if (last_ground_duty.id.empty()) {
        return true;
    }
    
    //1. TIME : Avoid the overlap between the lastest ground duty and the candidate task
    if(last_ground_duty.start_time < get_end_time(candidate_task)){
        return false;
    }
    //2. AIRPORT : use the threshold to check the airport connection
    // if(get_arrival_airport(candidate_task) != last_ground_duty.airport){

    if(get_arrival_airport(candidate_task) != last_ground_duty.airport && last_ground_duty.start_time - get_end_time(candidate_task) < MIN_CONNECTION_GROUND_DUTY){
            return false;
    }

    return true;
}
// if OK, return true
bool CrewSchedule::check_Flight_validity(const Flight& candidate_flight){
    //TODO: if flight is assigned to a crew, return false
    /////////

    // Check the qualification of the candidate flight
    // For now , // 100% chance to require qualification
    // std::random_device rd;
    // std::mt19937 gen(rd());
    // std::uniform_int_distribution<> dis(0, 99);
    // if (dis(gen) < PROBABILITY_REQUIRE_QUALIFICATION) {
    //     if (crew_.qualifications.find(candidate_flight.id) == crew_.qualifications.end()) {
    //         return false;
    //     }
    // }
    ////////////
    // if(candidate_flight.depaAirport != current_airport_){
    //     if(!try_positioning(candidate_flight)){
    //         return false;
    //     }
    // }

    // if last P not empty, get the lastest task
    const auto& lastest_duty_period = duty_periods_.back();

    //// If the Crew is on a EXTENDABLE FDuty ////
    if(lastest_duty_period.is_FDuty && lastest_duty_period.can_be_extended){

        // // if the layover time is too long, go for a bus positioning
        // if(candidate_flight.std - lastest_duty_period.endTime > std::chrono::hours(15)){
        //     return false;
        // }
        
        //1. start a new FDP
        if(candidate_flight.std - lastest_duty_period.endTime > MIN_REST_BEFORE_FDUTY ){
            // if(std::chrono::floor<std::chrono::days>(candidate_flight.sta) - std::chrono::floor<std::chrono::days>(candidate_flight.std)  > std::chrono::hours(0)){
            //     return false;
            // }
            
                return true;
            
        }


        /////////////////DutyPeriod max task num check////////////
        if(lastest_duty_period.taskCount >= MAX_TASKS_PER_FDUTY){
            return false;
        }
        ///////DutyPeriod max flight num check////////////////////
        if(lastest_duty_period.flightCount >= MAX_FLIGHTS_PER_FDUTY){
            return false;
        }

        
        // 2. add to current FDP
        // 2.1 check the FD max flight time （8）&& FD max task time （12）
        if(lastest_duty_period.total_flight_time + std::chrono::minutes(candidate_flight.flyTime_mins) > MAX_FLY_TIME_PER_DUTY ||
        candidate_flight.sta - lastest_duty_period.startTime > MAX_DUTY_TIME_PER_FLIGHT_DUTY){
            // try to start a new FDP if the MAX_REST_TIME_BETWEEN_FDUTY is met

            return false;
        }

        // 2.2 check the connection time （3）
        //// if the lastest task is a flight////
        if(std::holds_alternative<Flight>(lastest_duty_period.tasks.back())){
            // if the aircraft id of the candidate flight is not the same as the lastest task, then check the connection time (3)
            if(candidate_flight.aircraftNo != std::get<Flight>(lastest_duty_period.tasks.back()).aircraftNo){
                // check the connection time (3)
                // return false;
                if(get_start_time(candidate_flight) - last_task_end_time_ < MIN_CONNECTION_TIME_FLIGHT){
                    return false;
                }
                // else{
                //     if(lastest_duty_period.flightCount % 2 == 0){
                //         if(!check_return_validity(candidate_flight)){
                //             return false;
                //         }
                //     }
                // }
            }
            // else{
            //     if(lastest_duty_period.flightCount % 2 == 0){
            //         if(!check_return_validity(candidate_flight)){
            //             return false;
            //         }
            //     }
            // }
        }

        // // check the layover validity (using a threshold)
        // if( lastest_duty_period.taskCount >= 2){
        //     std::vector<std::string> layover_spots = data_.getLayoverStations();
        //     if(std::find(layover_spots.begin(), layover_spots.end(), candidate_flight.arriAirport) == layover_spots.end()){
        //         return false;
        //     }
        // }
        return true;
    }
    // If the lastest duty period is a FDuty but not extendable (bus positioning in the end of the FDuty)
    // suppose start a new FDuty which should consider the min rest time between two FDuty
    else if(lastest_duty_period.is_FDuty && !lastest_duty_period.can_be_extended){
        // try to start a new FDuty if the MIN_REST_BEFORE_FDUTY is met
        if(candidate_flight.std - get_end_time(lastest_duty_period.tasks.back()) < MIN_REST_BEFORE_FDUTY){
            return false;
        }
            //         if(std::chrono::floor<std::chrono::days>(candidate_flight.sta) - std::chrono::floor<std::chrono::days>(candidate_flight.std)  > std::chrono::hours(0)){
            //     return false;
            // }

        return true;

    }
    // If the lastest duty period is a NFDuty
    else if(!lastest_duty_period.is_FDuty){
        if(duty_periods_.size() < 2){
            return true;
        }
        if(candidate_flight.std - lastest_duty_period.endTime > MIN_REST_BEFORE_FDUTY){
            return true;
        }
        else if(candidate_flight.sta - lastest_duty_period.startTime < MAX_DUTY_TIME_PER_FLIGHT_DUTY && candidate_flight.std - lastest_duty_period.endTime > MIN_CONNECTION_TIME_BUS){
                
                const auto& second_lastest_duty_period = duty_periods_[duty_periods_.size() - 2];
                if(lastest_duty_period.startTime - get_end_time(second_lastest_duty_period.tasks.back()) < MIN_REST_BEFORE_FDUTY){
                    return false;
                }
                else{
                    return true;
                }
            }
        else{
            return false;  }

        
    }
    return true;
}

// according to the current logic, the bus candidates will be considered only if the flight candidates are all infeasible.
bool CrewSchedule::Check_ddh_Bus_validity(const Bus& candidate_bus){
    // if last P empty, return true
    if(duty_periods_.back().tasks.empty()){
        return true;
    }
    if(candidate_bus.id == DETECTOR_BUS_ID){
        return true;
    }
    auto layover_spots = data_.getLayoverStations();
    // get the lastest duty period
    const auto& latest_duty_period = duty_periods_.back();

    // if the lastest duty period is a FDuty and extendable
    if(latest_duty_period.is_FDuty && latest_duty_period.can_be_extended){

        if(current_airport_ == crew_.base && candidate_bus.arriAirport != crew_.base){
            return false;
        }

        if(candidate_bus.td - latest_duty_period.endTime > MIN_REST_BEFORE_FDUTY){
            return true;
        }
        else if(candidate_bus.td - latest_duty_period.endTime >= MIN_CONNECTION_TIME_BUS){
            if(std::find(layover_spots.begin(), layover_spots.end(), candidate_bus.arriAirport) == layover_spots.end()){
                return false;
            }
            // add the bus to the last duty period
            return true;
        }
        return false;
    }
    else if(latest_duty_period.is_FDuty && !latest_duty_period.can_be_extended){

        // start a new NFDP with a probability
        if(!latest_duty_period.tasks.empty()){
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 99);
            if(dis(gen) < PROBABILITY_ADD_POSITIONING){
                    return true;
                }

            auto& last_task = latest_duty_period.tasks.back();
            if(candidate_bus.td - get_end_time(last_task) >= MIN_REST_BEFORE_FDUTY){
                return true;
                
            }
        }
        return false;
        


    }
    // if the lastest duty period is a NFDuty
    else{

        std::string unassigned_qualified_flight_id = find_unassigned_qualified_flight();

        Flight unassigned_qualified_flight;
        for(const auto& flight : data_.getFlights()){
            if(flight.id == unassigned_qualified_flight_id){
                unassigned_qualified_flight = flight;
                break;
            }
        }
        if(unassigned_qualified_flight.depaAirport == candidate_bus.arriAirport || candidate_bus.arriAirport == crew_.base){
            return true;
        }
        else{
            return false;
        }

        

        // // the probability will gradiently decreased as the number of bus in the NFDuty increases
        // std::random_device rd;
        // std::mt19937 gen(rd());
        // std::uniform_int_distribution<> dis(0, 99);
        // if(dis(gen) < PROBABILITY_ADD_POSITIONING - latest_duty_period.taskCount * 10){
        //     return true;
        // }
    }
    return false;
}


bool CrewSchedule::Check_Cycle_validity(){
    // get the latest cycle
    Cycle& latest_cycle = cycles_.back();
    // get the latest duty period
    DutyPeriod& latest_duty_period = duty_periods_.back();
    // if the latest duty is a FDuty
    if(latest_duty_period.is_FDuty){
        // get the start and end day of the cycle
        auto cycle_start_day = std::chrono::floor<std::chrono::days>(latest_cycle.startTime);
        auto potential_cycle_end_day = std::chrono::floor<std::chrono::days>(latest_duty_period.endTime) + std::chrono::days(1);
        auto cycle_calendar_days = std::chrono::duration_cast<std::chrono::days>(potential_cycle_end_day - cycle_start_day).count() ;
        
        // if the crossed calendar days between the end time of the last FDP and the start time of the current cycle is greater than MAX CYCLE CALENDAR DAYS
        if(cycle_calendar_days > std::chrono::duration_cast<std::chrono::days>(MAX_CYCLE_CALENDAR_DAYS).count()){
            // if the crossed calendar days between the start time of the current FDP and the end time of the second latest DP > MIN_REST_CALENDAR_DAYS, then try to add a new cycle
            // get the second latest duty period
            // Check if there are at least 2 duty periods before accessing
            TimePoint rest_start_day = std::chrono::floor<std::chrono::days>(get_rest_start_point()) + std::chrono::days(1);
            // Get the start of the day for the latest duty period start time
            auto potential_rest_end_day = std::chrono::floor<std::chrono::days>(latest_duty_period.startTime);
            // Check if the rest period spans at least 2 calendar days
            auto rest_calendar_days = std::chrono::duration_cast<std::chrono::days>(potential_rest_end_day - rest_start_day).count();
            
            if(rest_calendar_days < std::chrono::duration_cast<std::chrono::days>(MIN_REST_CALENDAR_DAYS).count()){
                return false;
            }
            
        }

    }

    return true;
}



bool CrewSchedule::Check_Schedule_validity(){
    // check the cycle max flight time
    // iterate the DutyPeriods
    std::chrono::hours total_fdp_time = std::chrono::hours(0);
    for(const auto& duty_period : duty_periods_){
        if(!duty_period.is_FDuty){
            continue;
        }
        total_fdp_time += std::chrono::duration_cast<std::chrono::hours>(duty_period.endTime - duty_period.startTime);
    }
    if(total_fdp_time > MAX_SCHE_FDP_TIME){
        return false;
    }
    return true;
}

bool CrewSchedule::Check_LayOver_validity(){
    
    std::vector<std::string> layover_spots = data_.getLayoverStations();
    // get the last unextendable duty period
    const auto& last_unextendable_duty_period = std::find_if(duty_periods_.rbegin(), duty_periods_.rend(), [](const auto& duty_period) {
        return !duty_period.can_be_extended;
    });
    // if the last unextendable DP is not empty, get the end airport of the last unextendable duty period
    if(last_unextendable_duty_period != duty_periods_.rend()){
        if (!last_unextendable_duty_period->tasks.empty()) {
            const auto& last_end_airport = get_arrival_airport(last_unextendable_duty_period->tasks.back());
                    if(std::find(layover_spots.begin(), layover_spots.end(), last_end_airport) == layover_spots.end() || crew_.base != last_end_airport) return false;
    
        } 
    }
    
    return true;
}


// // according to the current logic, the bus candidates will be considered only if the flight candidates are all infeasible.
// bool CrewSchedule::Check_ddh_Bus_validity(const Bus& candidate_bus){
//     // if last P empty, return true
//     if(duty_periods_.empty()){
//         return true;
//     }
//     // get the lastest duty period
//     const auto& lastest_duty_period = duty_periods_.back();
//     // if the lastest duty period is a FDuty and extendable
//     if(lastest_duty_period.is_FDuty && lastest_duty_period.can_be_extended){
//         // /////////// CHECK THE CONNECTION TIME ///////////
//         // get the lastest task of the lastest FDutyPeriod
//         const auto& last_task = lastest_duty_period.tasks.back();
//         // suppose the lastest task of the lastest EXTENDABLE FDutyPeriod is a flight 
//         // suppose [..., ..., FLT]
//         if(std::holds_alternative<Flight>(last_task)){
//             const auto& last_flight = std::get<Flight>(last_task);
//             // check the connection time
//             if(candidate_bus.td - last_flight.sta < MIN_CONNECTION_TIME_BUS){
//             return false;
//             }
//         }
//     }
//     else if(lastest_duty_period.is_FDuty && !lastest_duty_period.can_be_extended){
//         // start a new NFDP with a probability
//         std::random_device rd;
//         std::mt19937 gen(rd());
//         std::uniform_int_distribution<> dis(0, 99);
//         if(dis(gen) < PROBABILITY_ADD_POSITIONING){
//             return true;
//         }
//     }
//     // if the lastest duty period is a NFDuty
//     else{
//         // the probability will gradiently decreased as the number of bus in the NFDuty increases
//         std::random_device rd;
//         std::mt19937 gen(rd());
//         std::uniform_int_distribution<> dis(0, 99);
//         if(dis(gen) < PROBABILITY_ADD_POSITIONING - lastest_duty_period.taskCount * 10){
//             return true;
//         }
//     }
//     return true;

// }

bool CrewSchedule::try_positioning(const Flight& flight){
    DutyPeriod& last_duty_period = duty_periods_.back();

    // get the lastest task
    std::variant<Flight, Bus, GroundDuty>& lastest_task = last_duty_period.tasks.back();

    // firstly, try to find a BUS (A->B) 
    for(const auto& bus : data_.getBuses()){

        if(bus.depaAirport == current_airport_ && bus.arriAirport == flight.depaAirport && bus.td >= last_task_end_time_ && bus.ta <= get_start_time(flight)){
                // add the first found bus to the last duty period
                if(Check_ddh_Bus_validity(bus)){
                    action_add_bus(bus);
                    // 1. update the last task end time
                    last_task_end_time_ = get_end_time(duty_periods_.back().tasks.back());
                    // 2. update the current airport
                    current_airport_ = get_arrival_airport(duty_periods_.back().tasks.back());
                    Update_DutyPeriod();
                    return true;
                }
        }

        
    }
    return false;

        
    
}

TimePoint CrewSchedule::get_rest_start_point(){
    // // get the lastest duty period
    // const auto& lastest_duty_period = duty_periods_.back();

    // if(duty_periods_.size() < 2){
    //     return lastest_duty_period.endTime; // No second latest duty period to check
    // }

    // const auto& second_latest_duty_period = duty_periods_[duty_periods_.size() - 2];
    // // from the end of the NFD, find the continuous GroundDuty sequence that the groundDuty.isDuty = 0, get the end time of the last GroundDuty and the start time of the first GroundDuty
    // TimePoint rest_start_time = second_latest_duty_period.endTime;
    // // Check if tasks is not empty before starting the loop

    TimePoint rest_start_time;
    // if(duty_periods_.back().tasks.empty()){
    //     std::string start_str = "2025/5/27 00:00";
    //     time_t start_time_t = Utils::parseTime(start_str);
    //     return std::chrono::system_clock::from_time_t(start_time_t)+ std::chrono::hours(8);
    // }

    bool found_rest_start_time = false;
    if ( duty_periods_.size() >= 2){
        for(int j = static_cast<int>(duty_periods_.size()) - 2; j >= 0 ; j--){
            const auto& current_duty_period = duty_periods_[j];
            if(current_duty_period.tasks.empty()){
                continue;
            }
            for(int i = static_cast<int>(current_duty_period.tasks.size()) - 1; i >= 0; i--){
                if(std::holds_alternative<GroundDuty>(current_duty_period.tasks[i])){
                    const auto& ground_duty = std::get<GroundDuty>(current_duty_period.tasks[i]);
                    if(ground_duty.is_duty == 0){
                        continue;
                    }
                    else{ 
                        rest_start_time = ground_duty.end_time; 
                        found_rest_start_time = true;
                        break;}
                }
                else{
                    rest_start_time = get_end_time(current_duty_period.tasks[i]);
                    found_rest_start_time = true;
                    break;
                    }
            }
            if(found_rest_start_time){
                break;
            }
        }
    }

    return rest_start_time;
}

std::string CrewSchedule::find_unassigned_qualified_flight(){

    Flight temp_flight;
    for(const auto& flight_id: crew_.qualifications){
        if(flight_assignments.find(flight_id) == flight_assignments.end()){
            return flight_id;
        }
    }
    return "";
}   

bool CrewSchedule::check_return_validity(const Flight& candidate_flight){
    DutyPeriod& last_duty_period = duty_periods_.back();
    if(last_duty_period.tasks.empty()){
        return true;
    }
    if(last_duty_period.flightCount % 2 == 0){
        // get the next flight in the same aircraft behind the candidate flight
        const auto& aircraft_flights = data_.getAircraftToFlights().at(candidate_flight.aircraftNo);
        
        auto next_flight_it = std::find_if(aircraft_flights.begin(), aircraft_flights.end(),
            [&candidate_flight](const Flight& f) {
                return f.std > candidate_flight.std;
            });

        if(next_flight_it == aircraft_flights.end()){
            return true;
        }

        const Flight& next_flight = *next_flight_it;
        //////
        if(next_flight.arriAirport != crew_.base){
            return false;
        }
        else{
            if(next_flight.sta - last_duty_period.startTime > MAX_DUTY_TIME_PER_FLIGHT_DUTY){
                return false;
            }
            if(std::chrono::minutes(next_flight.flyTime_mins + candidate_flight.flyTime_mins) + last_duty_period.total_flight_time > MAX_FLY_TIME_PER_DUTY){
                return false;
            }
        }
        return true;
    }
    return true;
}