#pragma once

#include "../../Loader/LoadData.hpp"
#include <vector>
#include <map>
#include <string>
#include <variant>
#include <chrono>
#include <limits>
#include <unordered_set>

struct SolutionState; // Forward Declaration

/**
 * @brief Internal helper struct for tracking detailed dynamic state of crew during evaluation
 * 
 * This struct maintains runtime state information for each crew member during schedule evaluation.
 * It tracks both duty-level metrics (flight tasks, total tasks, duty time) and cycle-level metrics
 * (total duties, total hours) to ensure all scheduling constraints are met.
 * 
 * The state is updated as tasks are assigned/removed and helps validate scheduling rules like:
 * - Maximum flight tasks per duty
 * - Maximum total tasks per duty  
 * - Maximum duty time limits
 * - Required rest periods between duties
 * - Maximum cycle duration and total cycle hours
 */

//// Helper function declarations
TimePoint get_start_time(const std::variant<Flight, Bus, GroundDuty>& task);
TimePoint get_end_time(const std::variant<Flight, Bus, GroundDuty>& task);
std::string get_arrival_airport(const std::variant<Flight, Bus, GroundDuty>& task);
std::string get_depa_airport(const std::variant<Flight, Bus, GroundDuty>& task);
std::chrono::minutes get_task_duration(const std::variant<Flight, Bus, GroundDuty>& task);

struct DutyPeriod {
    int can_be_extended = 1; // 1 if the duty period can be extended, 0 if the duty period cannot be extended
    //////////
    int is_FDuty = 0; // 1 if the duty period is a flight duty period, 0 if the duty period is a non-flight duty period
    std::chrono::minutes total_task_time = std::chrono::minutes(0); // total task time in the flight duty period (max 12 hours)
    std::chrono::minutes total_flight_time = std::chrono::minutes(0); // total flight time in the duty period (max 8 hours)
    //
    TimePoint startTime;
    TimePoint endTime;

    int flightCount = 0; // total flight tasks in the duty period(max 4)
    int taskCount = 0; // total tasks in the duty period (max 6)

    std::vector<std::variant<Flight, Bus, GroundDuty>> tasks;
};

struct Cycle{
    // int can_be_extended; // 1 if the cycle can be extended, 0 if the cycle cannot be extended
    TimePoint startTime;
    TimePoint endTime;
    std::vector<DutyPeriod> duty_periods;  // Changed from pointer to direct vector
    // std::chrono::minutes cycle_task_time = std::chrono::minutes(0); // Cycle task time
    // std::chrono::minutes cycle_duty_time = std::chrono::minutes(0); // Cycle duty time
    // std::chrono::minutes cycle_flight_time = std::chrono::minutes(0); // Cycle flight time
    // int cycle_task_count; 

    
};

// Forward declaration
using TimePoint = std::chrono::system_clock::time_point;
struct Flight;
struct Bus;
struct DutyPeriod; // Will define this later if needed for detailed duty tracking


class CrewSchedule {
public:
    /**
     * @brief Constructor
     * @param data All of the data loaded
     */
    explicit CrewSchedule(const DataLoader& data, const Crew& crew, 
            std::vector<DutyPeriod>& crew_duty_periods, 
            std::map<std::string, std::vector<std::pair<std::string, bool>>>& flight_assignments,
            std::vector<Cycle>& cycles);

    // Core construction flow
    void assign_tasks_to_crew();

    // Main construction method
    void construct_schedule_DFS();

private:
    const DataLoader& data_; 
    const Crew& crew_;
    std::vector<DutyPeriod>& duty_periods_; // List of duty periods for rule checking
    std::map<std::string, std::vector<std::pair<std::string, bool>>>& flight_assignments;
    
    std::vector<Cycle>& cycles_; // List of cycles for rule checking
    /////
    // std::string start_str_;


    
    // SolutionState& solution_;
    //
    // std::vector<std::variant<Flight, Bus>> schedule_; // Chronological list of tasks

    // --- Member Variables ---
    std::string current_airport_;
    TimePoint last_task_end_time_;
    std::unordered_set<std::string> all_bases_;
    
    // --- Rule Constants ---
    // duty period level
    const std::chrono::hours MIN_CONNECTION_TIME_BUS = std::chrono::hours(2);
    const std::chrono::hours MIN_CONNECTION_TIME_FLIGHT = std::chrono::hours(3);
    const std::chrono::hours MIN_REST_BEFORE_FDUTY = std::chrono::hours(12);
    const int MAX_FLIGHTS_PER_FDUTY = 4;
    const int MAX_TASKS_PER_FDUTY = 6;
    const std::chrono::hours MAX_FLY_TIME_PER_DUTY = std::chrono::hours(8);
    const std::chrono::hours MAX_DUTY_TIME_PER_FLIGHT_DUTY = std::chrono::hours(12);
    const std::chrono::hours MAX_REST_TIME_BETWEEN_FDUTY = std::chrono::hours(2);
    // cycle level
    const std::chrono::days MAX_CYCLE_CALENDAR_DAYS = std::chrono::days(4);
    const std::chrono::days MIN_REST_CALENDAR_DAYS = std::chrono::days(2);
    // schedule level
    const std::chrono::hours MAX_SCHE_FDP_TIME = std::chrono::hours(60);
    // THRESHOLD AND PROBABILITY
    const std::chrono::hours MIN_CONNECTION_GROUND_DUTY = std::chrono::hours(3);
    // for layover validity check
    const std::chrono::hours LAYOVER_THRESHOLD = std::chrono::hours(3);
    // when a FDP can not be extended, the probability of adding a positioning task && when the latest DP is NDP, the probability of adding a positioning task
    const int PROBABILITY_ADD_POSITIONING = 40;
    const int PROBABILITY_REQUIRE_QUALIFICATION = 100;
    const std::chrono::hours MIN_LEFT_TIME_FOR_FDUTY = std::chrono::hours(2);
    //
    const std::string DETECTOR_BUS_ID = "999999";
    
    void initialize_crew_state();
    void initialize_duty_period(DutyPeriod& duty_period);
    void initialize_cycle(Cycle& cycle);
    // ========Action=========
    void Action(std::variant<Flight, Bus, GroundDuty>& task_variant);
    void action_add_flight(const Flight& flight);
    void action_add_bus(const Bus& bus);
    void action_add_ground_duty(const GroundDuty& ground_duty);
    void action_remove_last_task();
    void action_add_duty_period();
    void action_allocate_pilot();
    void action_cycle();
    void action_try_add_positioning(const std::variant<Flight, Bus, GroundDuty>& candidate_task);
    // ========Update State=========
    // void update_DutyPeriod(DutyPeriod& duty_period);
    void Update();
    void Update_DutyPeriod();
    void Update_Cycle();

    // ========Check State=========
    bool Check_Action_validity(const std::variant<Flight, Bus, GroundDuty>& candidate_task);
    // Ground Duty Check
    bool check_Ground_Duty_validity(const std::variant<Flight, Bus, GroundDuty>& candidate_task);
    // Duty Level
    bool check_Flight_validity(const Flight& candidate_flight);
    bool Check_ddh_Bus_validity(const Bus& candidate_bus);
    // Cycle Level 
    bool Check_Cycle_validity();
    bool Check_Schedule_validity();
    bool Check_LayOver_validity();
    //
    bool check_return_validity(const Flight& candidate_flight);
   
    //=== 
    bool try_positioning(const Flight& flight);
    void try_position2Layover();
    GroundDuty get_lastest_ground_duty(); 

    //
    TimePoint get_rest_start_point();
    //

    std::string find_unassigned_qualified_flight();



};