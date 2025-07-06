// ReportGenerator.hpp
#pragma once

#include "SchedulingData.hpp" // 包含核心数据结构
#include <string>
#include <set>
#include <map>
#include <vector>

using Solution = std::map<std::string, std::vector<FDP>>;

std::string format_time(const time_point& tp);

namespace ReportGenerator {
/**
 * @brief 
 * @param solution 求解器输出的最终方案。
 * @param filename 要保存的报告文件名。
 * @param uncovered_flights 未覆盖航班集合。
 */
void generate_readable_report(const Solution& solution, const std::string& filename, 
                            const std::set<std::string>& uncovered_flights = {});

/**
 * @brief 根据求解器生成的方案，创建一个标准格式的CSV提交文件。
 * @param solution 求解器输出的最终方案。
 * @param filename 要保存的CSV文件名。
 */
void generate_submission_file(const Solution& solution, const std::string& filename);

} // namespace ReportGenerator