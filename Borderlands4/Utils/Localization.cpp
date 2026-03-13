#include "pch.h"


namespace Localization
{
    Language CurrentLanguage = Language::English;
    
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

    void Register(const std::string& key, const std::string& en, const std::string& zh)
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
            LOG_WARN("Localization", "Missing Key: %s", key.c_str());
            MissingKeys.insert(key);
        }

        return key.c_str(); 
    }

    void Initialize()
    {
        CurrentLanguage = GetSystemLanguage();
        
        // 1. Core UI Elements
        Register("WINDOW_TITLE", "Borderlands 4 Antigravity Cheat", (const char*)u8"无主之地4 极速辅助");
        Register("HELLO_GREETING", "Hello, Have Fun Cheating!", (const char*)u8"你好, 玩得开心 (支持正版/禁止商用)!");
        Register("MENU_TITLE", "Main Menu", (const char*)u8"主菜单");
        Register("LANGUAGE", "Language", (const char*)u8"语言 (Language)");
        Register("VERSION_D_D_D", "Version %d.%d.%d", (const char*)u8"版本 %d.%d.%d");
        Register("THANKS_FOR_USING_THIS_CHEAT_S", "Thanks for using this cheat, %s!", (const char*)u8"感谢使用此辅助, %s!");
        Register("USERNAME_NOT_FOUND_BUT_THANKS_FOR_USING_ANYWAY", "Username not found, but thanks for using anyway!", (const char*)u8"未找到用户名，但仍感谢使用！");
        
        // 2. Tabs
        Register("TAB_ABOUT", "About", (const char*)u8"关于");
        Register("TAB_PLAYER", "Player", (const char*)u8"玩家");
        Register("TAB_WEAPON", "Weapon", (const char*)u8"武器选项");
        Register("TAB_WORLD", "World", (const char*)u8"世界");
        Register("TAB_MISC", "Misc", (const char*)u8"杂项");
        Register("TAB_CONFIG", "Config", (const char*)u8"配置");
        Register("TAB_HOTKEYS", "Hotkeys", (const char*)u8"热键设置");
        Register("CONFIG", "Config", (const char*)u8"配置");
        Register("WEAPON", "Weapon", (const char*)u8"武器选项");
 
        // 3. Overlay / Active Features
        Register("ACTIVE_FEATURES_LIST", "Enabled Features List", (const char*)u8"已启用项列表");
        Register("ACTIVE_FEATURES", "Active Features:", (const char*)u8"已开启功能:");
        Register("SPEED_X_1F", "Speed x%.1f", (const char*)u8"速度修改 x%.1f");
        Register("GAME_SPEED_X_F", "Game Speed: x%.1f", (const char*)u8"游戏速度: x%.1f");
        Register("INF_AMMO", "Infinite Ammo", (const char*)u8"无限弹药");
        Register("VOLATILE_HINT", "Note: Toggles like God Mode and Infinite Ammo must be manually enabled after injection for safety.", (const char*)u8"提示：无敌模式和无限弹药等切换类功能在重启后需手动开启。");
 
        // 4. Player Tab - Core Features
        Register("ESP", "ESP", (const char*)u8"透视 (ESP)");
        Register("AIMBOT", "Aimbot", (const char*)u8"自瞄 (Aimbot)");
        Register("SILENT_AIM", "Silent Aim", (const char*)u8"静默自瞄 (Silent Aim)");
        Register("GODMODE", "GodMode", (const char*)u8"无敌模式");
        Register("DEMIGOD", "Demigod", (const char*)u8"半神模式 (1血不倒)");
        Register("NO_TARGET", "No Target", (const char*)u8"无视玩家 (AI不攻击)");
        
        // 5. Movement Settings
        Register("MOVEMENT", "Movement Settings", (const char*)u8"运动/位移设置");
        Register("SPEED_HACK", "Speed Hack", (const char*)u8"移动加速");
        Register("SPEED_VALUE", "Speed Multiplier", (const char*)u8"速度倍率");
        Register("FLIGHT", "Flight Mode", (const char*)u8"飞行模式");
        Register("FLIGHT_SPEED", "Flight Speed", (const char*)u8"飞行速度");
        
        // 6. Camera / Third Person
        Register("THIRD_PERSON", "Third Person", (const char*)u8"第三人称");
        Register("OVER_SHOULDER", "Over-Shoulder Camera", (const char*)u8"越肩视角");
        Register("THIRD_PERSON_CENTERED", "Centered (Third Person)", (const char*)u8"居中 (第三人称)");
        Register("THIRD_PERSON_OTS", "Over The Shoulder (ADS)", (const char*)u8"越肩瞄准 (右键开镜)");
        Register("OTS_OFFSET_X", "OTS Offset X (Forward)", (const char*)u8"越肩偏移 X (前后)");
        Register("OTS_OFFSET_Y", "OTS Offset Y (Right)", (const char*)u8"越肩偏移 Y (左右)");
        Register("OTS_OFFSET_Z", "OTS Offset Z (Up)", (const char*)u8"越肩偏移 Z (上下)");
        Register("OTS_ADS_FOV_BOOST", "OTS ADS FOV Change", (const char*)u8"越肩开镜 FOV 更改");
        Register("OTS_ADS_FOV_SCALE", "OTS ADS FOV Multiplier", (const char*)u8"越肩开镜 FOV 倍率");
        Register("ADS_FIRST_PERSON", "ADS switch to First Person", (const char*)u8"右键开镜切换到第一人称");
        Register("FREE_CAM", "Free Camera", (const char*)u8"自由视角");
        Register("FREECAM_BLOCK_INPUT", "Block User Input", (const char*)u8"屏蔽用户输入 (自由视角)");
 
        // 7. Player Progression
        Register("PLAYER_PROGRESSION", "Player Progression:", (const char*)u8"玩家进度:");
        Register("EXPERIENCE_LEVEL", "Experience Level", (const char*)u8"经验等级");
        Register("SET_EXPERIENCE_LEVEL", "Set Experience Level", (const char*)u8"设置经验等级");
        Register("GIVE_5_LEVELS", "Give 5 Levels", (const char*)u8"增加5级");
        
        // 8. World Tab
        Register("WORLD_ACTIONS", "World Actions:", (const char*)u8"世界操作:");
        Register("KILL_ENEMIES", "Kill All Enemies", (const char*)u8"击杀所有敌人");
        Register("CLEAR_GROUND_ITEMS", "Clear Ground Items", (const char*)u8"清理地面物品");
        Register("TELEPORT_LOOT", "Teleport Loot to Me", (const char*)u8"战利品传送");
        Register("SPAWN_ITEMS", "Spawn Items", (const char*)u8"刷出物品");
        Register("PLAYERS_ONLY", "Players Only", (const char*)u8"世界冻结");
        Register("GAME_SPEED", "Game Speed", (const char*)u8"游戏速度");
        Register("MAP_TELEPORT", "Map Waypoint Teleport", (const char*)u8"地图标点传送 (右键标点再取消)");
        Register("MAP_TELEPORT_WINDOW", "Map TP Window (s)", (const char*)u8"地图传送延迟窗口 (秒)");
        Register("BLACK_MARKET_BYPASS", "Black Market Bypass", (const char*)u8"黑市查看冷却绕过");
        
        // 9. Currency Settings
        Register("CURRENCY_SETTINGS", "Currency Settings:", (const char*)u8"货币修改:");
        Register("CASH", "Cash", (const char*)u8"金钱");
        Register("ERIDIUM", "Eridium", (const char*)u8"镒矿");
        Register("VC_TICKETS", "Vault Card Tickets", (const char*)u8"卡片/奖牌");
        
        // 10. Aimbot Tab
        Register("STANDARD_AIMBOT_SETTINGS", "Standard Aimbot Settings", (const char*)u8"常规自瞄设置");
        Register("REQUIRE_LOS", "Require LOS", (const char*)u8"需要可见性 (LOS)");
        Register("DRAW_FOV", "Draw FOV", (const char*)u8"显示视野范围");
        Register("DRAW_ARROW", "Draw Arrow", (const char*)u8"显示指向箭头");
        Register("SMOOTH_AIM", "Smooth Aim", (const char*)u8"平滑自瞄");
        Register("SMOOTHING", "Smoothing", (const char*)u8"平滑系数");
        Register("AIMBOT_FOV", "Aimbot FOV", (const char*)u8"自瞄视野 (Aimbot FOV)");
        Register("MAX_DISTANCE", "Max Distance", (const char*)u8"最大距离");
        Register("TARGET_BONE", "Target Bone", (const char*)u8"目标骨骼");
        Register("CHOOSE_BONE_TOOLTIP", "Choose the bone to inject damage directly to. E.g Head = 100% Critical Hit", (const char*)u8"选择要直接注入伤害的部位。例如 头部 = 100% 暴击");
        
        // 11. Weapon Tab
        Register("INSTANT_HIT", "Instant Hit & Projectile Speed", (const char*)u8"瞬间命中 (投射物速度)");
        Register("PROJECTILE_SPEED", "Projectile Speed Multiplier", (const char*)u8"子弹飞行速度倍率");
        Register("RAPID_FIRE", "Rapid Fire", (const char*)u8"连射 (高射速)");
        Register("FIRE_RATE_MODIFIER", "Fire Rate Multiplier", (const char*)u8"射速倍率");
        Register("NO_RECOIL", "No Recoil", (const char*)u8"无后坐力");
        Register("RECOIL_REDUCTION", "Recoil Reduction %", (const char*)u8"后坐力减免比例");
        Register("NO_SWAY", "No Sway", (const char*)u8"无武器摇晃");
        Register("HOMING_PROJECTILES", "Homing Projectiles", (const char*)u8"自导子弹 (魔法子弹)");
        Register("HOMING_RANGE", "Homing Range", (const char*)u8"自导范围");
        Register("TRIGGERBOT", "TriggerBot", (const char*)u8"自动射击 (TriggerBot)");
        Register("REQUIRE_KEY_HELD", "Require Key Held", (const char*)u8"需要热键配合");
        Register("TARGET_ALL", "Target All", (const char*)u8"目标所有单位");
        Register("INSTANT_RELOAD", "Instant Reload", (const char*)u8"秒换弹 (瞬时装填装弹)");
        
        // 12. Misc Settings
        Register("MISC_SETTINGS", "Misc Settings", (const char*)u8"杂项设置");
        Register("ENABLE_FOV_CHANGER", "Enable FOV Changer", (const char*)u8"修改游戏视野");
        Register("FOV_VALUE", "FOV Value", (const char*)u8"视野角度");
        Register("ADS_ZOOM_SCALE", "ADS Zoom Scale", (const char*)u8"开镜放大倍率");
        Register("ENABLE_VIEWMODEL_FOV", "Enable ViewModel FOV", (const char*)u8"修改武器视角大小 (ViewModel)");
        Register("VIEWMODEL_FOV_VALUE", "ViewModel FOV Value", (const char*)u8"武器视角角度");
        Register("DISABLE_VOLUMETRIC_CLOUDS", "Disable Volumetric Clouds", (const char*)u8"禁用体积云 (提升帧率)");
        
        // 13. Config & Debug
        Register("ESP_SETTINGS", "ESP Settings", (const char*)u8"透视设置");
        Register("SHOW_BOX", "Show Box", (const char*)u8"显示方框");
        Register("SHOW_DISTANCE", "Show Distance", (const char*)u8"显示敌方距离");
        Register("SHOW_BONES", "Show Bones", (const char*)u8"显示骨骼");
        Register("SHOW_NAME", "Show Name", (const char*)u8"显示敌方名称");
        Register("ENEMY_COLOR", "Enemy Color", (const char*)u8"敌人颜色");
        Register("DEBUG", "Debug", (const char*)u8"调试 (Debug)");
        Register("ENABLE_EVENT_DEBUG_LOGS", "Enable Event Debug Logs", (const char*)u8"启用事件调试日志");
        Register("ENABLE_EVENT_RECORDING", "Enable Event Recording (Save to File)", (const char*)u8"启用事件记录 (保存到文件)");
        Register("SAVE_SETTINGS", "Save Settings", (const char*)u8"保存设置");
        Register("LOAD_SETTINGS", "Load Settings", (const char*)u8"加载设置");
        Register("SILENT_AIM_SETTINGS", "Silent Aim Settings", (const char*)u8"静默自瞄设置 (Silent Aim Settings)");
        
        // 14. Hotkeys
        Register("HOTKEY_TAB_HELP", "Click on a button to change the bound key. Press ESC to cancel.", (const char*)u8"点击按钮修改对应热键。按 ESC 取消。");
        Register("PRESS_ANY_KEY", "Press Any Key...", (const char*)u8"等待输入...");
        Register("UNKNOWN_KEY", "Unknown", (const char*)u8"未知按键");
        Register("RESET", "Reset", (const char*)u8"重置");
        Register("AIMBOT_KEY", "Aimbot Key", (const char*)u8"自瞄热键");
        Register("TRIGGER_KEY", "TriggerBot Key", (const char*)u8"自动射击热键");
        Register("MENU_KEY", "Menu Toggle Key", (const char*)u8"菜单切换按键");
        Register("UNHOOK_KEY", "Unhook Key", (const char*)u8"卸载取消 Hook 热键");
        Register("GODMODE_KEY", "God Mode Toggle Key", (const char*)u8"无敌模式切换热键");
        Register("INF_AMMO_KEY", "Infinite Ammo Toggle Key", (const char*)u8"无限弹药切换热键");
        Register("TOGGLE_THIRDPERSON_KEY", "Third Person Toggle Key", (const char*)u8"人称视角切换热键");
        Register("DUMP_OBJECTS_KEY", "Dump Objects Key", (const char*)u8"导出对象 key");
 
        Register("HOST_ONLY_IF_YOU_ARE_NOT_THE_HOST_THIS_FEATURE_WILL_NOT_FUNCTION", "Host Only Feature: This will not function if you are not the host.", (const char*)u8"仅限主机：如果你不是主机，此功能将失效。");
    }
}
