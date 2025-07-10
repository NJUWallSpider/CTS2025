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


void CrewSchedule::initialize_crew_state() {
    // 1. Initialize time and location
    current_airport_ = crew_.initialStayStation;
  

    time_t start_time_t = Utils::parseTime(start_str_);
    last_task_end_time_ = std::chrono::system_clock::from_time_t(start_time_t)+ std::chrono::hours(8);

    // 2. Create and initialize a new duty period
    duty_periods_.emplace_back();
    initialize_duty_period(duty_periods_.back());

    // 3. Create and initialize a new cycle
    cycles_.emplace_back();
    initialize_cycle(cycles_.back());
}

void CrewSchedule::initialize_duty_period(DutyPeriod& duty_period){
    duty_period.is_FDuty = 0;
    duty_period.total_task_time = std::chrono::minutes(0);
    duty_period.total_flight_time = std::chrono::minutes(0);
    duty_period.flightCount = 0;
    duty_period.taskCount = 0;
    duty_period.tasks.clear();
    duty_period.startTime = last_task_end_time_;
    duty_period.endTime = last_task_end_time_;
}

void CrewSchedule::initialize_cycle(Cycle& cycle){
    cycle.startTime = last_task_end_time_;
    cycle.endTime = last_task_end_time_;

}