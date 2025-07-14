// src/Core/SchedulingData.h

#pragma once

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <filesystem>
#include <unordered_set>

using TimePoint = std::chrono::system_clock::time_point;

// // 代表一个可被调度的任务段 (航班或巴士)
// struct Leg {
//     int rnum = -1;            // 排序后的全局唯一索引 (rank number)
//     bool is_flight = false;           // true代表航班, false代表巴士 
//     bool is_ground_duty = false;      // true代表地面任务
//     bool is_grd_rest = false;         // true代表地面任务休息
//     std::string id;           // 原始ID (e.g., "Flt_100002" or "ddh_100001")
//     std::string dep_airport;
//     std::string arr_airport;
//     TimePoint dep_time;
//     TimePoint arr_time;
//     std::string aircraftNo; // 飞机尾号
//     int flyTime_mins;       // 飞行时间（分钟）
//     std::string crewId = "";     // 机组成员ID
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

struct AircraftFlightConnection {
    const Flight* flight;
    const Flight* next_flight; // Connection on the same aircraft
    std::chrono::minutes turnaround_time;
};

struct AircraftRoute {
    std::string aircraftNo;
    std::vector<AircraftFlightConnection> connections;
};

struct Bus{
    std::string id;
    std::string depaAirport;
    std::string arriAirport;
    TimePoint td;
    TimePoint ta;
};

// Forward declaration for Turnaround
struct Turnaround {
    std::vector<const Flight*> flights;
    const Bus* positioning_bus = nullptr;
    TimePoint startTime;
    TimePoint endTime;
    std::chrono::minutes total_flight_time;
    std::string startAirport;
    std::string endAirport;
};

// 代表一个预先分配的地面勤务
struct GroundDuty {
    std::string crew_id;
    std::string id;
    std::string airport;
    TimePoint start_time;
    TimePoint end_time;
    int is_duty;
};

// 代表一个机组成员及其当前状态
struct Crew {
    std::string id;                   // 机组成员ID
    std::string base;                 // 机组成员的基地机场
    std::string initialStayStation;   // 初始停留机场

    // 预先分配的地面占位
    std::vector<GroundDuty> groundDuties;

    // 机组成员的资格列表
    std::unordered_set<std::string> qualifications;

    // // 执飞(fly)和置位(deadhead)的航班rnum列表 
    // std::vector<int> fd_list; 

    // // 仅作为乘客搭乘(deadhead)的巴士rnum列表 
    // std::vector<int> h_list;

    // flight(non-deadhead) list
    std::vector<Flight> flight_list;

    // flight(deadhead) list
    std::vector<Flight> ddh_flight_list;

    // bus(deadhead) list
    std::vector<Bus> ddh_bus_list;

    // 成员当前任务的出发时间 
    TimePoint current_dp;

    // 成员当前任务的到达时间 
    TimePoint current_ar;

    // 成员执飞/搭乘任务经过的机场列表，第一个机场是其基地 
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

    // --- 公共访问接口 ---
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
    const std::map<std::string, AircraftRoute>& getAircraftRoutes() const { return aircraft_routes_; }
    const std::map<std::string, std::map<std::string, std::vector<Turnaround>>>& getCrewTurnarounds() const { return crew_turnarounds_by_airport_; }
private:
    // --- 成员变量 ---
    std::map<std::string, Crew> crews_;
    // std::vector<Leg> schedulable_legs_; // 已排序并分配好rnum
    std::map<std::string, std::vector<std::string>> flight_to_crews_;
    std::vector<Flight> flights_;
    std::vector<Bus> buses_;
    std::vector<std::string> layover_stations_;
    std::string data_version_;
    std::map<std::string, std::vector<Flight>> aircraft_to_flights_;
    std::map<std::string, AircraftRoute> aircraft_routes_;
    // Maps a route (departure airport, arrival airport) to a list of leg indices that connect those airports
    std::map<std::pair<std::string, std::string>, std::vector<int>> route_to_legs_index_;
    
    // Pre-computed turnarounds for each crew organized by departure airport
    // crew_id -> departure_airport -> list of turnarounds starting from that airport
    std::map<std::string, std::map<std::string, std::vector<Turnaround>>> crew_turnarounds_by_airport_;


    // --- 私有加载函数 ---
    void _load_crews(const std::filesystem::path& file_path);
    void _load_flights(const std::filesystem::path& flight_path);
    void _load_buses(const std::filesystem::path& bus_path);
    void _load_layover_stations(const std::filesystem::path& file_path);
    void _load_and_link_ground_duties(const std::filesystem::path& file_path);
    void _link_crew_qualifications(const std::filesystem::path& file_path);
    void _sort_qualifications();
    void _link_flights_to_aircrafts();
    void _build_aircraft_routes();
    void _build_crew_turnarounds();
};


// // 用于打包所有输入数据
// struct ProcessedData {
//     std::vector<Leg> legs;
//     std::vector<Crew> crews;
//     std::vector<std::string> layoverStations;
// };