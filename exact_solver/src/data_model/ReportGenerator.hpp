// ReportGenerator.hpp
#pragma once

#include "SchedulingData.hpp" // Include core data structures
#include <string>
#include <set>
#include <map>
#include <vector>

using Solution = std::map<std::string, std::vector<FDP>>;

std::string format_time(const time_point& tp);

namespace ReportGenerator {
/**
 * @brief 
 * @param solution Final solution from solver.
 * @param filename Report filename to save.
 * @param uncovered_flights Set of uncovered flights.
 */
void generate_readable_report(const Solution& solution, const std::string& filename, 
                            const std::set<std::string>& uncovered_flights = {});

/**
 * @brief Creates a standard format CSV submission file based on the solver's solution.
 * @param solution Final solution from solver.
 * @param filename CSV filename to save.
 */
void generate_submission_file(const Solution& solution, const std::string& filename);

} // namespace ReportGenerator
