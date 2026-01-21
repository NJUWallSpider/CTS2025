// src/SchedulingData.cpp

#include "LoadData.hpp"
#include "csv.h" // Include csv.h library
#include <iostream>
#include <algorithm> // for std::sort
#include <iomanip>   // for std::get_time

// Convert "YYYY/M/D H:M" format string to time point
TimePoint DataLoader::string_to_time_point(const std::string& time_str) {
    std::tm tm = {};
    std::stringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y/%m/%d %H:%M");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm)) + std::chrono::hours(8);
}

// Constructor, schedule all loading operations
DataLoader::DataLoader(const std::filesystem::path& data_path, const std::string& data_version) {
    std::cout << "Starting data loading..." << std::endl;

    _load_crews(data_path / "crew.csv");
    _load_flights(data_path / "flight.csv");
    _load_buses(data_path / "busInfo.csv");
    _load_layover_stations(data_path / "layoverStation.csv");
    
    _load_and_link_ground_duties(data_path / "groundDuty.csv");
    _link_crew_qualifications(data_path / "crewLegMatch.csv");
    //_sort_qualifications();
    //_link_flights_to_aircrafts();


    std::cout << "Data loading complete. Loaded " << crews_.size() << " crew members, "
              << flights_.size() << " flights, " << buses_.size() << " buses." << std::endl;
}

void DataLoader::_load_crews(const std::filesystem::path& file_path) {
    io::CSVReader<3> in(file_path.string());
    in.read_header(io::ignore_extra_column, "crewId", "base", "stayStation");
    
    std::string crewId, base, stayStation;
    while(in.read_row(crewId, base, stayStation)){
        crews_.emplace(crewId, Crew{
            crewId, base, stayStation, {}, {}, {}, {}, {}, TimePoint::min(), TimePoint::min(), {stayStation}
        });
    }
}

// load the flight tasks
void DataLoader::_load_flights(const std::filesystem::path& flight_path) {
    // load the flight tasks
    io::CSVReader<8> flight_in(flight_path.string());
    flight_in.read_header(io::ignore_extra_column, "id", "depaAirport", "arriAirport", "std", "sta", "fleet", "aircraftNo", "flyTime");
    std::string id, depa, arri, std_str, sta_str, fleet, aircraftNo, flyTime_str;
    while(flight_in.read_row(id, depa, arri, std_str, sta_str, fleet, aircraftNo, flyTime_str)) {
        flights_.push_back(Flight{
            id, depa, arri, 
            string_to_time_point(std_str), 
            string_to_time_point(sta_str),
            fleet,
            aircraftNo,
            std::stoi(flyTime_str)
        });
    }
    // sort all the flights by the std time
    std::sort(flights_.begin(), flights_.end(), [](const Flight& a, const Flight& b) {
        return a.std < b.std;
    });

}

void DataLoader::_load_buses(const std::filesystem::path& bus_path) {
        // Load buses
    io::CSVReader<5> bus_in(bus_path.string());
    bus_in.read_header(io::ignore_extra_column, "id", "depaAirport", "arriAirport", "td", "ta");
    std::string id, depa, arri, td_str, ta_str;
    while(bus_in.read_row(id, depa, arri, td_str, ta_str)) {
        buses_.push_back(Bus{
            id, depa, arri, 
            string_to_time_point(td_str), 
            string_to_time_point(ta_str),
        });
    }
    // sort all the buses by the td time
    std::sort(buses_.begin(), buses_.end(), [](const Bus& a, const Bus& b) {
        return a.td < b.td;
    });
}

// Load layover airports
void DataLoader::_load_layover_stations(const std::filesystem::path& file_path) {
    io::CSVReader<1> in(file_path.string());
    in.read_header(io::ignore_extra_column, "airport");
    std::string airport;
    while(in.read_row(airport)) {
        layover_stations_.push_back(airport);
    }
}

// Link ground tasks with crew members
void DataLoader::_load_and_link_ground_duties(const std::filesystem::path& file_path) {
    io::CSVReader<6> in(file_path.string());
    in.read_header(io::ignore_extra_column, "id", "crewId", "airport", "startTime", "endTime", "isDuty");
    
    std::string id, crewId, airport, startTime_str, endTime_str, isDuty_str;
    while(in.read_row(id, crewId, airport, startTime_str, endTime_str, isDuty_str)) {
        auto it = crews_.find(crewId);
        if (it != crews_.end()) {
            // Create the new ground duty
            GroundDuty newDuty{
                crewId, id, airport,
                string_to_time_point(startTime_str),
                string_to_time_point(endTime_str),
                std::stoi(isDuty_str)
            };
            
            // Find the correct position to insert based on start time
            auto& duties = it->second.groundDuties;
            auto insertPos = std::lower_bound(duties.begin(), duties.end(),
                newDuty,
                [](const GroundDuty& a, const GroundDuty& b) {
                    return a.start_time < b.start_time;
                });
            
            duties.insert(insertPos, newDuty);
        }
    }
}


// Link crew qualifications with tasks
void DataLoader::_link_crew_qualifications(const std::filesystem::path& file_path) {
    io::CSVReader<2> in(file_path.string());
    in.read_header(io::ignore_extra_column, "crewId", "legId");
    
    
    std::string crewId, legId;
    while(in.read_row(crewId, legId)) {
        if(file_path.string() == "data/0606/crewLegMatch.csv") {
            crewId = crewId.substr(1, crewId.size() - 2); // Remove quotes
            legId = legId.substr(1, legId.size() - 2); 
        }

        auto it = crews_.find(crewId);
        if (it != crews_.end()) {
            it->second.qualifications.insert(legId);
            

        }
        flight_to_crews_[legId].push_back(crewId);

        
    }

}

// void DataLoader::_sort_qualifications(){
//     std::unordered_map<std::string, const Flight*> flight_map;
//     for (const auto& flight : flights_) {
//         flight_map[flight.id] = &flight;
//     }
//     for(auto& crew : crews_){
//         std::sort(crew.second.qualifications.begin(), crew.second.qualifications.end(), [this, &flight_map](const std::string& a, const std::string& b) {
//             auto a_it = flight_map.find(a);
//             auto b_it = flight_map.find(b);

//             if (a_it != flight_map.end() && b_it != flight_map.end()) {
//                 return a_it->second->std < b_it->second->std;
//             }
//             return false;
//         });
//     }
// }

// void DataLoader::_link_flights_to_aircrafts(){
//     for(auto& flight : flights_){
//         aircraft_to_flights_[flight.aircraftNo].push_back(flight);
//     }

//     // sort the flights in each aircraft by the std time
//     for(auto& aircraft : aircraft_to_flights_){
//         std::sort(aircraft.second.begin(), aircraft.second.end(), [](const Flight& a, const Flight& b) {
//             return a.std < b.std;
//         });
//     }
// }
