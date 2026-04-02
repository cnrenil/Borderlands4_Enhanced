#pragma once
#include <string>
#include <functional>
#include "Core/Services/Localization/Localization.h"

namespace GUI
{
    struct ThemeDefinition
    {
        std::string Id;
        Localization::LocalizedText DisplayName;
        std::function<void(ImGuiStyle&)> Apply;
    };

    namespace ThemeManager
    {
        void Initialize();
        void Register(const std::string& id, const Localization::LocalizedText& displayName, std::function<void(ImGuiStyle&)> apply);
        void ApplyByIndex(int index);
        int GetThemeCount();
        const ThemeDefinition* GetThemeByIndex(int index);
        const char* GetThemeDisplayName(int index);
        int ClampThemeIndex(int index);
        void ApplyRuntimeAccent();
    }
}
