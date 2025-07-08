#pragma once

#include "../Constructor/SolutionState.hpp"
#include <string>
#include <chrono>
#include <iostream>

class DataLoader; // Forward declaration

class ReportGenerator {
public:
    // Validates that crew duty period schedules match flight assignments
    static bool validate_crew_flight_consistency(const SolutionState& solution, const std::string& output_path);
    static void generate_schedule_report(const SolutionState& solution, const DataLoader& data, const std::string& output_path, const std::chrono::steady_clock::time_point& start_time);
    static void generate_submission_csv(const SolutionState& solution, const std::string& output_path);
private:
    std::string data_version_;
}; 