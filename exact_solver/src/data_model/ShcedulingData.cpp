// SchedulingData.cpp
#include "SchedulingData.hpp"
#include "ReportGenerator.hpp"
#include "csv.h" // 引入CSV解析库
#include <iomanip> // 用于日期解析
#include <sstream> // 用于日期解析
#include <chrono>
#include <sstream>
#include <stdexcept> // 用于抛出异常
#include <iomanip>
#include <sstream>
#include <random> // 用于随机删除bus
#include <algorithm> // 用于随机删除bus

// --- 时间转换辅助函数 ---
// 将 "YYYY-MM-DD HH:MM:SS" 格式的字符串转换为 time_point
time_point string_to_time_point(const std::string& time_str) {
    std::tm tm = {};
    std::istringstream ss(time_str);
    
    // 尝试解析字符串
    ss >> std::get_time(&tm, "%Y/%m/%d %H:%M");
    
    if (ss.fail()) {
        // ss >> std::get_time(&tm, "%Y-%m-%d %H:%M");
        // if (ss.fail()) {
            throw std::runtime_error("Failed to parse datetime string");
        // }
    }
    
    // 转换为 time_t（UTC 时间）
    std::time_t tt = std::mktime(&tm);
    
    if (tt == -1) {
        throw std::runtime_error("Invalid datetime value");
    }
    
    // 转换为 UTC 时间点（需要减去时区偏移）
    auto tp = std::chrono::system_clock::from_time_t(tt);
    
    // 获取本地时间与 UTC 的时差
    std::time_t now = std::time(nullptr);
    std::tm local_tm = *std::localtime(&now);
    std::tm utc_tm = *std::gmtime(&now);
    local_tm.tm_isdst = utc_tm.tm_isdst; // 统一夏令时设置
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
        oss << "  飞行:" << fly_time.count() << "min";
        if (!aircraft_no.empty()) oss << "  机号:" << aircraft_no;
    }
    return oss.str();
}


std::string FDP::to_string() const {
    std::ostringstream oss;
    oss << "FDP: " << get_start_airport() << " -> " << get_end_airport() << "  "
        << format_time(get_start_time()) << " ~ " << format_time(get_end_time()) << "\n"
        << "  总飞行时长: " << std::fixed << std::setprecision(1)
        << std::chrono::duration<double, std::ratio<3600>>(get_flight_hours()).count() << "小时"
        << "  Deadhead数: " << get_deadhead_count()
        << "  日历天数: " << get_calendar_days()
        << "  Score: " << get_score() << "\n"
        << "  任务列表:\n";
    for (const auto& task : tasks) {
        oss << "    " << task.to_string() << "\n";
    }
    return oss.str();
}


// --- SchedulingData 类的实现 ---

SchedulingData::SchedulingData(const std::filesystem::path& data_path, const std::filesystem::path& heuristic_path, size_t max_tasks_threshold) 
    : max_tasks_threshold_(max_tasks_threshold) {
    std::cout << "开始加载数据..." << std::endl;
    // 读取需要排除的任务
    _load_excluded_tasks(heuristic_path);

    _load_crews(data_path / "crew.csv");
    _load_flights(data_path / "flight.csv");
    _load_buses(data_path / "busInfo.csv");
    _load_layover_stations(data_path / "layoverStation.csv");
    
    _link_ground_duties(data_path / "groundDuty.csv");
    _link_crew_qualifications(data_path / "crewLegMatch.csv");


    std::cout << "数据加载完毕。共加载 " << crews_.size() << " 名机长, "
              << flights_.size() << " 个航班, " << buses_.size() << " 趟大巴。" << std::endl;
}

void SchedulingData::_load_crews(const std::filesystem::path& file_path) {
    io::CSVReader<3, io::trim_chars<' '>> in(file_path.string());
    in.read_header(io::ignore_extra_column, "crewId", "base", "stayStation");
    
    std::string crewId, base, stayStation;
    while(in.read_row(crewId, base, stayStation)){
        // 使用 emplace 直接在 map 中构造对象，避免额外拷贝
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
    
    // 如果总任务数量超过阈值，随机删除多余的bus
    // if (buses_.size() + flights_.size() > max_tasks_threshold_) {
    //     std::cout << "Bus数量(" << buses_.size() << ") + 航班数量(" << flights_.size() 
    //              << ")超过阈值" << max_tasks_threshold_ << "，随机删除多余的bus..." << std::endl;
        
    //     // 将所有bus ID放入vector中
    //     std::vector<std::string> bus_ids;
    //     bus_ids.reserve(buses_.size());
    //     for (const auto& bus : buses_) {
    //         bus_ids.push_back(bus.first);
    //     }
        
    //     // 随机打乱顺序
    //     std::random_device rd;
    //     std::mt19937 g(rd());
    //     std::shuffle(bus_ids.begin(), bus_ids.end(), g);
        
    //     // 删除多余的bus
    //     size_t to_remove = buses_.size() + flights_.size() - max_tasks_threshold_;
    //     for (size_t i = 0; i < to_remove; ++i) {
    //         buses_.erase(bus_ids[i]);
    //     }
        
    //     std::cout << "已随机删除 " << to_remove << " 个bus，剩余 " << buses_.size() << " 个bus。" << std::endl;
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

    // 创建一个临时的集合来记录有ground duty的机组
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
            crewId = crewId.substr(1, crewId.size() - 2); // 去掉引号
            legId = legId.substr(1, legId.size() - 2); 
        }
        auto it = crews_.find(crewId);
        if (it != crews_.end()) {
            it->second.qualified_flights.insert(legId);
        }
    } 

    // // 删除没有资质的机组
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
    //         // 加入所有flights
    //         for (const auto& flight : flights_) {
    //             it->second.qualified_flights.insert(flight.second.id);
    //         }
    //     }
    //     it++;
    // }
    
    // std::cout << "删除了 " << removed_count << " 个无资质机组，剩余 " << crews_.size() << " 个机组。" << std::endl;
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

// --- 公共查询接口的实现 ---

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

