// SchedulingData.cpp
#include "SchedulingData.hpp"
#include "ReportGenerator.hpp"
#include "csv.h" // Include CSV parser library
#include <iomanip> // For date parsing
#include <sstream> // For date parsing
#include <chrono>
#include <sstream>
#include <stdexcept> // For throwing exceptions
#include <iomanip>
#include <sstream>
#include <random> // For randomly deleting buses
#include <algorithm> // For randomly deleting buses

// --- Time conversion helper function ---
// Convert "YYYY-MM-DD HH:MM:SS" format string to time_point
time_point string_to_time_point(const std::string& time_str) {
    std::tm tm = {};
    std::istringstream ss(time_str);
    
    // Attempt to parse string
    ss >> std::get_time(&tm, "%Y/%m/%d %H:%M");
    
    if (ss.fail()) {
        // ss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
        // if (ss.fail()) {
            throw std::runtime_error("Failed to parse datetime string");
        // }
    }
    
    // Convert to time_t (UTC time)
    std::time_t tt = std::mktime(&tm);
    
    if (tt == -1) {
        throw std::runtime_error("Invalid datetime value");
    }
    
    // Convert to UTC time point (need to subtract time zone offset)
    auto tp = std::chrono::system_clock::from_time_t(tt);
    
    // Get time difference between local time and UTC
    std::time_t now = std::time(nullptr);
    std::tm local_tm = *std::localtime(&now);
    std::tm utc_tm = *std::gmtime(&now);
    local_tm.tm_isdst = utc_tm.tm_isdst; // Unify DST setting
    std::time_t local_tt = std::mktime(&local_tm);
    std::time_t utc_tt = std::mktime(&utc_tm);
    auto timezone_diff = std::chrono::seconds(utc_tt - local_tt);
    
    return tp - timezone_diff;
}


std::string Task::to_string() const {
    std::ostringstream oss;
    oss << "[" << task_type << "]  " << id << "  "
        << start_airport << " -> " << end_airport << "  "
        << format_time(start_time) << " ~ " << format_time(end_time);
    if (task_type == "flight") {
        oss << "  Flight:" << fly_time.count() << "min";
        if (!aircraft_no.empty()) oss << "  AircraftNo:" << aircraft_no;
    }
    return oss.str();
}


std::string FDP::to_string() const {
    std::ostringstream oss;
    oss << "FDP: " << get_start_airport() << " -> " << get_end_airport() << "  "
        << format_time(get_start_time()) << " ~ " << format_time(get_end_time()) << "\n"
        << "  Total Flight Time: " << std::fixed << std::setprecision(1)
        << std::chrono::duration<double, std::ratio<3600>>(get_flight_hours()).count() << " hours"
        << "  Deadhead Count: " << get_deadhead_count()
        << "  Calendar Days: " << get_calendar_days()
        << "  Score: " << get_score() << "\n"
        << "  Task List:\n";
    for (const auto& task : tasks) {
        oss << "    " << task.to_string() << "\n";
    }
    return oss.str();
}


// --- Implementation of SchedulingData class ---

SchedulingData::SchedulingData(const std::filesystem::path& data_path, const std::filesystem::path& heuristic_path, size_t max_tasks_threshold) 
    : max_tasks_threshold_(max_tasks_threshold) {
    std::cout << "Starting to load data..." << std::endl;
    // Read tasks to exclude
    _load_excluded_tasks(heuristic_path);

    _load_crews(data_path / "crew.csv");
    _load_flights(data_path / "flight.csv");
    _load_buses(data_path / "busInfo.csv");
    _load_layover_stations(data_path / "layoverStation.csv");
    
    _link_ground_duties(data_path / "groundDuty.csv");
    _link_crew_qualifications(data_path / "crewLegMatch.csv");


    std::cout << "Data loading complete. Loaded " << crews_.size() << " crews, "
              << flights_.size() << " flights, " << buses_.size() << " buses." << std::endl;
}

void SchedulingData::_load_crews(const std::filesystem::path& file_path) {
    io::CSVReader<3, io::trim_chars<' '>> in(file_path.string());
    in.read_header(io::ignore_extra_column, "crewId", "base", "stayStation");
    
    std::string crewId, base, stayStation;
    while(in.read_row(crewId, base, stayStation)){
        // Use emplace to construct object directly in map, avoiding extra copy
        if(excluded_crew_ids_.find(crewId) != excluded_crew_ids_.end()) continue;
        crews_.emplace(crewId, Crew{crewId, base, stayStation});
    }
}

void SchedulingData::_load_flights(const std::filesystem::path& file_path) {
    io::CSVReader<8> in(file_path.string());
    in.read_header(io::ignore_extra_column, "id", "depaAirport", "arriAirport", "std", "sta", "fleet", "aircraftNo", "flyTime");

    std::string id, depa, arri, std_str, sta_str, fleet, aircraftNo;
    int flyTime;
    while(in.read_row(id, depa, arri, std_str, sta_str, fleet, aircraftNo, flyTime)) {
        if (excluded_task_ids_.find(id) != excluded_task_ids_.end()) continue;
        flights_.emplace(id, Flight{
            id, depa, arri, 
            string_to_time_point(std_str), 
            string_to_time_point(sta_str),
            fleet, aircraftNo, flyTime
        });
    }
}

void SchedulingData::_load_buses(const std::filesystem::path& file_path) {
    std::unordered_set<std::string> flight_airports;
    for (const auto& flight : flights_) {
        flight_airports.insert(flight.second.depa_airport);
        flight_airports.insert(flight.second.arri_airport);
    }


    io::CSVReader<5> in(file_path.string());
    in.read_header(io::ignore_extra_column, "id", "depaAirport", "arriAirport", "td", "ta");

    std::string id, depa, arri, td_str, ta_str;
    int flyTime;
    while(in.read_row(id, depa, arri, td_str, ta_str)) {
        if (flight_airports.find(depa) != flight_airports.end() && 
        flight_airports.find(arri) != flight_airports.end()) {
            buses_.emplace(id, Bus{
                id, depa, arri, 
                string_to_time_point(td_str), 
                string_to_time_point(ta_str)
            });
        }
    }
    
    // If total task count exceeds threshold, randomly delete excess buses
    // if (buses_.size() + flights_.size() > max_tasks_threshold_) {
    //     std::cout << "Bus count (" << buses_.size() << ") + Flight count (" << flights_.size() 
    //              << ") exceeds threshold " << max_tasks_threshold_ << ", randomly deleting excess buses..." << std::endl;
        
    //     // Put all bus IDs into vector
    //     std::vector<std::string> bus_ids;
    //     bus_ids.reserve(buses_.size());
    //     for (const auto& bus : buses_) {
    //         bus_ids.push_back(bus.first);
    //     }
        
    //     // Shuffle randomly
    //     std::random_device rd;
    //     std::mt19937 g(rd());
    //     std::shuffle(bus_ids.begin(), bus_ids.end(), g);
        
    //     // Delete excess buses
    //     size_t to_remove = buses_.size() + flights_.size() - max_tasks_threshold_;
    //     for (size_t i = 0; i < to_remove; ++i) {
    //         buses_.erase(bus_ids[i]);
    //     }
        
    //     std::cout << "Randomly deleted " << to_remove << " buses, remaining " << buses_.size() << " buses." << std::endl;
    // }
}

void SchedulingData::_load_layover_stations(const std::filesystem::path& file_path) {
    io::CSVReader<1> in(file_path.string());
    in.read_header(io::ignore_extra_column, "airport");
    std::string airport;
    while(in.read_row(airport)){
        layover_stations_.insert(airport);
    }
}

void SchedulingData::_link_ground_duties(const std::filesystem::path& file_path) {
    io::CSVReader<6> in(file_path.string());
    in.read_header(io::ignore_extra_column, "id", "crewId", "airport", "startTime", "endTime", "isDuty");

    std::string id, crewId, airport, start_str, end_str;
    int isDuty_int;

    // Create a temporary set to record crews with ground duties
    std::unordered_set<std::string> crews_with_duties;

    while(in.read_row(id, crewId, airport, start_str, end_str, isDuty_int)) {
        auto it = crews_.find(crewId);
        if (it != crews_.end()) {
            it->second.ground_duties.emplace_back(GroundDuty{
                id, crewId, airport,
                string_to_time_point(start_str),
                string_to_time_point(end_str),
                static_cast<bool>(isDuty_int)
            });
            crews_with_duties.insert(crewId);
        }
    }
}

void SchedulingData::_link_crew_qualifications(const std::filesystem::path& file_path) {
    io::CSVReader<2> in(file_path.string());
    in.read_header(io::ignore_extra_column, "crewId", "legId");
    
    std::string crewId, legId;
    while(in.read_row(crewId, legId)) {
        if(file_path.string().find("0606") != std::string::npos){
            crewId = crewId.substr(1, crewId.size() - 2); // Remove quotes
            legId = legId.substr(1, legId.size() - 2); 
        }
        auto it = crews_.find(crewId);
        if (it != crews_.end()) {
            it->second.qualified_flights.insert(legId);
        }
    } 

    // // Delete crews with no qualifications
    // size_t removed_count = 0;
    // for (auto it = crews_.begin(); it != crews_.end();) {
    //     if (!it->second.qualified_flights.empty()) {
    //         it = crews_.erase(it);
    //         removed_count++;
    //     } else {
    //         ++it;
    //     }
    // }

    // for (auto it = crews_.begin(); it != crews_.end();) {
    //     if (it->second.qualified_flights.empty()) {
    //         // Add all flights
    //         for (const auto& flight : flights_) {
    //             it->second.qualified_flights.insert(flight.second.id);
    //         }
    //     }
    //     it++;
    // }
    
    // std::cout << "Deleted " << removed_count << " unqualified crews, remaining " << crews_.size() << " crews." << std::endl;
}

void SchedulingData::_load_excluded_tasks(const std::filesystem::path& file_path) {
    io::CSVReader<3> in(file_path.string());
    in.read_header(io::ignore_extra_column, "crewId", "taskId", "isDDH");
    
    std::string crew_id, task_id;
    int is_ddh;
    while(in.read_row(crew_id, task_id, is_ddh)) {
        excluded_task_ids_.insert(task_id);
        excluded_crew_ids_.insert(crew_id);
    }
}

// --- Implementation of Public Query Interfaces ---

const Flight* SchedulingData::get_flight(const std::string& flight_id) const {
    auto it = flights_.find(flight_id);
    return (it != flights_.end()) ? &(it->second) : nullptr;
}

const Bus* SchedulingData::get_bus(const std::string& bus_id) const {
    auto it = buses_.find(bus_id);
    return (it != buses_.end()) ? &(it->second) : nullptr;
}

const Crew* SchedulingData::get_crew(const std::string& crew_id) const {
    auto it = crews_.find(crew_id);
    return (it != crews_.end()) ? &(it->second) : nullptr;
}

const std::unordered_map<std::string, Crew>& SchedulingData::get_all_crews() const {
    return crews_;
}