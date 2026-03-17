#pragma once
#include <mutex>
#include <string>
#include <vector>

namespace SilentAimHooks
{
	void UpdateTarget(AActor* target, const FVector& targetPos);
	void Tick();
	void OnAimbotHotkey();
	void ResetArm();
}

namespace CheatsData
{
	struct BulletTracer
	{
		std::vector<FVector> Points;
		float CreationTime;
		int32 Seed;    // Used to match hits from Server_HitscanHit
		bool bClosed;  // Whether we've already reached MaxPoints or finished
	};

	inline std::mutex TracerMutex;
	inline std::vector<BulletTracer> BulletTracers;

	struct BoneListConfig
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
	};

	inline BoneListConfig BoneList;
}

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
	static void AddMag();
	static void DrawReticle();
	static void TriggerBot();
	static void RenderEnabledOptions();
	static void Render();
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
	static void DumpObjects();

	// Hotkey Callbacks
	static void AimbotHotkey();
	static void TriggerHotkey();
	
	// Modular ProcessEvent Handlers
	static void HandleDebugEvents(
		const UObject* Object,
		UFunction* Function,
		void* Params,
		void(*OriginalProcessEvent)(const UObject*, UFunction*, void*),
		bool bCallOriginal);
	static bool HandleMovementEvents(const UObject* Object, UFunction* Function, void* Params);
	static bool HandleAimbotEvents(const UObject* Object, UFunction* Function, void* Params);
	static void HandleConstructedObject(const UObject* Object);
	static bool HandleWeaponEvents(const UObject* Object, UFunction* Function, void* Params);
	static bool HandleCameraEvents(const UObject* Object, UFunction* Function, void* Params);
	
	// Modular Update Handlers
	static void UpdateMovement();
	static void UpdateWeapon();
	static void UpdateCamera();
	static void UpdateDebug();
	static void ShutdownCamera();

	// Cross-thread Rendering Helpers
	static inline FVector AimbotTargetPos;
	static inline bool bHasAimbotTarget = false;
	static inline std::atomic<bool> bTriggerSuppressMouseInput = false;

};
