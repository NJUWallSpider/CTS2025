// SchedulingData.hpp
#pragma once // Prevent duplicate inclusion

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <chrono>
#include <filesystem>
#include <optional>

// Use aliases to simplify code
using time_point = std::chrono::system_clock::time_point;
// C++20 date representation, better than time_point for year-month-day
using Date = std::chrono::year_month_day; 

// --- C++ Equivalent Data Structures ---

struct Flight {
    std::string id;
    std::string depa_airport;
    std::string arri_airport;
    time_point std;
    time_point sta;
    std::string fleet;
    std::string aircraft_no;
    int fly_time; // Minutes
};

struct Bus {
    std::string id;
    std::string depa_airport;
    std::string arri_airport;
    time_point td;
    time_point ta;
};

struct GroundDuty {
    std::string id;
    std::string crew_id;
    std::string airport;
    time_point start_time;
    time_point end_time;
    bool is_duty;
};
struct Crew {
    std::string id;
    std::string base;
    std::string initial_station;
    std::unordered_set<std::string> qualified_flights;
    std::vector<GroundDuty> ground_duties;
};


// Unified representation of Task (Equivalent to Task in Python)
// In C++, we can get properties directly from Flight/Bus via functions or methods, without necessarily needing a new struct
// But to maintain 1:1 structure, we define it first
class Task {
public:
    std::string id;
    std::string task_type; // "flight", "bus"
    std::string start_airport;
    std::string end_airport;
    time_point start_time;
    time_point end_time;
    std::chrono::minutes fly_time{0};
    std::string aircraft_no;

    // In C++, for hashability, we need custom hash function and equality operator
    // Or, due to complex members, typically not used directly as unordered_map key
    bool operator==(const Task& other) const {
        return id == other.id;
    }

    std::string to_string() const;
};

// C++ representation of FDP
class FDP {
public:
    int id;

    std::vector<Task> tasks;

    time_point get_start_time() const {
            return tasks.empty() ? time_point{} : tasks.front().start_time;
        }
    time_point get_end_time() const {
        return tasks.empty() ? time_point{} : tasks.back().end_time;
    }
    std::string get_start_airport() const {
        return tasks.empty() ? "" : tasks.front().start_airport;
    }
    std::string get_end_airport() const {
        return tasks.empty() ? "" : tasks.back().end_airport;
    }

    std::string to_string() const;
    
    // Use std::accumulate to calculate sum, very efficient
    std::chrono::minutes get_flight_hours() const {
        std::chrono::minutes total_duration(0);
        for (const auto& task : tasks) {
            if (task.task_type == "flight") {
                total_duration += task.fly_time;
            }
        }
        return total_duration;
    }

    int get_deadhead_count() const {
        int count = 0;
        for (const auto& task : tasks) {
            if (task.task_type.find("positioning") != std::string::npos || task.task_type == "bus") {
                count++;
            }
        }
        return count;
    }

    std::unordered_set<std::string> get_included_flight_ids() const {
        std::unordered_set<std::string> ids;
        for (const auto& task : tasks) {
            if (task.task_type == "flight") {
                ids.insert(task.id);
            }
        }
        return ids;
    }

    int get_calendar_days() const {
        if (tasks.empty()) return 0;
        Date start_date = std::chrono::floor<std::chrono::days>(get_start_time());
        Date end_date = std::chrono::floor<std::chrono::days>(get_end_time());
        // Subtraction between year_month_day directly gives days
        return (std::chrono::sys_days(end_date) - std::chrono::sys_days(start_date)).count() + 1;
    }

    double get_score() const {
        constexpr double W_fly = 25.0, W_deadhead = 0.5, W_days = 2.0;
        double flight_hours = std::chrono::duration<double, std::ratio<3600>>(get_flight_hours()).count();
        return flight_hours * W_fly - get_deadhead_count() * W_deadhead - get_calendar_days() * W_days;
    }
    
    // For hashability, also need customization
    bool operator==(const FDP& other) const;
};

struct Tour {
    std::string id;
    std::string base;
    std::vector<FDP> fdps;

    time_point get_start_time() const {
        return fdps.empty() ? time_point{} : fdps.front().get_start_time();
    }

    time_point get_end_time() const {
        return fdps.empty() ? time_point{} : fdps.back().get_end_time();
    }

    double get_flight_hours() const {
        double total_hours = 0.0;
        for (const auto& fdp : fdps) {
            total_hours += std::chrono::duration<double, std::ratio<3600>>(fdp.get_flight_hours()).count();
        }
        return total_hours;
    }

    int get_deadhead_count() const {
        int count = 0;
        for (const auto& fdp : fdps) {
            count += fdp.get_deadhead_count();
        }
        return count;
    }

    std::set<std::string> get_covered_flights() const {
        std::set<std::string> covered;
        for (const auto& fdp : fdps) {
            auto ids = fdp.get_included_flight_ids();
            covered.insert(ids.begin(), ids.end());
        }
        return covered;
    }

    int get_calendar_days() const {
        if (fdps.empty()) return 0;
        Date start_date = std::chrono::floor<std::chrono::days>(get_start_time());
        Date end_date = std::chrono::floor<std::chrono::days>(get_end_time());
        return (std::chrono::sys_days(end_date) - std::chrono::sys_days(start_date)).count() + 1;
    }
    
    int get_away_from_base_overnights() const {
        int overnights = 0;
        if (fdps.size() > 1) {
            for (size_t i = 0; i < fdps.size() - 1; ++i) {
                if (fdps[i].get_end_airport() != base) {
                    Date start_of_next_fdp = std::chrono::floor<std::chrono::days>(fdps[i+1].get_start_time());
                    Date end_of_prev_fdp = std::chrono::floor<std::chrono::days>(fdps[i].get_end_time());
                    overnights += (std::chrono::sys_days(start_of_next_fdp) - std::chrono::sys_days(end_of_prev_fdp)).count();
                }
            }
        }
        return overnights;
    }
};

// --- Define hash function for std::pair<std::string, Date> (Correct and robust version) ---
struct PairHash {
    std::size_t operator()(const std::pair<std::string, Date>& p) const {
        // 1. Calculate hash of string
        const auto h1 = std::hash<std::string>{}(p.first);

        // 2. Convert year_month_day to "days since epoch"
        // This generates a unique integer value for each unique date
        const auto days_since_epoch = std::chrono::sys_days(p.second).time_since_epoch().count();
        
        // 3. Hash this unique integer representing the date
        const auto h2 = std::hash<long long>{}(days_since_epoch);

        // 4. Combine two hash values. This is a more robust way than simple XOR.
        // (Inspired by boost::hash_combine)
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

time_point string_to_time_point(const std::string& time_str);

// --- Main class for data loading and storage ---
class SchedulingData {
public:
    // Constructor, receives data path and loads all data
    explicit SchedulingData(const std::filesystem::path& data_path, const std::filesystem::path& heuristic_path, size_t max_tasks_threshold = 3500);

    // --- Public query interface ---
    const Flight* get_flight(const std::string& flight_id) const;
    const Bus* get_bus(const std::string& bus_id) const;
    const Crew* get_crew(const std::string& crew_id) const;

    // Provide read-only access to all flight and bus data
    const std::unordered_map<std::string, Flight>& get_all_flights() const { return flights_; }
    const std::unordered_map<std::string, Bus>& get_all_buses() const { return buses_; }
    
    // For convenience, provide a reference to all crews
    const std::unordered_map<std::string, Crew>& get_all_crews() const;

    // All valid FDPs
    std::unordered_map<
        std::pair<std::string, Date>, 
        std::vector<FDP>, 
        PairHash
    > all_valid_fdps_;

    // Get all layover stations
    const std::unordered_set<std::string>& get_layover_stations() const { return layover_stations_; }

private:
    // Data loading methods
    void _load_crews(const std::filesystem::path& file_path);
    void _load_flights(const std::filesystem::path& file_path);
    void _load_buses(const std::filesystem::path& file_path);
    void _load_layover_stations(const std::filesystem::path& file_path);
    void _link_ground_duties(const std::filesystem::path& file_path);
    void _link_crew_qualifications(const std::filesystem::path& file_path);
    void _load_excluded_tasks(const std::filesystem::path& file_path);

    std::unordered_map<std::string, Flight> flights_;
    std::unordered_map<std::string, Bus> buses_;
    std::unordered_map<std::string, Crew> crews_;
    std::unordered_set<std::string> excluded_task_ids_; // Stores task IDs to exclude
    std::unordered_set<std::string> excluded_crew_ids_; // Stores crew IDs to exclude
    std::unordered_set<std::string> layover_stations_;
    
    // Task count threshold, randomly delete buses if exceeded
    size_t max_tasks_threshold_;
};
