#include "pch.h"

static AActor* CurrentAimbotTarget = nullptr;

namespace
{
	static SDK::FVector g_SilentRedirectTargetPos{};
	static double g_LastSilentArmTime = 0.0;

	static std::atomic<bool> g_NativeProjectileHookInstalled{ false };
	static std::atomic<bool> g_NativeProjectileHookAttempted{ false };
	static double g_LastNativeHookAttemptTime = 0.0;
	static uintptr_t g_NativeProjectileHookAddress = 0;
	static std::mutex g_SilentHookInstallMutex;

	static std::atomic<bool> g_StartFireHookInstalled{ false };
	static std::atomic<bool> g_StartFireHookAttempted{ false };
	static double g_LastStartFireHookAttemptTime = 0.0;
	static uintptr_t g_StartFireHookAddress = 0;
	static constexpr size_t kStartFireVTableOffset = 0xBC8; // IDA: APlayerController::execStartFire -> call [vtable + 0xBC8]
	static constexpr size_t kStartFireVTableIndex = kStartFireVTableOffset / sizeof(void*);
	static constexpr size_t kPCWeaponBridgeOffset = 0x3D0;  // IDA: sub_147A9FAFE reads [PlayerController + 0x3D0]
	static constexpr size_t kWeaponFireVTableOffset = 0x660; // IDA: jmp qword ptr [vtable + 0x660]
	static constexpr size_t kWeaponFireVTableIndex = kWeaponFireVTableOffset / sizeof(void*);

	static std::atomic<bool> g_WeaponFireHookInstalled{ false };
	static std::atomic<bool> g_WeaponFireHookAttempted{ false };
	static double g_LastWeaponFireHookAttemptTime = 0.0;
	static uintptr_t g_WeaponFireHookAddress = 0;

	struct NativeVec3
	{
		double X;
		double Y;
		double Z;
	};

	using ProjectileContextBuildFn = __int64(__fastcall*)(
		__int64 a1,
		__int64 a2,
		unsigned int* a3,
		__int64 a4,
		__int64 a5,
		__int64 a6,
		__int64 a7,
		__int64 a8,
		__int64 a9,
		__int64 a10);

	using LightSpawnCommitFn = void(__fastcall*)(__int64 a1, __int64 a2);

	using StartFireFn = __int64(__fastcall*)(SDK::APlayerController* controller, uint8 fireModeNum);
	using WeaponFireBridgeFn = __int64(__fastcall*)(void* weaponBridge, unsigned int fireModeNum);

	static ProjectileContextBuildFn oProjectileContextBuild = nullptr;
	static LightSpawnCommitFn oLightSpawnCommit = nullptr;
	static StartFireFn oStartFire = nullptr;
	static WeaponFireBridgeFn oWeaponFireBridge = nullptr;
	static constexpr uintptr_t kProjectileContextBuildRVA = 0x15EDCCA; // IDA: sub_1415EDCCA
	static constexpr uintptr_t kLightSpawnCommitRVA = 0x115C2F0;       // IDA: sub_14115C2F0

	double NowSeconds()
	{
		return static_cast<double>(GetTickCount64()) * 0.001;
	}

	bool IsSilentArmed()
	{
		return ConfigManager::B("Aimbot.Enabled") &&
			ConfigManager::B("Aimbot.Silent") &&
			ConfigManager::B("Aimbot.NativeProjectileHook") &&
			CurrentAimbotTarget &&
			Utils::IsValidActor(CurrentAimbotTarget) &&
			(NowSeconds() - g_LastSilentArmTime) <= 0.8;
	}

	void LogSilentState(const char* where)
	{
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAimDbg",
			1000,
			"%s: enabled=%d silent=%d native=%d target=%p armAge=%.3f",
			where,
			ConfigManager::B("Aimbot.Enabled") ? 1 : 0,
			ConfigManager::B("Aimbot.Silent") ? 1 : 0,
			ConfigManager::B("Aimbot.NativeProjectileHook") ? 1 : 0,
			CurrentAimbotTarget,
			(NowSeconds() - g_LastSilentArmTime));
	}

	void DumpVTableWindow(void** vtable, size_t centerIndex, size_t radius, const char* tag)
	{
		if (!vtable) return;

		MEMORY_BASIC_INFORMATION mbi{};
		if (VirtualQuery(vtable, &mbi, sizeof(mbi)) == 0 ||
			mbi.State != MEM_COMMIT ||
			(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
		{
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAimDbg",
				1000,
				"%s vtable unreadable: vtable=%p",
				tag,
				vtable);
			return;
		}

		const size_t begin = (centerIndex > radius) ? (centerIndex - radius) : 0;
		const size_t end = centerIndex + radius;
		for (size_t i = begin; i <= end; ++i)
		{
			void* fn = vtable[i];

			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAimDbg",
				1000,
				"%s vtable[0x%zX] = %p",
				tag,
				i * sizeof(void*),
				fn);
		}
	}

	bool ReadVec3Param(const void* param, SDK::FVector& out)
	{
		if (!param) return false;
		__try
		{
			const NativeVec3* v = reinterpret_cast<const NativeVec3*>(param);
			if (!std::isfinite(v->X) || !std::isfinite(v->Y) || !std::isfinite(v->Z)) return false;
			out = SDK::FVector{ v->X, v->Y, v->Z };
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	void WriteVec3Param(void* param, const SDK::FVector& value)
	{
		if (!param) return;
		__try
		{
			NativeVec3* v = reinterpret_cast<NativeVec3*>(param);
			v->X = value.X;
			v->Y = value.Y;
			v->Z = value.Z;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	bool RedirectDirectionFromSource(const SDK::FVector& source, void* dirOut, const char* tag)
	{
		const SDK::FVector toTarget = g_SilentRedirectTargetPos - source;
		const float lenSq = static_cast<float>(toTarget.X * toTarget.X + toTarget.Y * toTarget.Y + toTarget.Z * toTarget.Z);
		if (lenSq <= 0.0001f) return false;

		const float len = sqrtf(lenSq);
		const SDK::FVector dir{ toTarget.X / len, toTarget.Y / len, toTarget.Z / len };
		WriteVec3Param(dirOut, dir);
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAim",
			90,
			"%s direction redirected. dir=(%.4f, %.4f, %.4f)",
			tag,
			dir.X, dir.Y, dir.Z);
		return true;
	}

	__int64 __fastcall hkProjectileContextBuild(
		__int64 a1,
		__int64 a2,
		unsigned int* a3,
		__int64 a4,
		__int64 a5,
		__int64 a6,
		__int64 a7,
		__int64 a8,
		__int64 a9,
		__int64 a10)
	{
		Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 600, "hkProjectileContextBuild hit");
		if (IsSilentArmed())
		{
			SDK::FVector source = (GVars.POV ? GVars.POV->Location : SDK::FVector{ 0.0, 0.0, 0.0 });
			ReadVec3Param(reinterpret_cast<void*>(a6), source);
			RedirectDirectionFromSource(source, reinterpret_cast<void*>(a7), "ProjectileContext");
		}
		return oProjectileContextBuild ? oProjectileContextBuild(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) : 0;
	}

	void __fastcall hkLightSpawnCommit(__int64 a1, __int64 a2)
	{
		Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 600, "hkLightSpawnCommit hit a2=0x%llX", static_cast<unsigned long long>(a2));
		if (IsSilentArmed() && a2)
		{
			// IDA sub_143D5B106 shows source at +0x20 and direction at +0x38 in the light spawn data blob.
			SDK::FVector source = (GVars.POV ? GVars.POV->Location : SDK::FVector{ 0.0, 0.0, 0.0 });
			ReadVec3Param(reinterpret_cast<void*>(a2 + 0x20), source);
			RedirectDirectionFromSource(source, reinterpret_cast<void*>(a2 + 0x38), "LightSpawnCommit");
		}
		if (oLightSpawnCommit)
		{
			oLightSpawnCommit(a1, a2);
		}
	}

	bool EnsureNativeProjectileHook()
	{
		std::lock_guard<std::mutex> guard(g_SilentHookInstallMutex);
		if (g_NativeProjectileHookInstalled.load()) return true;
		LogSilentState("EnsureNativeProjectileHook.enter");

		const double now = NowSeconds();
		if (g_NativeProjectileHookAttempted.load() && (now - g_LastNativeHookAttemptTime) < 2.0)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 800, "EnsureNativeProjectileHook throttled");
			return false;
		}

		g_NativeProjectileHookAttempted.store(true);
		g_LastNativeHookAttemptTime = now;

		const uintptr_t imageBase = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));
		if (!imageBase)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 1000, "EnsureNativeProjectileHook: imageBase null");
			return false;
		}

		const uintptr_t projectileContextTarget = imageBase + kProjectileContextBuildRVA;
		const uintptr_t lightCommitTarget = imageBase + kLightSpawnCommitRVA;

		MH_STATUS createStatus = MH_CreateHook(
			reinterpret_cast<LPVOID>(projectileContextTarget),
			&hkProjectileContextBuild,
			reinterpret_cast<LPVOID*>(&oProjectileContextBuild));
		if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		{
			Logger::LogThrottled(
				Logger::Level::Error,
				"SilentAim",
				2000,
				"ProjectileContext hook create failed. status=%d(%s) addr=0x%llX",
				static_cast<int>(createStatus),
				MH_StatusToString(createStatus),
				static_cast<unsigned long long>(projectileContextTarget));
			return false;
		}

		createStatus = MH_CreateHook(
			reinterpret_cast<LPVOID>(lightCommitTarget),
			&hkLightSpawnCommit,
			reinterpret_cast<LPVOID*>(&oLightSpawnCommit));
		if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		{
			Logger::LogThrottled(
				Logger::Level::Error,
				"SilentAim",
				2000,
				"LightSpawnCommit hook create failed. status=%d(%s) addr=0x%llX",
				static_cast<int>(createStatus),
				MH_StatusToString(createStatus),
				static_cast<unsigned long long>(lightCommitTarget));
			return false;
		}

		MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(projectileContextTarget));
		if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
		{
			Logger::LogThrottled(
				Logger::Level::Error,
				"SilentAim",
				2000,
				"ProjectileContext hook enable failed. status=%d(%s) addr=0x%llX",
				static_cast<int>(enableStatus),
				MH_StatusToString(enableStatus),
				static_cast<unsigned long long>(projectileContextTarget));
			return false;
		}

		enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(lightCommitTarget));
		if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
		{
			Logger::LogThrottled(
				Logger::Level::Error,
				"SilentAim",
				2000,
				"LightSpawnCommit hook enable failed. status=%d(%s) addr=0x%llX",
				static_cast<int>(enableStatus),
				MH_StatusToString(enableStatus),
				static_cast<unsigned long long>(lightCommitTarget));
			return false;
		}

		g_NativeProjectileHookAddress = projectileContextTarget;
		g_NativeProjectileHookInstalled.store(true);
		Logger::LogThrottled(
			Logger::Level::Info,
			"SilentAim",
			10000,
			"Native fire-path hooks installed. ProjectileContext=0x%llX LightCommit=0x%llX",
			static_cast<unsigned long long>(projectileContextTarget),
			static_cast<unsigned long long>(lightCommitTarget));
		return true;
	}

	__int64 __fastcall hkStartFire(SDK::APlayerController* controller, uint8 fireModeNum)
	{
		if (controller == GVars.PlayerController &&
			ConfigManager::B("Aimbot.Enabled") &&
			ConfigManager::B("Aimbot.Silent") &&
			CurrentAimbotTarget &&
			Utils::IsValidActor(CurrentAimbotTarget))
		{
			g_SilentRedirectTargetPos = Cheats::AimbotTargetPos;
			g_LastSilentArmTime = NowSeconds();
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAim",
				120,
				"StartFire hook armed. mode=%u target=%s",
				static_cast<unsigned>(fireModeNum),
				CurrentAimbotTarget->GetName().c_str());
		}

		return oStartFire ? oStartFire(controller, fireModeNum) : 0;
	}

	__int64 __fastcall hkWeaponFireBridge(void* weaponBridge, unsigned int fireModeNum)
	{
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAimDbg",
			200,
			"hkWeaponFireBridge hit. bridge=%p mode=%u",
			weaponBridge,
			static_cast<unsigned>(fireModeNum));

		if (ConfigManager::B("Aimbot.Enabled") &&
			ConfigManager::B("Aimbot.Silent") &&
			CurrentAimbotTarget &&
			Utils::IsValidActor(CurrentAimbotTarget))
		{
			g_SilentRedirectTargetPos = Cheats::AimbotTargetPos;
			g_LastSilentArmTime = NowSeconds();
		}

		return oWeaponFireBridge ? oWeaponFireBridge(weaponBridge, fireModeNum) : 0;
	}

	bool EnsureStartFireHook()
	{
		std::lock_guard<std::mutex> guard(g_SilentHookInstallMutex);
		if (g_StartFireHookInstalled.load()) return true;
		if (!GVars.PlayerController)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 1000, "EnsureStartFireHook: PlayerController null");
			return false;
		}

		const double now = NowSeconds();
		if (g_StartFireHookAttempted.load() && (now - g_LastStartFireHookAttemptTime) < 1.0)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 600, "EnsureStartFireHook throttled");
			return false;
		}

		g_StartFireHookAttempted.store(true);
		g_LastStartFireHookAttemptTime = now;

		void*** thisAsVTable = reinterpret_cast<void***>(GVars.PlayerController);
		if (!thisAsVTable || !*thisAsVTable)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 1000, "EnsureStartFireHook: vtable unavailable");
			return false;
		}

		void** vtable = *thisAsVTable;
		void* target = vtable[kStartFireVTableIndex];
		if (!target)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 1000, "EnsureStartFireHook: startfire slot null");
			return false;
		}

		MH_STATUS createStatus = MH_CreateHook(target, &hkStartFire, reinterpret_cast<LPVOID*>(&oStartFire));
		if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		{
			Logger::LogThrottled(
				Logger::Level::Error,
				"SilentAim",
				2000,
				"StartFire hook create failed. status=%d(%s) addr=0x%llX",
				static_cast<int>(createStatus),
				MH_StatusToString(createStatus),
				static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)));
			return false;
		}

		MH_STATUS enableStatus = MH_EnableHook(target);
		if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
		{
			Logger::LogThrottled(
				Logger::Level::Error,
				"SilentAim",
				2000,
				"StartFire hook enable failed. status=%d(%s) addr=0x%llX",
				static_cast<int>(enableStatus),
				MH_StatusToString(enableStatus),
				static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)));
			return false;
		}

		g_StartFireHookAddress = reinterpret_cast<uintptr_t>(target);
		g_StartFireHookInstalled.store(true);
		Logger::LogThrottled(
			Logger::Level::Info,
			"SilentAim",
			10000,
			"StartFire hook installed at 0x%llX (vtable+0x%zX)",
			static_cast<unsigned long long>(g_StartFireHookAddress),
			kStartFireVTableOffset);
		return true;
	}

	bool EnsureWeaponFireHook()
	{
		std::lock_guard<std::mutex> guard(g_SilentHookInstallMutex);
		if (!GVars.PlayerController)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 1000, "EnsureWeaponFireHook: PlayerController null");
			return false;
		}

		const double now = NowSeconds();
		if (g_WeaponFireHookAttempted.load() && (now - g_LastWeaponFireHookAttemptTime) < 1.0)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 600, "EnsureWeaponFireHook throttled");
			return false;
		}

		void* weaponBridge = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(GVars.PlayerController) + kPCWeaponBridgeOffset);
		if (!weaponBridge)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 1000, "EnsureWeaponFireHook: weapon bridge null (+0x%zX)", kPCWeaponBridgeOffset);
			return false;
		}
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAimDbg",
			1000,
			"EnsureWeaponFireHook: weaponBridge=%p (PC=%p)",
			weaponBridge,
			GVars.PlayerController);

		void*** bridgeAsVTable = reinterpret_cast<void***>(weaponBridge);
		if (!bridgeAsVTable || !*bridgeAsVTable)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 1000, "EnsureWeaponFireHook: bridge vtable unavailable");
			return false;
		}

		void** bridgeVTable = *bridgeAsVTable;
		DumpVTableWindow(bridgeVTable, kWeaponFireVTableIndex, 4, "WeaponBridge");
		void* target = bridgeVTable[kWeaponFireVTableIndex];
		if (!target)
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAimDbg", 1000, "EnsureWeaponFireHook: bridge slot null (+0x%zX)", kWeaponFireVTableOffset);
			DumpVTableWindow(bridgeVTable, kWeaponFireVTableIndex, 8, "WeaponBridgeNullSlot");
			return false;
		}

		if (g_WeaponFireHookInstalled.load() && g_WeaponFireHookAddress == reinterpret_cast<uintptr_t>(target))
		{
			return true;
		}

		g_WeaponFireHookAttempted.store(true);
		g_LastWeaponFireHookAttemptTime = now;

		MH_STATUS createStatus = MH_CreateHook(target, &hkWeaponFireBridge, reinterpret_cast<LPVOID*>(&oWeaponFireBridge));
		if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		{
			Logger::LogThrottled(
				Logger::Level::Error,
				"SilentAim",
				2000,
				"WeaponFire bridge hook create failed. status=%d(%s) addr=0x%llX",
				static_cast<int>(createStatus),
				MH_StatusToString(createStatus),
				static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)));
			DumpVTableWindow(bridgeVTable, kWeaponFireVTableIndex, 8, "WeaponBridgeHookCreateFail");
			return false;
		}

		MH_STATUS enableStatus = MH_EnableHook(target);
		if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
		{
			Logger::LogThrottled(
				Logger::Level::Error,
				"SilentAim",
				2000,
				"WeaponFire bridge hook enable failed. status=%d(%s) addr=0x%llX",
				static_cast<int>(enableStatus),
				MH_StatusToString(enableStatus),
				static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)));
			return false;
		}

		g_WeaponFireHookAddress = reinterpret_cast<uintptr_t>(target);
		g_WeaponFireHookInstalled.store(true);
		Logger::LogThrottled(
			Logger::Level::Info,
			"SilentAim",
			8000,
			"WeaponFire bridge hook installed at 0x%llX (PC+0x%zX -> vtable+0x%zX)",
			static_cast<unsigned long long>(g_WeaponFireHookAddress),
			kPCWeaponBridgeOffset,
			kWeaponFireVTableOffset);
		return true;
	}
}

void Cheats::Aimbot()
{
	bHasAimbotTarget = false;
	CurrentAimbotTarget = nullptr;

	if (!ConfigManager::B("Aimbot.Enabled") || !Utils::bIsInGame) return;
	if (!GVars.POV || !GVars.PlayerController || !Utils::GetSelfActor()) return;

	CurrentAimbotTarget = Utils::GetBestTarget(
		GVars.PlayerController,
		ConfigManager::F("Aimbot.MaxFOV"),
		ConfigManager::B("Aimbot.LOS"),
		ConfigManager::S("Aimbot.Bone"),
		ConfigManager::B("Aimbot.TargetAll")
	);

	if (CurrentAimbotTarget && CurrentAimbotTarget->IsA(ACharacter::StaticClass()))
	{
		ACharacter* TargetChar = reinterpret_cast<ACharacter*>(CurrentAimbotTarget);

		static std::string CachedBoneString = "";
		static FName CachedBoneName;
		if (CachedBoneString != ConfigManager::S("Aimbot.Bone"))
		{
			std::wstring WideString = UtfN::StringToWString(ConfigManager::S("Aimbot.Bone"));
			CachedBoneName = UKismetStringLibrary::Conv_StringToName(WideString.c_str());
			CachedBoneString = ConfigManager::S("Aimbot.Bone");
		}

		FVector TargetPos;
		if (TargetChar->Mesh && TargetChar->Mesh->GetBoneIndex(CachedBoneName) != -1)
			TargetPos = TargetChar->Mesh->GetBoneTransform(CachedBoneName, ERelativeTransformSpace::RTS_World).Translation;
		else
			TargetPos = Utils::GetHighestBone(TargetChar);

		bHasAimbotTarget = true;
		AimbotTargetPos = TargetPos;
	}
}

void Cheats::AimbotHotkey()
{
	if (!Utils::bIsInGame || !GVars.PlayerController || !GVars.POV)
	{
		g_LastSilentArmTime = 0.0;
		return;
	}

	FVector CameraPos = GVars.POV->Location;
	FVector TargetPos = AimbotTargetPos;

	if (ConfigManager::B("Aimbot.Silent"))
	{
		EnsureStartFireHook();
		EnsureWeaponFireHook();
		if (ConfigManager::B("Aimbot.NativeProjectileHook"))
		{
			EnsureNativeProjectileHook();
		}

		if (!CurrentAimbotTarget)
		{
			const bool bLmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
			if (!bLmbDown || (NowSeconds() - g_LastSilentArmTime) > 0.6)
			{
				g_LastSilentArmTime = 0.0;
			}
			return;
		}

		g_SilentRedirectTargetPos = TargetPos;
		g_LastSilentArmTime = NowSeconds();

		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAim",
			250,
			"Silent redirect armed. target=(%.1f, %.1f, %.1f)",
			TargetPos.X, TargetPos.Y, TargetPos.Z);
		return;
	}

	if (!CurrentAimbotTarget) return;
	g_LastSilentArmTime = 0.0;

	FRotator DesiredRot = Utils::GetRotationToTarget(CameraPos, TargetPos);
	FRotator CurrentRot = GVars.PlayerController->ControlRotation;

	if (ConfigManager::B("Aimbot.Smooth"))
	{
		float SmoothFactor = (ConfigManager::F("Aimbot.SmoothingVector") <= 1.0f) ? 1.0f : ConfigManager::F("Aimbot.SmoothingVector");
		FRotator Delta = DesiredRot - CurrentRot;
		Delta.Normalize();

		FRotator SmoothedRot = CurrentRot + (Delta / SmoothFactor);
		SmoothedRot.Normalize();
		GVars.PlayerController->ClientSetRotation(SmoothedRot, true);
	}
	else
	{
		GVars.PlayerController->ClientSetRotation(DesiredRot, true);
	}
}

bool Cheats::HandleAimbotEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
	(void)Object;
	(void)Function;
	(void)Params;
	return false;
}

void Cheats::HandleConstructedObject(const SDK::UObject* Object)
{
	(void)Object;
}
