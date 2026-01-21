// ReportGenerator.cpp
#include "ReportGenerator.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <iomanip> // For std::setw, std::left etc.
#include <sstream> // For std::stringstream

// Helper function: Format time_point to "MM-DD HH:MM"
std::string format_time(const time_point& tp) {
    // auto zoned_time = std::chrono::zoned_time(std::chrono::current_zone(), tp);
    return std::format("{:%m-%d %H:%M}", tp);
}

// Helper function: Format task type
std::string format_task_type(const std::string& type) {
    std::string result = type;
    std::replace(result.begin(), result.end(), '_', ' ');
    // Capitalize first letter
    if (!result.empty()) {
        result[0] = toupper(result[0]);
        for (size_t i = 1; i < result.length() -1; ++i) {
            if (result[i-1] == ' ') {
                result[i] = toupper(result[i]);
            }
        }
    }
    return result;
}

namespace ReportGenerator {

void generate_readable_report(const Solution& solution, const std::string& filename, const std::set<std::string>& uncovered_flights) {
    std::ofstream report_file(filename);
    if (!report_file.is_open()) {
        std::cerr << "Error: Unable to write report file '" << filename << "'." << std::endl;
        return;
    }

    std::stringstream ss;

    // Report Header
    auto now = std::chrono::system_clock::now();
    ss << "Schedule Generation Time: " << std::format("{:%Y-%m-%d %H:%M:%S}", now) << "\n";
    long long assigned_crews = 0;
    for(const auto& pair : solution) {
        if (!pair.second.empty()) {
            assigned_crews++;
        }
    }
    ss << "Total Assigned Crews: " << assigned_crews << "\n\n";

    // Iterate through all crews
    for (const auto& pair : solution) {
        const std::string& crew_id = pair.first;
        const auto& fdps = pair.second;
        if (fdps.empty()) continue;

        ss << std::string(60, '=') << "\n";
        ss << "Crew ID: " << crew_id << "\n";
        ss << std::string(60, '=') << "\n";

        // Sort FDPs by time
        auto sorted_FDPs = fdps;
        std::sort(sorted_FDPs.begin(), sorted_FDPs.end(), 
            [](const FDP& a, const FDP& b){ return a.get_start_time() < b.get_start_time(); });

        for (size_t i = 0; i < sorted_FDPs.size(); ++i) {
            const auto& fdp = sorted_FDPs[i];
            ss << "\n--- FDP #" << i + 1 << " ---\n";
            ss << "  Time Range: " << format_time(fdp.get_start_time()) << " -> " << format_time(fdp.get_end_time()) << "\n";
            ss << "  Route: " << fdp.get_start_airport() << " -> " << fdp.get_end_airport() << "\n";

            for (const auto& task : fdp.tasks) {
                std::stringstream task_line;
                task_line << "    * [" << std::left << std::setw(15) << format_task_type(task.task_type) << "] "
                          << std::left << std::setw(12) << task.id << " | "
                          << task.start_airport << " " << format_time(task.start_time) << " -> "
                          << task.end_airport << " " << format_time(task.end_time);
                ss << task_line.str() << "\n";
            }

            // Calculate rest period
            if (i < sorted_FDPs.size() - 1) {
                auto rest_duration = sorted_FDPs[i+1].get_start_time() - fdp.get_end_time();
                auto hours = std::chrono::duration_cast<std::chrono::hours>(rest_duration);
                auto minutes = std::chrono::duration_cast<std::chrono::minutes>(rest_duration % std::chrono::hours(1));
                ss << "\n  >>> Rest Period: " << hours.count() << " hours " << minutes.count() << " minutes <<<
";
            }
        }
        ss << "\n";
    }
    
    // Add uncovered flights list
    if (!uncovered_flights.empty()) {
        ss << "\n" << std::string(60, '=') << "\n";
        ss << "Uncovered Flights List: " << uncovered_flights.size() << " count\n";
        ss << std::string(60, '=') << "\n\n";
        
        std::vector<std::string> sorted_flights(uncovered_flights.begin(), uncovered_flights.end());
        std::sort(sorted_flights.begin(), sorted_flights.end());
        
        size_t count = 0;
        for (const auto& flight_id : sorted_flights) {
            ss << std::left << std::setw(15) << flight_id;
            if (++count % 5 == 0) ss << "\n";
        }
        if (count % 5 != 0) ss << "\n"; // Ensure newline at the end
    }

    report_file << ss.str();
    std::cout << "Readable report successfully generated: '" << filename << "'" << std::endl;
}

void generate_submission_file(const Solution& solution, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to write submission file '" << filename << "'." << std::endl;
        return;
    }

    file << "crewId,taskId,isDDH\n"; // Write header

    for (const auto& pair : solution) {
        const std::string& crew_id = pair.first;
        if (pair.second.empty()) continue;

        // Collect and sort all tasks
        std::vector<Task> all_tasks_for_crew;
        for (const auto& fdp : pair.second) {
            all_tasks_for_crew.insert(all_tasks_for_crew.end(), fdp.tasks.begin(), fdp.tasks.end());
        }
        std::sort(all_tasks_for_crew.begin(), all_tasks_for_crew.end(), 
            [](const Task& a, const Task& b){ return a.start_time < b.start_time; });
        
        // Write each row
        for (const auto& task : all_tasks_for_crew) {
            int is_ddh = (task.task_type.find("deadhead") != std::string::npos || task.task_type == "bus") ? 1 : 0;
            file << crew_id << "," << task.id << "," << is_ddh << "\n";
        }
    }
    std::cout << "Standard submission file successfully generated: '" << filename << "'" << std::endl;
}

} // namespace ReportGenerator
