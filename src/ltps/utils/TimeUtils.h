#pragma once

#include <chrono>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace time_utils {

using Clock     = std::chrono::system_clock;
using TimePoint = std::chrono::time_point<Clock>;

// 获取当前时间点
inline TimePoint now() { return Clock::now(); }

// 获取当前本地时间字符串 yyyy-MM-dd HH:mm:ss
inline std::string getCurrentTimeString() {
    auto    nowTime = Clock::now();
    auto    timeT   = Clock::to_time_t(nowTime);
    std::tm localTime{};
    localtime_s(&localTime, &timeT);
    return fmt::format("{:%Y-%m-%d %H:%M:%S}", localTime);
}

// 时间转紧凑字符串 yyyyMMddHHmmss (字典序 = 时间序, 用作 key 数据段)
inline std::string toCompactTimeString(const TimePoint& tp) {
    auto    timeT = Clock::to_time_t(tp);
    std::tm localTime{};
    localtime_s(&localTime, &timeT);
    return fmt::format("{:%Y%m%d%H%M%S}", localTime);
}

// 获取当前本地时间紧凑字符串 yyyyMMddHHmmss
inline std::string getCurrentCompactTimeString() { return toCompactTimeString(Clock::now()); }

// 时间转字符串 yyyy-MM-dd HH:mm:ss
inline std::string timeToString(const TimePoint& tp) {
    auto    timeT = Clock::to_time_t(tp);
    std::tm localTime{};
    localtime_s(&localTime, &timeT);
    return fmt::format("{:%Y-%m-%d %H:%M:%S}", localTime);
}

// 字符串转时间（支持 yyyy-MM-dd HH:mm:ss）
inline std::optional<TimePoint> parseTimeString(const std::string& timeStr) {
    std::tm            tm{};
    std::istringstream ss(timeStr);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        return std::nullopt;
    }
    return Clock::from_time_t(std::mktime(&tm));
}

// 获取将来时间点
inline TimePoint futureTime(int seconds) { return Clock::now() + std::chrono::seconds(seconds); }


class Timer {
public:
    explicit Timer(std::string name = "Timer") : mName(std::move(name)), mStart(Clock::now()) {}

    ~Timer() {
        auto end      = Clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - mStart).count();
        std::cout << fmt::format("[{}] Time: {} ms\n", mName, duration);
    }

private:
    std::string mName;
    TimePoint   mStart;
};

} // namespace time_utils
