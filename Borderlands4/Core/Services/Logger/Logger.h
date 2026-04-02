#pragma once
#include "pch.h"
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <map>

namespace Logger
{
    enum class Level {
        Debug,
        Info,
        Warning,
        Error,
        Critical
    };

    struct LogEntry {
        Level Lvl;
        std::string Category;
        std::string Message;
        std::chrono::system_clock::time_point Timestamp;
    };

    void Initialize();
    void Shutdown();

    // Standard Logging
    void Log(Level level, const std::string& category, const char* format, ...);
    void RawLog(Level level, const char* category, const char* format, ...);

    // Throttled Logging (only logs if 'intervalMs' has passed since last log with same category + format)
    void LogThrottled(Level level, const std::string& category, int intervalMs, const char* format, ...);

    // Helper Shorthands
    inline void Debug(const std::string& cat, const char* fmt, ...) { /* Handled in cpp */ }
    inline void Info(const std::string& cat, const char* fmt, ...) { /* Handled in cpp */ }
    inline void Warn(const std::string& cat, const char* fmt, ...) { /* Handled in cpp */ }
    inline void Error(const std::string& cat, const char* fmt, ...) { /* Handled in cpp */ }

    // Event Recording (Specific for ProcessEvent tracing)
    void StartRecording();
    void StopRecording();
    bool IsRecording();
    void LogEvent(const std::string& className, const std::string& funcName, const std::string& objName);

    // Export helpers
    std::vector<LogEntry> GetRecentLogs(int count = 100);
}

// Macros for ease of use and to capture format strings for throttling
#define LOG_DEBUG(cat, fmt, ...) Logger::Log(Logger::Level::Debug, cat, fmt, ##__VA_ARGS__)
#define LOG_INFO(cat, fmt, ...)  Logger::Log(Logger::Level::Info, cat, fmt, ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...)  Logger::Log(Logger::Level::Warning, cat, fmt, ##__VA_ARGS__)
#define LOG_ERROR(cat, fmt, ...) Logger::Log(Logger::Level::Error, cat, fmt, ##__VA_ARGS__)

#define LOG_THROTTLE(interval, cat, fmt, ...) Logger::LogThrottled(Logger::Level::Info, cat, interval, fmt, ##__VA_ARGS__)
