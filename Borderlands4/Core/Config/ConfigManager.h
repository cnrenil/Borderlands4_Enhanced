#pragma once
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ConfigManager
{
    // Supported types for configuration
    using ConfigValue = std::variant<bool, int, float, std::string, ImVec4, ImVec2>;

    void Initialize();
    void SaveSettings();
    void LoadSettings();

    // Central storage for all settings
    extern std::unordered_map<std::string, ConfigValue> ConfigMap;

    // Register a setting with its default value
    void Register(const std::string& key, ConfigValue defaultValue);

    // Get a reference to a setting value (handles missing keys gracefully)
    template<typename T>
    T& Get(const std::string& key) {
        return std::get<T>(ConfigMap.at(key));
    }

    // Shorthand helpers for common types
    inline bool& B(const std::string& key) { return Get<bool>(key); }
    inline float& F(const std::string& key) { return Get<float>(key); }
    inline int& I(const std::string& key) { return Get<int>(key); }
    inline std::string& S(const std::string& key) { return Get<std::string>(key); }
    inline ImVec4& Color(const std::string& key) { return Get<ImVec4>(key); }
    inline ImVec2& Vec2(const std::string& key) { return Get<ImVec2>(key); }

    // Check if a setting exists
    bool Exists(const std::string& key);
}
