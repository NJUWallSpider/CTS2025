#include "SolutionConstruct.hpp"
#include "Scheduler/CrewSchedule.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <map>
#include <limits>
#include <algorithm>
#include <numeric>
#include <random>

SolutionConstructor::SolutionConstructor(const DataLoader& data) : data_(data) {}

SolutionState SolutionConstructor::generate_schedule() {
    SolutionState solution;
    const auto& crews = data_.getCrews();
    
    // Create a vector of crews with priority scores
    std::vector<std::pair<Crew, double>> crew_scores;
    
    // Calculate maximum values for normalization
    double max_ground_duties = 0;
    double max_quals = 0;
    std::map<std::string, int> base_flight_counts;
    
    // First pass: gather statistics 6-3-2
    for(const auto& [_, crew] : crews) {
        max_ground_duties = std::max(max_ground_duties, static_cast<double>(crew.groundDuties.size()));
        max_quals = std::max(max_quals, static_cast<double>(crew.qualifications.size()));
    }
    
    for(const auto& flight : data_.getFlights()) {
        base_flight_counts[flight.depaAirport]++;
    }
    
    double max_base_flights = 0;
    for(const auto& [_, count] : base_flight_counts) {
        max_base_flights = std::max(max_base_flights, static_cast<double>(count));
    }
    
    // Calculate priority scores for each crew 6-3-2
    for(const auto& [_, crew] : crews) {
        double score = 0.0;
        
        // Factor 1: Ground duties (30% weight)
        if(max_ground_duties > 0) {
            score += (crew.groundDuties.size() / max_ground_duties) * 0.6;
        }
        
        // Factor 2: Qualification flexibility (40% weight)
        if(max_quals > 0) {
            score -= (crew.qualifications.size() / max_quals) * 0.3;
        }
        
        // Factor 3: Base station strategic value (30% weight)
        if(max_base_flights > 0 && base_flight_counts.count(crew.base)) {
            score += (base_flight_counts[crew.base] / max_base_flights) * 0.2;
        }
        
        crew_scores.emplace_back(crew, score);
    }
    
    // Sort crews by their composite scores in descending order
    std::sort(crew_scores.begin(), crew_scores.end(), 
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
    
    // Extract sorted crews
    std::vector<Crew> weight_quali_base_sorted_crews;
    for(const auto& [crew, _] : crew_scores) {
        weight_quali_base_sorted_crews.push_back(crew);
    }

    // // shuffle option
    // std::random_device rd;
    // std::mt19937 g(rd());
    // std::shuffle(sorted_crews.begin(), sorted_crews.end(), g);

    // for the second phase:
    std::vector<Crew> crews_with_no_ground_duties;
    for(const auto& crew : weight_quali_base_sorted_crews){
        // get the crews that has no ground duties
        if(crew.groundDuties.empty()){
            crews_with_no_ground_duties.emplace_back(crew);            
        }
    }
    
    for(const auto& crew : crews_with_no_ground_duties){
        CrewSchedule crew_schedule_builder(data_, crew, 
        solution.crew_dutyperiods[crew.id], 
        solution.flight_assignments,
        solution.crew_cycles[crew.id]);

        crew_schedule_builder.construct_schedule_DFS();

    }
    // the score of the solution is the sum of the flights that is piloted
    solution.score = 0.0;
    for(const auto& flight : solution.flight_assignments){
        // get the flight that has one "true" in the vector
        for(const auto& task : flight.second){
            if(task.second){
                solution.score += 1;
                break;
            }
        }
    }
    return solution;
}