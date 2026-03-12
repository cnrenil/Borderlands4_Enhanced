#include "pch.h"
#include "Localization.h"

namespace Localization
{
    Language CurrentLanguage = Language::English;
    
    // Using unordered_map for performance, string -> {Language -> string}
    static std::unordered_map<std::string, std::unordered_map<Language, std::string>> Dictionary;

    Language GetSystemLanguage()
    {
        LANGID LangID = GetUserDefaultUILanguage();
        if (PRIMARYLANGID(LangID) == LANG_CHINESE)
        {
            return Language::Chinese;
        }
        return Language::English;
    }

    void AddString(const std::string& key, const std::string& en, const std::string& zh)
    {
        Dictionary[key][Language::English] = en;
        Dictionary[key][Language::Chinese] = zh;
    }

    const char* T(const std::string& key)
    {
        auto it = Dictionary.find(key);
        if (it != Dictionary.end())
        {
            return it->second[CurrentLanguage].c_str();
        }

        static std::unordered_set<std::string> MissingKeys;
        if (MissingKeys.find(key) == MissingKeys.end())
        {
            printf("[Localization] Missing Key: %s\n", key.c_str());
            MissingKeys.insert(key);
        }

        return key.c_str(); 
    }

    void Initialize()
    {
        CurrentLanguage = GetSystemLanguage();
        
        // 1. Core UI Elements
        AddString("WINDOW_TITLE", "Borderlands 4 Antigravity Cheat", (const char*)u8"无主之地4 极速辅助");
        AddString("HELLO_GREETING", "Hello, Have Fun Cheating!", (const char*)u8"你好, 玩得开心 (支持正版/禁止商用)!");
        AddString("MENU_TITLE", "Main Menu", (const char*)u8"主菜单");
        AddString("LANGUAGE", "Language", (const char*)u8"语言 (Language)");
        AddString("VERSION_D_D_D", "Version %d.%d.%d", (const char*)u8"版本 %d.%d.%d");
        AddString("THANKS_FOR_USING_THIS_CHEAT_S", "Thanks for using this cheat, %s!", (const char*)u8"感谢使用此辅助, %s!");
        AddString("USERNAME_NOT_FOUND_BUT_THANKS_FOR_USING_ANYWAY", "Username not found, but thanks for using anyway!", (const char*)u8"未找到用户名，但仍感谢使用！");
        
        // 2. Tabs
        AddString("TAB_ABOUT", "About", (const char*)u8"关于");
        AddString("TAB_PLAYER", "Player", (const char*)u8"玩家");
        AddString("TAB_WEAPON", "Weapon", (const char*)u8"武器选项");
        AddString("TAB_WORLD", "World", (const char*)u8"世界");
        AddString("TAB_MISC", "Misc", (const char*)u8"杂项");
        AddString("TAB_CONFIG", "Config", (const char*)u8"配置");
        AddString("CONFIG", "Config", (const char*)u8"配置");
        AddString("WEAPON", "Weapon", (const char*)u8"武器选项");

        // 3. Overlay / Active Features
        AddString("ACTIVE_FEATURES_LIST", "Enabled Features List", (const char*)u8"已启用项列表");
        AddString("ACTIVE_FEATURES", "Active Features:", (const char*)u8"已开启功能:");
        AddString("SPEED_X_1F", "Speed x%.1f", (const char*)u8"速度修改 x%.1f");
        AddString("GAME_SPEED_X_F", "Game Speed: x%.1f", (const char*)u8"游戏速度: x%.1f");
        AddString("INF_AMMO", "Infinite Ammo", (const char*)u8"无限弹药");
        AddString("VOLATILE_HINT", "Note: Toggles like God Mode and Infinite Ammo must be manually enabled after injection for safety.", (const char*)u8"提示：无敌模式和无限弹药等切换类功能在重启后需手动开启。");

        // 4. Player Tab - Core Features
        AddString("ESP", "ESP", (const char*)u8"透视 (ESP)");
        AddString("AIMBOT", "Aimbot", (const char*)u8"自瞄 (Aimbot)");
        AddString("SILENT_AIM", "Silent Aim", (const char*)u8"静默自瞄 (Silent Aim)");
        AddString("GODMODE", "GodMode", (const char*)u8"无敌模式");
        AddString("DEMIGOD", "Demigod", (const char*)u8"半神模式 (1血不倒)");
        AddString("NO_TARGET", "No Target", (const char*)u8"无视玩家 (AI不攻击)");
        
        // 5. Movement Settings
        AddString("MOVEMENT", "Movement Settings", (const char*)u8"运动/位移设置");
        AddString("SPEED_HACK", "Speed Hack", (const char*)u8"移动加速");
        AddString("SPEED_VALUE", "Speed Multiplier", (const char*)u8"速度倍率");
        AddString("FLIGHT", "Flight Mode", (const char*)u8"飞行模式");
        AddString("FLIGHT_SPEED", "Flight Speed", (const char*)u8"飞行速度");
        
        // 6. Camera / Third Person
        AddString("THIRD_PERSON", "Third Person", (const char*)u8"第三人称");
        AddString("THIRD_PERSON_CENTERED", "Centered (Third Person)", (const char*)u8"居中 (第三人称)");
        AddString("THIRD_PERSON_OTS", "Over The Shoulder (ADS)", (const char*)u8"越肩瞄准 (右键开镜)");
        AddString("OTS_OFFSET_X", "OTS Offset X (Forward)", (const char*)u8"越肩偏移 X (前后)");
        AddString("OTS_OFFSET_Y", "OTS Offset Y (Right)", (const char*)u8"越肩偏移 Y (左右)");
        AddString("OTS_OFFSET_Z", "OTS Offset Z (Up)", (const char*)u8"越肩偏移 Z (上下)");
        AddString("ADS_FIRST_PERSON", "ADS switch to First Person", (const char*)u8"右键开镜切换到第一人称");
        AddString("FREE_CAM", "Free Camera", (const char*)u8"自由视角");
        AddString("FREECAM_BLOCK_INPUT", "Block User Input", (const char*)u8"屏蔽用户输入 (自由视角)");

        // 7. Player Progression
        AddString("PLAYER_PROGRESSION", "Player Progression:", (const char*)u8"玩家进度:");
        AddString("EXPERIENCE_LEVEL", "Experience Level", (const char*)u8"经验等级");
        AddString("SET_EXPERIENCE_LEVEL", "Set Experience Level", (const char*)u8"设置经验等级");
        AddString("GIVE_5_LEVELS", "Give 5 Levels", (const char*)u8"增加5级");
        
        // 8. World Tab
        AddString("WORLD_ACTIONS", "World Actions:", (const char*)u8"世界操作:");
        AddString("KILL_ENEMIES", "Kill All Enemies", (const char*)u8"击杀所有敌人");
        AddString("CLEAR_GROUND_ITEMS", "Clear Ground Items", (const char*)u8"清理地面物品");
        AddString("TELEPORT_LOOT", "Teleport Loot to Me", (const char*)u8"战利品传送");
        AddString("SPAWN_ITEMS", "Spawn Items", (const char*)u8"刷出物品");
        AddString("PLAYERS_ONLY", "Players Only", (const char*)u8"世界冻结");
        AddString("GAME_SPEED", "Game Speed", (const char*)u8"游戏速度");
        AddString("MAP_TELEPORT", "Map Waypoint Teleport", (const char*)u8"地图标点传送 (右键标点再取消)");
        AddString("MAP_TELEPORT_WINDOW", "Map TP Window (s)", (const char*)u8"地图传送延迟窗口 (秒)");
        AddString("BLACK_MARKET_BYPASS", "Black Market Bypass", (const char*)u8"黑市查看冷却绕过");
        
        // 9. Currency Settings
        AddString("CURRENCY_SETTINGS", "Currency Settings:", (const char*)u8"货币修改:");
        AddString("CASH", "Cash", (const char*)u8"金钱");
        AddString("ERIDIUM", "Eridium", (const char*)u8"镒矿");
        AddString("VC_TICKETS", "Vault Card Tickets", (const char*)u8"卡片/奖牌");
        
        // 10. Aimbot Tab
        AddString("STANDARD_AIMBOT_SETTINGS", "Standard Aimbot Settings", (const char*)u8"常规自瞄设置");
        AddString("REQUIRE_LOS", "Require LOS", (const char*)u8"需要可见性 (LOS)");
        AddString("DRAW_FOV", "Draw FOV", (const char*)u8"显示视野范围");
        AddString("DRAW_ARROW", "Draw Arrow", (const char*)u8"显示指向箭头");
        AddString("SMOOTH_AIM", "Smooth Aim", (const char*)u8"平滑自瞄");
        AddString("SMOOTHING", "Smoothing", (const char*)u8"平滑系数");
        AddString("AIMBOT_FOV", "Aimbot FOV", (const char*)u8"自瞄视野 (Aimbot FOV)");
        AddString("MAX_DISTANCE", "Max Distance", (const char*)u8"最大距离");
        AddString("TARGET_BONE", "Target Bone", (const char*)u8"目标骨骼");
        AddString("CHOOSE_BONE_TOOLTIP", "Choose the bone to inject damage directly to. E.g Head = 100% Critical Hit", (const char*)u8"选择要直接注入伤害的部位。例如 头部 = 100% 暴击");
        
        // 11. Weapon Tab
        AddString("INSTANT_HIT", "Instant Hit & Projectile Speed", (const char*)u8"瞬间命中 (投射物速度)");
        AddString("PROJECTILE_SPEED", "Projectile Speed Multiplier", (const char*)u8"子弹飞行速度倍率");
        AddString("RAPID_FIRE", "Rapid Fire", (const char*)u8"连射 (高射速)");
        AddString("FIRE_RATE_MODIFIER", "Fire Rate Multiplier", (const char*)u8"射速倍率");
        AddString("NO_RECOIL", "No Recoil", (const char*)u8"无后坐力");
        AddString("RECOIL_REDUCTION", "Recoil Reduction %", (const char*)u8"后坐力减免比例");
        AddString("NO_SWAY", "No Sway", (const char*)u8"无武器摇晃");
        AddString("HOMING_PROJECTILES", "Homing Projectiles", (const char*)u8"自导子弹 (魔法子弹)");
        AddString("HOMING_RANGE", "Homing Range", (const char*)u8"自导范围");
        AddString("TRIGGERBOT", "TriggerBot", (const char*)u8"自动射击 (TriggerBot)");
        AddString("REQUIRE_KEY_HELD", "Require Key Held", (const char*)u8"需要热键配合");
        AddString("TARGET_ALL", "Target All", (const char*)u8"目标所有单位");
        AddString("INSTANT_RELOAD", "Instant Reload", (const char*)u8"秒换弹 (瞬时装填装弹)");
        
        // 12. Misc Settings
        AddString("MISC_SETTINGS", "Misc Settings", (const char*)u8"杂项设置");
        AddString("ENABLE_FOV_CHANGER", "Enable FOV Changer", (const char*)u8"修改游戏视野");
        AddString("FOV_VALUE", "FOV Value", (const char*)u8"视野角度");
        AddString("ENABLE_VIEWMODEL_FOV", "Enable ViewModel FOV", (const char*)u8"修改武器视角大小 (ViewModel)");
        AddString("VIEWMODEL_FOV_VALUE", "ViewModel FOV Value", (const char*)u8"武器视角角度");
        AddString("DISABLE_VOLUMETRIC_CLOUDS", "Disable Volumetric Clouds", (const char*)u8"禁用体积云 (提升帧率)");
        
        // 13. Config & Debug
        AddString("ESP_SETTINGS", "ESP Settings", (const char*)u8"透视设置");
        AddString("SHOW_BOX", "Show Box", (const char*)u8"显示方框");
        AddString("SHOW_DISTANCE", "Show Distance", (const char*)u8"显示敌方距离");
        AddString("SHOW_BONES", "Show Bones", (const char*)u8"显示骨骼");
        AddString("SHOW_NAME", "Show Name", (const char*)u8"显示敌方名称");
        AddString("ENEMY_COLOR", "Enemy Color", (const char*)u8"敌人颜色");
        AddString("DEBUG", "Debug", (const char*)u8"调试 (Debug)");
        AddString("ENABLE_EVENT_DEBUG_LOGS", "Enable Event Debug Logs", (const char*)u8"启用事件调试日志");
        AddString("SAVE_SETTINGS", "Save Settings", (const char*)u8"保存设置");
        AddString("LOAD_SETTINGS", "Load Settings", (const char*)u8"加载设置");
        AddString("SILENT_AIM_SETTINGS", "Silent Aim Settings", (const char*)u8"静默自瞄设置 (Silent Aim Settings)");
    }
}
