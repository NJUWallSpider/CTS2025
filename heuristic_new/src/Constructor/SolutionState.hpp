#pragma once

#include "./Scheduler/CrewSchedule.hpp" // For DutyPeriod
#include <map>
#include <string>
#include <vector>
#include <unordered_set>




// struct Turnaround {
//     std::vector<const Flight*> flights;
//     const Bus* positioning_bus = nullptr;
//     TimePoint startTime;
//     TimePoint endTime;
//     std::chrono::minutes total_flight_time;
//     std::string startAirport;
//     std::string endAirport;
// };

/**
 * @brief Represents a complete solution containing crew schedules and flight assignments
 * 
 * This struct maintains the state of a full scheduling solution, including:
 * - Crew duty period assignments
 * - Flight-to-crew mappings with qualification info
 * - Overall solution quality score
 * 
 * The solution can be evaluated and validated to ensure it meets all scheduling constraints.
 * Higher scores indicate better quality solutions based on optimization criteria.
 */
struct SolutionState {
    // Stores the sequence of crew IDs in the order they were assigned.
    std::vector<std::string> crew_assignment_order;

    // Maps a crew ID to their assigned sequence of duty periods.
    std::map<std::string, std::vector<DutyPeriod>> crew_dutyperiods;

    std::map<std::string, std::vector<Cycle>> crew_cycles;

    // A set of all flight IDs that have been assigned in this solution.
    // Maps flight ID to assignment info - vector of pairs (crew ID, whether crew is qualified)
    std::map<std::string, std::vector<std::pair<std::string, bool>>> flight_assignments;



    // The overall score of this solution. Higher is better.
    double score = 0.0;
    double avg_flight_hours = 0.0;
    int NFDP_count = 0;

    // TODO: Add methods to calculate score, check validity, etc.
}; 