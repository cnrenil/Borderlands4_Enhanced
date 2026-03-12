#pragma once
#include <mutex>

inline bool AimbotKeyDown = false;

struct BulletTracer {
	std::vector<FVector> Points;
	float CreationTime;
	int32 Seed;    // Used to match hits from Server_HitscanHit
	bool bClosed;  // Whether we've already reached MaxPoints or finished
};

inline std::mutex TracerMutex;
inline std::vector<BulletTracer> BulletTracersList;

struct BoneListStruct
{
	std::string HeadBone = "Head";
	std::string NeckBone = "Neck";
	std::string ChestBone = "Spine3";
	std::string StomachBone = "Spine2";
	std::string PelvisBone = "Hips";
	std::string LeftShoulderBone = "L_Upperarm";
	std::string LeftElbowBone = "L_Forearm";
	std::string LeftHandBone = "L_Hand";
	std::string RightShoulderBone = "R_Upperarm";
	std::string RightElbowBone = "R_Forearm";
	std::string RightHandBone = "R_Hand";
	std::string LeftThighBone = "L_Thigh";
	std::string LeftShinBone = "L_Shin";
	std::string LeftFootBone = "L_Foot";
	std::string RightThighBone = "R_Thigh";
	std::string RightShinBone = "R_Shin";
	std::string RightFootBone = "R_Foot";
} inline BoneList;

struct EspSettingsstruct {
	bool ShowTeam = true;
	bool ShowBox = true;
	bool ShowEnemyDistance = true;
	bool ShowEnemyName = true;
	bool Bones = true;
	float BoneOpacity = 1.0f;
	ImVec4 EnemyColor = ImVec4(1.0f, 0.0f, 0.0f, BoneOpacity);
	ImVec4 TeamColor = ImVec4(0.0f, 1.0f, 0.0f, BoneOpacity);
	ImVec4 TargetColor = ImVec4(0.7f, 0.0f, 1.0f, BoneOpacity);
	bool LOS = false;
	bool BulletTracers = true;
	bool TracerRainbow = true;
	float TracerDuration = 2.0f;
	ImVec4 TracerColor = ImVec4(1.0f, 0.5f, 0.0f, 1.0f); // Orange-ish by default
} inline ESPSettings;

struct AimbotSettingsstruct {
	float MaxFOV = 15.0f;
	float MaxDistance = 100.0f;
	bool LOS = true;
	float MinDistance = 2.0f;
	bool Smooth = false;
	float SmoothingVector = 5.0f;
	bool DrawArrow = false;
	bool DrawFOV = false;
	bool RequireKeyHeld = true;
	ImGuiKey AimbotKey = ImGuiKey_MouseX2;
	float FOVThickness = 1.0f;
	bool ArrowThickness = 2.0f;
	bool TargetAll = false;
	bool UseMouseInput = true;
	float MouseSensitivity = 1.0f;
} inline AimbotSettings;

struct TriggerBotSettingsstruct {
	bool Enabled = false;
	bool RequireKeyHeld = true;
	ImGuiKey TriggerKey = ImGuiKey_MouseX1;
	bool TargetAll = false; // Trigger on friendlies too
} inline TriggerBotSettings;

struct WeaponSettingsstruct {
	bool InstantHitEnabled = false;
	float ProjectileSpeedMultiplier = 999.0f;
	bool RapidFireEnabled = false;
	bool NoRecoilEnabled = false;
	float RecoilReduction = 1.0f;
	bool NoSwayEnabled = false;
	bool HomingProjectiles = false;
	float HomingRange = 50.0f;
	float FireRate = 1.0f;
} inline WeaponSettings;

struct SilentAimSettingsstruct {
	float HitChance = 100.0f;
	bool RequiresLOS = false;
	bool DrawFOV = false;
	bool DrawArrow = false;
	float ArrowThickness = 2.0f;
	float FOVThickness = 1.0f;
	bool TargetAll = false;
	bool MagicBullet = false;
} inline SilentAimSettings;

struct CVarsstruct
{
	bool Debug = false;
	bool SecretFeatures = false;
	bool GodMode = false;
	bool InfAmmo = false;
	bool Aimbot = false;
	bool ESP = true;
	float Speed = 1;
	bool SpeedEnabled = false;
	bool SilentAim = false;
	bool Reticle = false;
	bool TriggerBot = false;
	bool RenderOptions = false;
	float FOV = 120.0f;
	bool ListPlayers = false;
	bool ShootFromReticle = false;
	bool SaveDebugToFile = false;
	bool BulletTime = false;
	bool NoTarget = false;
	bool PlayersOnly = false;
	float GameSpeed = 1.0f;
	bool Demigod = false;
	bool ThirdPerson = false;
	bool Freecam = false;
	bool FlightEnabled = false;
	float FlightSpeed = 1.0f;
} inline CVars;

#include "Utils/Localization.h"

struct MiscSettingsStruct {
	bool Reticle = false;
	ImVec4 ReticleColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
	float ReticleSize = 5.0f;
	ImVec2 ReticlePosition = ImVec2(0.0f, 0.0f);
	bool ReticleWhenThrowing = false;
	bool CrossReticle = true;
	bool EnableFOV = false;
	float FOV = 100.0f;
	bool EnableViewModelFOV = false;
	float ViewModelFOV = 90.0f;
	bool DisableVolumetricClouds = false;
	bool ShouldAutoSave = true;
	bool ShouldSaveCVars = true;
	bool MapTeleport = false;
	float MapTPWindow = 2.0f;
	bool ThirdPersonCentered = false;
	bool ThirdPersonOTS = true;
	bool ThirdPersonADSFirstPerson = true; 
	float OTS_X = -150.0f;
	float OTS_Y = 60.0f;
	float OTS_Z = 20.0f;
	bool NoBMCooldown = true;
	Language CurrentLanguage = Language::English;
} inline MiscSettings;

struct Settingsstruct
{
	bool ShouldSave = true;
	bool ShouldLoad = true;
} inline Settings;

struct TextVarsstruct
{
	std::string SilentAimBone = BoneList.HeadBone;
	std::string AimbotBone = BoneList.HeadBone;
	std::string DebugFunctionNameMustInclude = "";
	std::string DebugFunctionObjectMustInclude = "";
} inline TextVars;

struct Cheats
{
	static void ToggleGodMode();
	static void ToggleInfAmmo();
	static void Aimbot();
	static void AddAutoFire();
	static void RemoveRecoil();
	static void RemoveSpread();
	static void SetFireRate(float FireRate);
	static void PenetrateWalls();
	static void InstaKill();
	static void UpdateESP();
	static void RenderESP();
	static void SetPlayerSpeed();
	static void SilentAimHoming();
	static void AddMag();
	static void DrawReticle();
	static void TriggerBot();
	static void RenderEnabledOptions();
	static void ChangeFOV();
	static void Lean();
	static void AutoWin();
	static void ListPlayers();
	static void ChangeGameRenderSettings();
	static void GoTo(FVector Location);
	static void SetExperienceLevel(int32 xpAmount);
	static void WeaponModifiers();
	static void ToggleNoTarget();
	static void ClearGroundItems();
	static void SetGameSpeed(float Speed);
	static void AddCurrency(const std::string& type, int amount);
	static void KillEnemies();
	static void TogglePlayersOnly();
	static void GiveLevels();
	static void SpawnItems();
	static void CheckMapTeleport();
	static void ToggleDemigod();
	static void TeleportLoot();
	static void ToggleThirdPerson();
	static void ToggleFreecam();
	static void Flight();
	static void EnforcePersistence();
	static void InfiniteAmmo();
	static void BlackMarketBypass();
};