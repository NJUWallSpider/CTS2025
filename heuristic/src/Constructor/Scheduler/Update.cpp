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


// void CrewSchedule::Update(){
//     /////////// GLOBLE STATE ///////////
//     // 1. update the last task end time
//     last_task_end_time_ = get_end_time(duty_periods_.back().tasks.back());
//     // 2. update the current airport
//     current_airport_ = get_arrival_airport(duty_periods_.back().tasks.back());
//     /////////// DUTY PERIOD STATE ///////////
//     Update_DutyPeriod();
//     // /////////// CYCLE STATE ///////////
//     // update_Cycle();
// }



// update the Duty info acorrding to the CURRENT TASKS
void CrewSchedule::Update_DutyPeriod(){

    // get the last duty period
    DutyPeriod& duty_period = duty_periods_.back();

    // 1. taskCount
    duty_period.taskCount = duty_period.tasks.size();

    // 2. flightCount
    duty_period.flightCount = std::count_if(duty_period.tasks.begin(), duty_period.tasks.end(), [](const auto& task) {
        return std::holds_alternative<Flight>(task);
    });
    duty_period.total_flight_time = std::accumulate(duty_period.tasks.begin(), duty_period.tasks.end(), std::chrono::minutes(0), [](std::chrono::minutes sum, const auto& task) {
        if(std::holds_alternative<Flight>(task)){
            return sum + std::chrono::minutes(std::get<Flight>(task).flyTime_mins);
        }
        return sum;
    });


    // 3. infer the duty type: if there's at least one flight in the duty period, it's a flight duty period
    duty_period.is_FDuty = (duty_period.flightCount > 0);
    // 4. Start and End Time
    duty_period.startTime = get_start_time(duty_period.tasks.front());
    ///////////////NOTE: the end time of a FDuty should be the end time of the last flight task
    if(duty_period.is_FDuty){
        // get the last flight task
        const auto& last_flight = std::find_if(duty_period.tasks.rbegin(), duty_period.tasks.rend(), [](const auto& task) {
            return std::holds_alternative<Flight>(task);
        });
        duty_period.endTime = get_end_time(*last_flight);
    }
    else{
        duty_period.endTime = get_end_time(duty_period.tasks.back());
    }

    // total time
    duty_period.total_task_time = std::chrono::duration_cast<std::chrono::minutes>(duty_period.endTime - duty_period.startTime);

    // 4. infer the duty extendability: 
    // if the duty period : 
    //(1) is a flight duty period, 
    //(2) the last task is not a bus(positioning)
    // [..., ..., BUS] means the FlightDutyPeriod is completed
    duty_period.can_be_extended = (!(duty_period.is_FDuty && std::holds_alternative<Bus>(duty_period.tasks.back())));
    // || (!(duty_period.is_FDuty && std::chrono::hours(2) + duty_period.total_flight_time > MAX_FLY_TIME_PER_DUTY))
    // || (!(duty_period.is_FDuty && std::chrono::hours(2) + duty_period.total_task_time > MAX_DUTY_TIME_PER_FLIGHT_DUTY));

    // set the previous duties' all unextendable
    for(int i = 0; i < duty_periods_.size() - 1; i++){
        duty_periods_[i].can_be_extended = 0;
    }
    
    }

// update the cycle info acorrding to the current duty periods
void CrewSchedule::Update_Cycle(){

    // 1. get the last cycle
    Cycle& cycle = cycles_.back();
    
    // 2. update the cycle start and end time
    // 2.1  the start time of a cycle should be the start time of the first DP
    cycle.startTime = cycle.duty_periods.front().startTime;

    // 2.2 the end time of a cycle should be the end time of the last FDP
    const auto& last_fdp = std::find_if(cycle.duty_periods.rbegin(), cycle.duty_periods.rend(), [](const auto& duty_period) {
        return duty_period.is_FDuty;
    });
    cycle.endTime = last_fdp->endTime;

    // 3. update the cycle can_be_extended flag
    // if the calendar days of the cycle is less than 4, then the cycle can be extended
}
