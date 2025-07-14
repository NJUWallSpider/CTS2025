// src/Core/Utils.h

#pragma once

#include <string>
#include <chrono>
#include <sstream>
#include <iomanip>

class Utils {
public:
    // 将 "YYYY/M/D H:M" 格式的字符串转换为秒级时间戳
    static long parseTime( std::string& timeStr) {
        std::tm tm = {};
        std::stringstream ss(timeStr);
        ss >> std::get_time(&tm, "%Y/%m/%d %H:%M");
        if (ss.fail()) {
            // 可以加入更详细的错误处理
            return -1; 
        }
        auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        return std::chrono::duration_cast<std::chrono::seconds>(time_point.time_since_epoch()).count();
    }
};