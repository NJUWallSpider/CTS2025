// src/Core/SchedulingData.h

#pragma once

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <filesystem>
#include <unordered_set>

using TimePoint = std::chrono::system_clock::time_point;

// // Represents a schedulable task segment (Flight or Bus)
// struct Leg {
//     int rnum = -1;            // Globally unique index after sorting (rank number)
//     bool is_flight = false;           // true for flight, false for bus 
//     bool is_ground_duty = false;      // true for ground duty
//     bool is_grd_rest = false;         // true for ground duty rest
//     std::string id;           // Original ID (e.g., "Flt_100002" or "ddh_100001")
//     std::string dep_airport;
//     std::string arr_airport;
//     TimePoint dep_time;
//     TimePoint arr_time;
//     std::string aircraftNo; // Aircraft tail number
//     int flyTime_mins;       // Flight time (minutes)
//     std::string crewId = "";     // Crew member ID
// };

struct Flight{
    std::string id;
    std::string depaAirport;
    std::string arriAirport;
    TimePoint std;
    TimePoint sta;
    std::string fleet;
    std::string aircraftNo;
    int flyTime_mins;
    ///
    int is_assigned;
    std::string assigned_crew_id;
    std::vector<std::string> flight_to_crews;
};

struct Bus{
    std::string id;
    std::string depaAirport;
    std::string arriAirport;
    TimePoint td;
    TimePoint ta;
};

// Represents a pre-assigned ground duty
struct GroundDuty {
    std::string crew_id;
    std::string id;
    std::string airport;
    TimePoint start_time;
    TimePoint end_time;
    int is_duty;
};

// Represents a crew member and their current state
struct Crew {
    std::string id;                   // Crew member ID
    std::string base;                 // Crew member base airport
    std::string initialStayStation;   // Initial stay station

    // Pre-assigned ground duties
    std::vector<GroundDuty> groundDuties;

    // Crew member qualifications list
    std::unordered_set<std::string> qualifications;

    // // Flight (fly) and positioning (deadhead) flight rnum list 
    // std::vector<int> fd_list; 

    // // Bus rnum list for passenger (deadhead) only 
    // std::vector<int> h_list;

    // flight(non-deadhead) list
    std::vector<Flight> flight_list;

    // flight(deadhead) list
    std::vector<Flight> ddh_flight_list;

    // bus(deadhead) list
    std::vector<Bus> ddh_bus_list;

    // Member current task departure time 
    TimePoint current_dp;

    // Member current task arrival time 
    TimePoint current_ar;

    // List of airports visited by member for flight/passenger tasks, first airport is their base 
    std::vector<std::string> visited_airports;

};

/**
 * @class SchedulingData
 * @brief Loads and manages all input data required for the scheduling problem.
 * 
 * This class is responsible for reading data from various CSV files (flight.csv, 
 * crew.csv, etc.) into memory. It provides a structured and easily accessible 
 * way to get information about flights, crew members, connectivity rules, 
 * and other operational constraints. The rest of the application relies on this 
 * class to access the necessary data for scheduling and evaluation.
 */
class DataLoader {
public:
    explicit DataLoader(const std::filesystem::path& data_path, const std::string& data_version);

    // --- Public Access Interface ---
    const std::map<std::string, Crew>& getCrews() const { return crews_; }
    // const std::vector<Leg>& getSchedulableLegs() const { return schedulable_legs_; }
    // const Leg& getLegByRnum(int rnum) const { return schedulable_legs_[rnum]; }
    const std::vector<Flight>& getFlights() const { return flights_; }
    const std::vector<Bus>& getBuses() const { return buses_; }
    const std::map<std::pair<std::string, std::string>, std::vector<int>>& getRouteIndex() const { return route_to_legs_index_; }
    const std::vector<std::string>& getLayoverStations() const { return layover_stations_; }
    const std::map<std::string, std::vector<std::string>>& getFlightToCrews() const { return flight_to_crews_; }
    static TimePoint string_to_time_point(const std::string& time_str);
    std::string getDataVersion() const { return data_version_; }
    const std::map<std::string, std::vector<Flight>>& getAircraftToFlights() const { return aircraft_to_flights_; }
private:
    // --- Member Variables ---
    std::map<std::string, Crew> crews_;
    // std::vector<Leg> schedulable_legs_; // Sorted and assigned rnum
    std::map<std::string, std::vector<std::string>> flight_to_crews_;
    std::vector<Flight> flights_;
    std::vector<Bus> buses_;
    std::vector<std::string> layover_stations_;
    std::string data_version_;
    std::map<std::string, std::vector<Flight>> aircraft_to_flights_;
    // Maps a route (departure airport, arrival airport) to a list of leg indices that connect those airports
    std::map<std::pair<std::string, std::string>, std::vector<int>> route_to_legs_index_;


    // --- Private Load Functions ---
    void _load_crews(const std::filesystem::path& file_path);
    void _load_flights(const std::filesystem::path& flight_path);
    void _load_buses(const std::filesystem::path& bus_path);
    void _load_layover_stations(const std::filesystem::path& file_path);
    void _load_and_link_ground_duties(const std::filesystem::path& file_path);
    void _link_crew_qualifications(const std::filesystem::path& file_path);
    void _sort_qualifications();
    void _link_flights_to_aircrafts();
};


// // Used to package all input data
// struct ProcessedData {
//     std::vector<Leg> legs;
//     std::vector<Crew> crews;
//     std::vector<std::string> layoverStations;
// };
