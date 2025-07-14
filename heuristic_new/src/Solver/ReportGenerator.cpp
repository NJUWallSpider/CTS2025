#include "ReportGenerator.hpp"
#include "../Constructor/Scheduler/CrewSchedule.hpp"
#include "../Loader/LoadData.hpp"
#include <fstream>
#include <iostream>
#include <variant>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <set>
#include <unordered_set>

bool ReportGenerator::validate_crew_flight_consistency(const SolutionState& solution, const std::string& output_path) {
    std::ofstream out(output_path);
    // Build a map of flight assignments from crew schedules
    std::map<std::string, std::vector<std::pair<std::string, bool>>> crew_schedule_flights;
    
    // First pass: collect all flight assignments from crew schedules
    for (const auto& [crew_id, duty_periods] : solution.crew_dutyperiods) {
        for (const auto& duty_period : duty_periods) {
            for (const auto& task : duty_period.tasks) {
                std::visit([&](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, Flight>) {
                        crew_schedule_flights[arg.id].push_back({crew_id, false}); // We'll update qualification later
                    }
                }, task);
            }
        }
    }

    // Second pass: update qualification status from flight_assignments
    for (auto& [flight_id, crews] : crew_schedule_flights) {
        auto it = solution.flight_assignments.find(flight_id);
        if (it != solution.flight_assignments.end()) {
            for (auto& crew_pair : crews) {
                // Find this crew's qualification in flight_assignments
                auto assign_it = std::find_if(it->second.begin(), it->second.end(),
                    [&crew_pair](const auto& assignment) {
                        return assignment.first == crew_pair.first;
                    });
                if (assign_it != it->second.end()) {
                    crew_pair.second = assign_it->second;
                }
            }
        }
    }

    // Compare the two maps
    bool is_consistent = true;

    // Check if all flights in crew schedules are in flight_assignments
    for (const auto& [flight_id, crews] : crew_schedule_flights) {
        auto it = solution.flight_assignments.find(flight_id);
        if (it == solution.flight_assignments.end()) {
            out << "Flight " << flight_id << " found in crew schedules but not in flight_assignments\n";
            is_consistent = false;
            continue;
        }

        // Check if all crews assigned to this flight match
        std::set<std::string> schedule_crews, assignment_crews;
        for (const auto& crew : crews) {
            schedule_crews.insert(crew.first);
        }
        for (const auto& crew : it->second) {
            assignment_crews.insert(crew.first);
        }

        if (schedule_crews != assignment_crews) {
            out << "Crew mismatch for flight " << flight_id << ":\n";
            out << "  In schedules: ";
            for (const auto& crew : schedule_crews) out << crew << " ";
            out << "\n  In assignments: ";
            for (const auto& crew : assignment_crews) out << " ";
            out << "\n";
            is_consistent = false;
        }

        // Check if qualifications match
        for (const auto& crew_pair : crews) {
            auto assign_it = std::find_if(it->second.begin(), it->second.end(),
                [&crew_pair](const auto& assignment) {
                    return assignment.first == crew_pair.first;
                });
            if (assign_it != it->second.end() && crew_pair.second != assign_it->second) {
                out << "Qualification mismatch for flight " << flight_id 
                         << ", crew " << crew_pair.first << ":\n"
                         << "  In schedule: " << (crew_pair.second ? "qualified" : "not qualified")
                         << "\n  In assignments: " << (assign_it->second ? "qualified" : "not qualified") << "\n";
                is_consistent = false;
            }
        }
    }

    // Check if all flights in flight_assignments are in crew schedules
    for (const auto& [flight_id, crews] : solution.flight_assignments) {
        if (crew_schedule_flights.find(flight_id) == crew_schedule_flights.end()) {
            out << "Flight " << flight_id << " found in flight_assignments but not in crew schedules\n";
            is_consistent = false;
        }
    }

    if (is_consistent) {
        out << "Crew schedules and flight assignments are consistent.\n";
    }

    return is_consistent;
}

// Helper to convert TimePoint to a formatted string
inline std::string format_time(const TimePoint& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp - std::chrono::hours(8));
    std::tm tm = *std::localtime(&t);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return ss.str();
}

// Helper to format a duration in minutes to "Xh Ym"
inline std::string format_duration(const std::chrono::minutes& duration_mins) {
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration_mins);
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration_mins % std::chrono::hours(1));
    std::stringstream ss;
    ss << hours.count() << "h " << minutes.count() << "m";
    return ss.str();
}

// Helper to print task details in a more structured way
void print_task_details_pretty(std::ofstream& out, const std::variant<Flight, Bus, GroundDuty>& task, const std::map<std::string, std::vector<std::pair<std::string, bool>>> flight_assignments, std::string crew_id) {
    std::visit([&out, &flight_assignments, &crew_id](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Flight>) {
            bool assigned = false;
            for(const auto& assignment : flight_assignments.at(arg.id)){
                if(assignment.first == crew_id){
                    assigned = assignment.second;
                    break;
                }
            }
            out << "    [" << format_time(arg.std) << " - " << format_time(arg.sta) << "] Flight " << arg.id
                << "\t(" << arg.aircraftNo << ")"
                << "\t(" << arg.depaAirport << " -> " << arg.arriAirport << ")"<< "\t"<< (assigned ? "Qualified" : "DDH")    <<"\n";
        } else if constexpr (std::is_same_v<T, Bus>) {
            out << "    [" << format_time(arg.td) << " - " << format_time(arg.ta) << "] Bus " << arg.id
                << "\t(" << arg.depaAirport << " -> " << arg.arriAirport << ")\n";
        } else if constexpr (std::is_same_v<T, GroundDuty>) {
            out << "    [" << format_time(arg.start_time) << " - " << format_time(arg.end_time) << "] Ground Duty"
                << "\t(" << arg.is_duty << ")"
                << "\t(" << arg.airport << ")\n";
        }
    }, task);
}

void ReportGenerator::generate_schedule_report(const SolutionState& solution, const DataLoader& data, const std::string& output_path, const std::chrono::steady_clock::time_point& start_time) {
    auto end_time = std::chrono::steady_clock::now();
    auto runtime = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    std::ofstream out(output_path);
    if (!out) {
        std::cerr << "Error: Could not open file " << output_path << " for writing." << std::endl;
        return;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);

    out << "==================================================\n";
    out << "           CREW SCHEDULE REPORT\n";
    out << "==================================================\n";
    out << "Report Generated: " << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "\n";
    out << "Total Runtime:    " << runtime.count() << " seconds\n\n";
    out << "Best Score:       " << solution.score << "\n\n";
    out << "Average Flight Hours: " << solution.avg_flight_hours << "\n\n";
    out << "NFDP Count: " << solution.NFDP_count << "\n\n";
    
    const auto& all_crews = data.getCrews();
    auto layover_spots = data.getLayoverStations();
    int NonLayover = 0;
    int total_duty_periods = 0;
    int NonBase_layover = 0;
    int Invalid_layover = 0;
    std::unordered_set<std::string> Invalid_layover_set;
    for (const auto& crew_id : solution.crew_assignment_order) {
        if (solution.crew_dutyperiods.find(crew_id) == solution.crew_dutyperiods.end()) {
            continue;
        }
        const auto& duty_periods = solution.crew_dutyperiods.at(crew_id);
        if (all_crews.find(crew_id) == all_crews.end()) continue;
        const auto& crew_data = all_crews.at(crew_id);

        out << "--------------------------------------------------\n";
        out << "Crew ID: " << crew_id << "\n";
        out << "Base:    " << crew_data.base << "\n";
        out << "--------------------------------------------------\n\n";

        for (size_t i = 0; i < duty_periods.size(); ++i) {
            const auto& duty_period = duty_periods[i];
            
            out << "  === Duty Period " << i + 1 << " ===\n";
            auto duty_duration = std::chrono::duration_cast<std::chrono::minutes>(duty_period.endTime - duty_period.startTime);
            out << "  Duration: " << format_duration(duty_duration)
                << " | Tasks: " << duty_period.taskCount
                << " | Flights: " << duty_period.flightCount
                << " | Flight Time: " << format_duration(duty_period.total_flight_time) << "\n";
            out << "  Start: " << format_time(duty_period.startTime) << "\n";
            out << "  End:   " << format_time(duty_period.endTime) << "\n\n";
            if(!duty_period.tasks.empty()){
                out << "    Layover Airport:  " << get_arrival_airport(duty_period.tasks.back()) << "\n";
                out << "    Layover Validity:  " << (std::find(layover_spots.begin(), layover_spots.end(), get_arrival_airport(duty_period.tasks.back())) != layover_spots.end() ? "Yes" : "No") << "\n";
                NonBase_layover += get_arrival_airport(duty_period.tasks.back()) != crew_data.base;
                if(std::find(layover_spots.begin(), layover_spots.end(), get_arrival_airport(duty_period.tasks.back())) == layover_spots.end()){
                    Invalid_layover++;
                    Invalid_layover_set.insert(get_arrival_airport(duty_period.tasks.back()));
                   // std::cout << "Invalid Layover: " << crew_id << " " << (duty_period.is_FDuty ? "FDP" : "is not")<<"\tIndex: " << i << "\tTask count: " << duty_period.taskCount << "\tFlight count: " << duty_period.flightCount << "\tArrival airport: " << get_arrival_airport(duty_period.tasks.back()) << "\n";
                }
            }
            total_duty_periods++;
            auto sorted_tasks = duty_period.tasks;
            std::sort(sorted_tasks.begin(), sorted_tasks.end(), [](const auto& a, const auto& b){
                return get_start_time(a) < get_start_time(b);
            });
            
            for (const auto& task : sorted_tasks) {
                print_task_details_pretty(out, task,solution.flight_assignments,crew_id);
            }
            out << "\n";

            if (i + 1 < duty_periods.size()) {
                const auto& next_duty_period = duty_periods[i+1];
                auto rest_duration = std::chrono::duration_cast<std::chrono::minutes>(next_duty_period.startTime - duty_period.endTime);
                std::string rest_location;
                if (!duty_period.tasks.empty()) {
                    rest_location = get_arrival_airport(duty_period.tasks.back());
                } else {
                    rest_location = crew_data.base;
                }

                out << "  --- Rest Period ---\n";
                out << "  Duration: " << format_duration(rest_duration) << "\n";
                out << "  Location: " << rest_location << "\n\n";
            }
        }
        std::vector<Cycle> crew_cycles = solution.crew_cycles.find(crew_id)->second;
        // for(size_t i = 0; i < crew_cycles.size(); i++) {
            
        //     if( i == 1){
        //         auto rest_duration = std::chrono::duration_cast<std::chrono::minutes>(crew_cycles[i].startTime - crew_cycles[i-1].endTime);
        //         out << "  --- Rest Period ---\n";
        //         out << "  Duration: " << format_duration(rest_duration) << "\n";
        //         out << "  Location: " << get_arrival_airport(crew_cycles[i-1].duty_periods.back().tasks.back()) << "\n\n";
        //     }

        //     const auto& cycle = crew_cycles[i];
        //     out << "  === Cycle " << i + 1 << " ===\n";
        //     auto cycle_duration = std::chrono::duration_cast<std::chrono::minutes>(cycle.endTime - cycle.startTime);
        //     out << "  Duration: " << format_duration(cycle_duration) << "\n";
        //     out << "  Start: " << format_time(cycle.startTime) << "\n";
        //     out << "  End:   " << format_time(cycle.endTime) << "\n\n";

        // }
    }
    std::string directory = output_path.substr(0, output_path.find_last_of("/\\") + 1);
    std::ofstream out_layover_validity(directory + "layover_validity.txt");
    out_layover_validity << "Total Duty Periods: " << total_duty_periods << "\n";
    out_layover_validity << "Total Non-Base Layover: " << NonBase_layover << "\n";
    out_layover_validity << "Total Invalid Layover: " << Invalid_layover << "\n";
    out_layover_validity << "Invalid Layover Set Size: " << Invalid_layover_set.size() << "\n";
    out_layover_validity << "Invalid Layover Set: ";
    for(const auto& layover : Invalid_layover_set){
        out_layover_validity << layover << " ";
    }
    out_layover_validity << "\n";
    out_layover_validity << "Average Invalid Layover: " << (double)Invalid_layover / total_duty_periods << "\n";
    std::cout << "Schedule report generated at " << output_path << std::endl;
}

void ReportGenerator::generate_submission_csv(const SolutionState& solution, const std::string& output_path) {
    std::ofstream out(output_path);
    if (!out) {
        std::cerr << "Error: Could not open file " << output_path << " for writing." << std::endl;
        return;
    }

    out << "crewId,taskId,isDDH\n";

    for (const auto& [crew_id, duty_periods] : solution.crew_dutyperiods) {
        for (const auto& duty_period : duty_periods) {
            for (const auto& task : duty_period.tasks) {
                std::visit([&](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    std::string taskId;
                    bool isDDH = false;

                    if constexpr (std::is_same_v<T, Flight>) {
                        taskId = arg.id;
                        // Check if the flight is a deadhead flight for this crew
                        auto it = solution.flight_assignments.find(arg.id);
                        if (it != solution.flight_assignments.end()) {
                            for (const auto& assignment : it->second) {
                                if (assignment.first == crew_id) {
                                    isDDH = !assignment.second; 
                                    break;
                                }
                            }
                        }
                    } else if constexpr (std::is_same_v<T, Bus>) {
                        if(arg.id == "999999"){
                            return ;
                        }
                        taskId = arg.id;
                        isDDH = true; // Bus is always DDH
                    } else if constexpr (std::is_same_v<T, GroundDuty>) {
                        // Skip GroundDuty tasks
                        return;
                    }
                    out << crew_id << "," << taskId << "," << (isDDH ? "1" : "0") << "\n";
                }, task);
            }
        }
    }

    std::cout << "Submission CSV generated at " << output_path << std::endl;
} 

void ReportGenerator::generate_assignment_report(const SolutionState& solution, const DataLoader& data, const std::string& output_path) {
    std::ofstream out(output_path);
    if (!out) {
        std::cerr << "Error: Could not open file " << output_path << " for writing." << std::endl;
        return;
    }

    // 1. Get all flights and group by aircraftNo
    const auto& all_flights = data.getFlights();
    std::map<std::string, std::vector<Flight>> flights_by_aircraft;
    for (const auto& flight_obj : all_flights) {
        flights_by_aircraft[flight_obj.aircraftNo].push_back(flight_obj);
    }

    // 2. Sort flights within each aircraft group chronologically and create a flat list
    std::vector<Flight> sorted_flights;
    std::vector<std::string> aircraft_order;
    for (auto const& [aircraft_no, flights] : flights_by_aircraft) {
        aircraft_order.push_back(aircraft_no);
    }
    std::sort(aircraft_order.begin(), aircraft_order.end());

    for (const auto& aircraft_no : aircraft_order) {
        auto& flights = flights_by_aircraft.at(aircraft_no);
        std::sort(flights.begin(), flights.end(), [](const Flight& a, const Flight& b) {
            return a.std < b.std;
        });
        sorted_flights.insert(sorted_flights.end(), flights.begin(), flights.end());
    }

    // 3. Get all crews and sort them by ID
    const auto& all_crews_map = data.getCrews();
    std::vector<std::string> crew_ids;
    for (const auto& crew_id : solution.crew_assignment_order) {
        crew_ids.push_back(crew_id);
    }
    std::sort(crew_ids.begin(), crew_ids.end());

    // 4. Create a lookup for flight assignments
    std::map<std::string, std::unordered_set<std::string>> flight_to_crews;
    for (const auto& [flight_id, assignments] : solution.flight_assignments) {
        for (const auto& assignment : assignments) {
            if(assignment.second) {
            flight_to_crews[flight_id].insert(assignment.first);
        }
        }
    }

    // 5. Write the report in CSV format

    // Header row 1: Aircraft Numbers
    out << ",";
    for (size_t i = 0; i < sorted_flights.size(); ++i) {
        out << sorted_flights[i].aircraftNo;
        if (i < sorted_flights.size() - 1) {
            out << ",";
        }
    }
    out << "\n";

    // Header row 2: Flight IDs
    out << "Crew ID,";
    for (size_t i = 0; i < sorted_flights.size(); ++i) {
        out << sorted_flights[i].id;
        if (i < sorted_flights.size() - 1) {
            out << ",";
        }
    }
    out << "\n";

    // Data rows: Crew assignments
    for (const auto& crew_id : crew_ids) {
        bool has_assignment = false;
        for (const auto& flight : sorted_flights) {
            if (flight_to_crews.count(flight.id) && flight_to_crews.at(flight.id).count(crew_id)) {
                has_assignment = true;
                break;
            }
        }
        if (!has_assignment) continue;

        out << crew_id << ",";
        for (size_t i = 0; i < sorted_flights.size(); ++i) {
            const auto& flight = sorted_flights[i];
            bool assigned = flight_to_crews.count(flight.id) && flight_to_crews.at(flight.id).count(crew_id);
            out << (assigned ? "1" : "0");
            if (i < sorted_flights.size() - 1) {
                out << ",";
            }
        }
        out << "\n";
    }

    std::cout << "Assignment report CSV generated at " << output_path << std::endl;
} 

void ReportGenerator::generate_crew_dutyperiod_report(const SolutionState& solution, const DataLoader& data, const std::string& output_path) {
    std::ofstream out(output_path);
    if (!out) {
        std::cerr << "Error: Could not open file " << output_path << " for writing." << std::endl;
        return;
    }
    out << "Crew ID,Base,Count of Non-Base Layover,Count of Invalid Layover\n";

    const auto& layover_spots = data.getLayoverStations();
    for(const auto& crew_id : solution.crew_assignment_order){
        int count_nonbase_layover = 0;
        int count_invalid_layover = 0;
        const auto& duty_periods = solution.crew_dutyperiods.at(crew_id);
        for(const auto& duty_period : duty_periods){
            if(duty_period.tasks.empty()){
                continue;
            }
            if(std::find(layover_spots.begin(), layover_spots.end(), get_arrival_airport(duty_period.tasks.back())) == layover_spots.end()){
                count_invalid_layover++;
            }
            if(get_arrival_airport(duty_period.tasks.back()) != data.getCrews().at(crew_id).base){
                count_nonbase_layover++;
            }
        }
        out << crew_id << "," << data.getCrews().at(crew_id).base << "," << count_nonbase_layover << "," << count_invalid_layover << "\n";
    }
    std::cout << "Crew duty period report generated at " << output_path << std::endl;
}
