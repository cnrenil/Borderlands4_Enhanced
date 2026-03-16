#pragma once
#include <string>
#include <map>
#include <unordered_map>
#include <initializer_list>

enum class Language {
    English,
    Chinese
};

namespace Localization
{
    struct LocalizedText
    {
        std::unordered_map<Language, std::string> Values;

        const char* Resolve() const;

        static LocalizedText Make(std::initializer_list<std::pair<const Language, std::string>> values);
    };

    Language GetSystemLanguage();
    void Initialize();
    
    // Global language state
    extern Language CurrentLanguage;
    
    // Dictionary management
    void Register(const std::string& key, const std::string& en, const std::string& zh);
    const char* T(const std::string& key);
}
