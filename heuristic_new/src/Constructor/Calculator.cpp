#include "SolutionConstruct.hpp"
#include "SolutionState.hpp"
#include <__chrono/duration.h>

double SolutionConstructor::calculate_avg_flight_hours(const SolutionState& solution){
    double avg_flight_hours = 0.0;
    std::chrono::minutes total_flight_hours = std::chrono::minutes(0);
    std::chrono::days total_calendar_days = std::chrono::days(0);
    for(auto [crew_id, duty_periods] : solution.crew_dutyperiods){

        for(auto duty_period : duty_periods){
           // if(duty_period.is_FDuty){
            total_flight_hours += duty_period.total_flight_time;

            total_calendar_days += std::chrono::floor<std::chrono::days>(duty_period.endTime) - std::chrono::floor<std::chrono::days>(duty_period.startTime) + std::chrono::days(1);
        //}  
    }
    }
    if (total_calendar_days.count() > 0) {
        avg_flight_hours = static_cast<double>(total_flight_hours.count()) / total_calendar_days.count();
    }
    return avg_flight_hours;
}

int SolutionConstructor::calculate_NFDP_count(const SolutionState& solution){
    int NFDP_count = 0;
    for(auto [crew_id, duty_periods] : solution.crew_dutyperiods){
        for(auto duty_period : duty_periods){
            if(!duty_period.is_FDuty){
                NFDP_count++;
            }
        }
    }
    return NFDP_count;
}