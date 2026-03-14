#include "pch.h"


namespace Logger
{
    static std::mutex LogMutex;
    static std::ofstream LogFile;
    static std::ofstream EventLogFile;
    static std::vector<LogEntry> LogHistory;
    static std::map<std::string, std::chrono::steady_clock::time_point> ThrottleMap;
    static bool bIsRecording = false;

    static const char* LevelToString(Level level) {
        switch (level) {
            case Level::Debug: return "DEBUG";
            case Level::Info: return "INFO";
            case Level::Warning: return "WARN";
            case Level::Error: return "ERROR";
            case Level::Critical: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    void Initialize()
    {
        std::lock_guard<std::mutex> lock(LogMutex);
        
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        std::string dir = std::string(path).substr(0, std::string(path).find_last_of("\\/"));
        std::string logPath = dir + "\\B4_Hack_Log.txt";

        LogFile.open(logPath, std::ios::out | std::ios::trunc);
        if (LogFile.is_open()) {
            LogFile << "[SYSTEM] Logger Initialized at " << logPath << "\n";
        }
    }

    static void InternalStopRecording()
    {
        if (!bIsRecording) return;
        bIsRecording = false;
        if (EventLogFile.is_open()) {
            EventLogFile << "[SYSTEM] Event Recording Stopped\n";
            EventLogFile.close();
        }
    }

    void Shutdown()
    {
        std::lock_guard<std::mutex> lock(LogMutex);
        InternalStopRecording();
        if (LogFile.is_open()) {
            LogFile << "[SYSTEM] Logger Shutdown\n";
            LogFile.close();
        }
    }

    void InternalLog(Level level, const char* category, const char* format, va_list args)
    {
        // 1. Format the message
        char buffer[2048];
        vsnprintf(buffer, sizeof(buffer), format, args);
        std::string message = buffer;

        auto now = std::chrono::system_clock::now();
        auto now_t = std::chrono::system_clock::to_time_t(now);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now_t);

        std::lock_guard<std::mutex> lock(LogMutex);

        // 2. Add to history
        LogHistory.push_back({ level, category ? category : "None", message, now });
        if (LogHistory.size() > 500) LogHistory.erase(LogHistory.begin());

        // 3. Build full string
        std::stringstream ss;
        ss << "[" << std::put_time(&timeinfo, "%H:%M:%S") << "] "
           << "[" << std::left << std::setw(5) << LevelToString(level) << "] "
           << "[" << std::left << std::setw(10) << (category ? category : "None") << "] "
           << message << "\n";
        
        std::string finalLog = ss.str();

        // 4. Output to Console (if enabled in UI or always for Errors)
        if (level >= Level::Info || (ConfigManager::ConfigMap.count("Misc.Debug") && ConfigManager::B("Misc.Debug")))
        {
            printf("%s", finalLog.c_str());
        }

        // 5. Output to File
        if (LogFile.is_open()) {
            LogFile << finalLog;
            LogFile.flush();
        }
    }

    void Log(Level level, const std::string& category, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        InternalLog(level, category.c_str(), format, args);
        va_end(args);
    }

    void RawLog(Level level, const char* category, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        InternalLog(level, category, format, args);
        va_end(args);
    }

    void LogThrottled(Level level, const std::string& category, int intervalMs, const char* format, ...)
    {
        std::string throttleKey = category + format;
        auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(LogMutex);
            if (ThrottleMap.count(throttleKey)) {
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - ThrottleMap[throttleKey]).count();
                if (diff < intervalMs) {
                    return;
                }
            }
            ThrottleMap[throttleKey] = now;
        }

        va_list args;
        va_start(args, format);
        InternalLog(level, category.c_str(), format, args);
        va_end(args);
    }

    void StartRecording()
    {
        std::lock_guard<std::mutex> lock(LogMutex);
        if (bIsRecording) return;

        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        std::string dir = std::string(path).substr(0, std::string(path).find_last_of("\\/"));
        std::string eventPath = dir + "\\B4_Hack_Events.txt";

        EventLogFile.open(eventPath, std::ios::out | std::ios::trunc);
        if (EventLogFile.is_open()) {
            bIsRecording = true;
            EventLogFile << "[SYSTEM] Event Recording Started\n";
        }
    }

    void StopRecording()
    {
        std::lock_guard<std::mutex> lock(LogMutex);
        InternalStopRecording();
    }

    bool IsRecording()
    {
        return bIsRecording;
    }

    void LogEvent(const std::string& className, const std::string& funcName, const std::string& objName)
    {
        if (!bIsRecording) return;

        std::lock_guard<std::mutex> lock(LogMutex);
        if (EventLogFile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto now_t = std::chrono::system_clock::to_time_t(now);
            struct tm timeinfo;
            localtime_s(&timeinfo, &now_t);

            EventLogFile << "[" << std::put_time(&timeinfo, "%H:%M:%S") << "] "
                         << className << "::" << funcName << " (" << objName << ")\n";
        }
    }

    std::vector<LogEntry> GetRecentLogs(int count)
    {
        std::lock_guard<std::mutex> lock(LogMutex);
        if (count > (int)LogHistory.size()) count = (int)LogHistory.size();
        return std::vector<LogEntry>(LogHistory.end() - count, LogHistory.end());
    }
}
