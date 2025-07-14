// src/SchedulingData.cpp

#include "LoadData.hpp"
#include "csv.h" // 引入 csv.h 库
#include <iostream>
#include <algorithm> // for std::sort
#include <iomanip>   // for std::get_time

// 将 "YYYY/M/D H:M" 格式的字符串转换为时间点
TimePoint DataLoader::string_to_time_point(const std::string& time_str) {
    std::tm tm = {};
    std::stringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y/%m/%d %H:%M");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm)) + std::chrono::hours(8);
}

// 构造函数，调度所有加载操作
DataLoader::DataLoader(const std::filesystem::path& data_path, const std::string& data_version) {
    std::cout << "开始加载数据..." << std::endl;

    _load_crews(data_path / "crew.csv");
    _load_flights(data_path / "flight.csv");
    _load_buses(data_path / "busInfo.csv");
    _load_layover_stations(data_path / "layoverStation.csv");
    
    _load_and_link_ground_duties(data_path / "groundDuty.csv");
    _link_crew_qualifications(data_path / "crewLegMatch.csv");
    //_sort_qualifications();
    _link_flights_to_aircrafts();
    _build_aircraft_routes();
    _build_crew_turnarounds();


    std::cout << "数据加载完毕。共加载 " << crews_.size() << " 名机组成员, "
              << flights_.size() << " 个航班, " << buses_.size() << " 个巴士。" << std::endl;
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
        // 加载巴士
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

// 加载过夜机场
void DataLoader::_load_layover_stations(const std::filesystem::path& file_path) {
    io::CSVReader<1> in(file_path.string());
    in.read_header(io::ignore_extra_column, "airport");
    std::string airport;
    while(in.read_row(airport)) {
        layover_stations_.push_back(airport);
    }
}

// 将地面任务与机组成员关联
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


// 将机组成员的资格与任务关联
void DataLoader::_link_crew_qualifications(const std::filesystem::path& file_path) {
    io::CSVReader<2> in(file_path.string());
    in.read_header(io::ignore_extra_column, "crewId", "legId");
    
    
    std::string crewId, legId;
    while(in.read_row(crewId, legId)) {
        if(file_path.string() == "data/0606/crewLegMatch.csv") {
            crewId = crewId.substr(1, crewId.size() - 2); // 去掉引号
            legId = legId.substr(1, legId.size() - 2); 
        }

        auto it = crews_.find(crewId);
        if (it != crews_.end()) {
            it->second.qualifications.insert(legId);
            

        }
        flight_to_crews_[legId].push_back(crewId);

        
    }

}

void DataLoader::_build_aircraft_routes() {
    for (auto& flight_ref : flights_) {
        const Flight* flight = &flight_ref;
        // This is a bit inefficient, but flight list is not that large
        auto it = std::find_if(flights_.begin(), flights_.end(), [&](const Flight& f) {
            return f.aircraftNo == flight->aircraftNo && f.std > flight->sta;
        });

        const Flight* next_flight = (it != flights_.end()) ? &(*it) : nullptr;
        
        if (aircraft_routes_.find(flight->aircraftNo) == aircraft_routes_.end()) {
            aircraft_routes_[flight->aircraftNo] = AircraftRoute{flight->aircraftNo, {}};
        }

        long long turnaround_minutes = 0;
        if (next_flight) {
            turnaround_minutes = std::chrono::duration_cast<std::chrono::minutes>(next_flight->std - flight->sta).count();
        }

        aircraft_routes_.at(flight->aircraftNo).connections.push_back({
            flight,
            next_flight,
            std::chrono::minutes(turnaround_minutes)
        });
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

void DataLoader::_link_flights_to_aircrafts(){
    for(auto& flight : flights_){
        aircraft_to_flights_[flight.aircraftNo].push_back(flight);
    }

    // sort the flights in each aircraft by the std time
    for(auto& aircraft : aircraft_to_flights_){
        std::sort(aircraft.second.begin(), aircraft.second.end(), [](const Flight& a, const Flight& b) {
            return a.std < b.std;
        });
    }
}

void DataLoader::_build_crew_turnarounds() {
    std::cout << "Building crew turnarounds..." << std::endl;
    
    // Constants from CrewSchedule class
    const std::chrono::hours MAX_FLY_TIME_PER_DUTY = std::chrono::hours(8);
    const std::chrono::hours MAX_DUTY_TIME_PER_FLIGHT_DUTY = std::chrono::hours(12);
    
    for (const auto& [crew_id, crew] : crews_) {
        for (const auto& flight : flights_) {
            // Skip flights crew is not qualified for
            if (crew.qualifications.find(flight.id) == crew.qualifications.end()) {
                continue;
            }

            Turnaround current_turnaround;
            current_turnaround.flights.push_back(&flight);
            current_turnaround.startTime = flight.std;
            current_turnaround.total_flight_time = std::chrono::minutes(flight.flyTime_mins);

            const Flight* current_flight_in_turnaround = &flight;
            while (true) {
                auto route_it = aircraft_routes_.find(current_flight_in_turnaround->aircraftNo);
                if (route_it == aircraft_routes_.end()) break;

                auto conn_it = std::find_if(route_it->second.connections.begin(), route_it->second.connections.end(), 
                    [&](const AircraftFlightConnection& conn){
                    return conn.flight->id == current_flight_in_turnaround->id && conn.next_flight != nullptr;
                });

                if (conn_it == route_it->second.connections.end()) break;

                const Flight* next_flight = conn_it->next_flight;
                
                // Skip if crew is not qualified for the next flight
                if (crew.qualifications.find(next_flight->id) == crew.qualifications.end()) break;

                auto new_total_flight_time = current_turnaround.total_flight_time + std::chrono::minutes(next_flight->flyTime_mins);
                auto new_duty_time = std::chrono::duration_cast<std::chrono::minutes>(next_flight->sta - current_turnaround.startTime);

                if (new_total_flight_time > MAX_FLY_TIME_PER_DUTY || new_duty_time > MAX_DUTY_TIME_PER_FLIGHT_DUTY) break;
                
                current_turnaround.flights.push_back(next_flight);
                current_turnaround.total_flight_time = new_total_flight_time;
                current_flight_in_turnaround = next_flight;
            }

            current_turnaround.endTime = current_flight_in_turnaround->sta;
            current_turnaround.startAirport = current_turnaround.flights.front()->depaAirport;
            current_turnaround.endAirport = current_turnaround.flights.back()->arriAirport;
            
            // Add to the crew's turnarounds organized by departure airport
            crew_turnarounds_by_airport_[crew_id][current_turnaround.startAirport].push_back(current_turnaround);
        }
    }
    
    std::cout << "Crew turnarounds built successfully." << std::endl;
}