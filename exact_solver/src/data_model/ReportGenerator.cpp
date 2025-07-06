// ReportGenerator.cpp
#include "ReportGenerator.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <iomanip> // 用于 std::setw, std::left 等格式控制
#include <sstream> // 用于 std::stringstream

// 辅助函数：将 time_point 格式化为 "MM-DD HH:MM"
std::string format_time(const time_point& tp) {
    // auto zoned_time = std::chrono::zoned_time(std::chrono::current_zone(), tp);
    return std::format("{:%m-%d %H:%M}", tp);
}

// 辅助函数：格式化任务类型
std::string format_task_type(const std::string& type) {
    std::string result = type;
    std::replace(result.begin(), result.end(), '_', ' ');
    // 首字母大写
    if (!result.empty()) {
        result[0] = toupper(result[0]);
        for (size_t i = 1; i < result.length() -1; ++i) {
            if (result[i-1] == ' ') {
                result[i] = toupper(result[i]);
            }
        }
    }
    return result;
}

namespace ReportGenerator {

void generate_readable_report(const Solution& solution, const std::string& filename, const std::set<std::string>& uncovered_flights) {
    std::ofstream report_file(filename);
    if (!report_file.is_open()) {
        std::cerr << "错误：无法写入报告文件 '" << filename << "'." << std::endl;
        return;
    }

    std::stringstream ss;

    // 报告头
    auto now = std::chrono::system_clock::now();
    ss << "排班方案生成时间: " << std::format("{:%Y-%m-%d %H:%M:%S}", now) << "\n";
    long long assigned_crews = 0;
    for(const auto& pair : solution) {
        if (!pair.second.empty()) {
            assigned_crews++;
        }
    }
    ss << "总计分配机组数: " << assigned_crews << "\n\n";

    // 遍历所有机组
    for (const auto& pair : solution) {
        const std::string& crew_id = pair.first;
        const auto& fdps = pair.second;
        if (fdps.empty()) continue;

        ss << std::string(60, '=') << "\n";
        ss << "机组ID (Crew ID): " << crew_id << "\n";
        ss << std::string(60, '=') << "\n";

        // 按时间排序FDP
        auto sorted_FDPs = fdps;
        std::sort(sorted_FDPs.begin(), sorted_FDPs.end(), 
            [](const FDP& a, const FDP& b){ return a.get_start_time() < b.get_start_time(); });

        for (size_t i = 0; i < sorted_FDPs.size(); ++i) {
            const auto& fdp = sorted_FDPs[i];
            ss << "\n--- FDP #" << i + 1 << " ---\n";
            ss << "  时间范围: " << format_time(fdp.get_start_time()) << " -> " << format_time(fdp.get_end_time()) << "\n";
            ss << "  航线: " << fdp.get_start_airport() << " -> " << fdp.get_end_airport() << "\n";

            for (const auto& task : fdp.tasks) {
                std::stringstream task_line;
                task_line << "    * [" << std::left << std::setw(15) << format_task_type(task.task_type) << "] "
                          << std::left << std::setw(12) << task.id << " | "
                          << task.start_airport << " " << format_time(task.start_time) << " -> "
                          << task.end_airport << " " << format_time(task.end_time);
                ss << task_line.str() << "\n";
            }

            // 计算休息期
            if (i < sorted_FDPs.size() - 1) {
                auto rest_duration = sorted_FDPs[i+1].get_start_time() - fdp.get_end_time();
                auto hours = std::chrono::duration_cast<std::chrono::hours>(rest_duration);
                auto minutes = std::chrono::duration_cast<std::chrono::minutes>(rest_duration % std::chrono::hours(1));
                ss << "\n  >>> 休息期 (Rest Period): " << hours.count() << "小时 " << minutes.count() << "分钟 <<<\n";
            }
        }
        ss << "\n";
    }
    
    // 添加未覆盖航班列表
    if (!uncovered_flights.empty()) {
        ss << "\n" << std::string(60, '=') << "\n";
        ss << "未覆盖航班列表 (Uncovered Flights): " << uncovered_flights.size() << " 个\n";
        ss << std::string(60, '=') << "\n\n";
        
        std::vector<std::string> sorted_flights(uncovered_flights.begin(), uncovered_flights.end());
        std::sort(sorted_flights.begin(), sorted_flights.end());
        
        size_t count = 0;
        for (const auto& flight_id : sorted_flights) {
            ss << std::left << std::setw(15) << flight_id;
            if (++count % 5 == 0) ss << "\n";
        }
        if (count % 5 != 0) ss << "\n"; // 确保最后一行有换行
    }

    report_file << ss.str();
    std::cout << "可读报告已成功生成: '" << filename << "'" << std::endl;
}

void generate_submission_file(const Solution& solution, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "错误：无法写入提交文件 '" << filename << "'." << std::endl;
        return;
    }

    file << "crewId,taskId,isDDH\n"; // 写入表头

    for (const auto& pair : solution) {
        const std::string& crew_id = pair.first;
        if (pair.second.empty()) continue;

        // 收集并排序所有任务
        std::vector<Task> all_tasks_for_crew;
        for (const auto& fdp : pair.second) {
            all_tasks_for_crew.insert(all_tasks_for_crew.end(), fdp.tasks.begin(), fdp.tasks.end());
        }
        std::sort(all_tasks_for_crew.begin(), all_tasks_for_crew.end(), 
            [](const Task& a, const Task& b){ return a.start_time < b.start_time; });
        
        // 写入每一行
        for (const auto& task : all_tasks_for_crew) {
            int is_ddh = (task.task_type.find("deadhead") != std::string::npos || task.task_type == "bus") ? 1 : 0;
            file << crew_id << "," << task.id << "," << is_ddh << "\n";
        }
    }
    std::cout << "标准提交文件已成功生成: '" << filename << "'" << std::endl;
}

} // namespace ReportGenerator