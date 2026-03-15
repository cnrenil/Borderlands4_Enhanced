#include "pch.h"
#include <intrin.h>

extern std::atomic<int> g_PresentCount;

namespace SilentAimHooks
{
	static SDK::AActor* g_CurrentAimbotTarget = nullptr;
	static SDK::FVector g_LatestTargetPos{};

	namespace
	{
		static std::atomic<int> g_LastAimbotHotkeyFrame{ -1 };
		static SDK::FVector g_SilentRedirectTargetPos{};
		static double g_LastSilentArmTime = 0.0;
		static std::mutex g_HookInstallMutex;

		static std::atomic<bool> g_NativeProjectileHookInstalled{ false };
		static std::atomic<bool> g_NativeProjectileHookAttempted{ false };
		static std::atomic<bool> g_NativeProjectileHookBlocked{ false };
		static double g_LastNativeHookAttemptTime = 0.0;

		static std::atomic<bool> g_WeaponBehaviorFireHookInstalled{ false };
		static std::atomic<bool> g_WeaponBehaviorFireHookAttempted{ false };
		static double g_LastWeaponBehaviorFireHookAttemptTime = 0.0;

		using ProjectileBuildRequestFn = __int64(__fastcall*)(
			__int64 outRequest,
			__int64 ownerOrContext,
			__int64* fireData,
			__int64 instigatorOrSource,
			__int64 fireContext,
			void* sourceParam,
			void* dirParam,
			int projectileFlags,
			__int64 extraContext,
			unsigned __int8 requirePawnSource);

		using WeaponBehaviorFireProjectileFn = __int64(__fastcall*)(void* behaviorThis);

		static ProjectileBuildRequestFn oProjectileBuildRequest = nullptr;
		static WeaponBehaviorFireProjectileFn oWeaponBehaviorFireProjectile = nullptr;

		// Resolve the shared projectile request builder by AOB. The two script-native projectile wrappers both funnel into
		// this helper, so the native side only depends on a single pattern.
		// IDA references (for the current build this was derived from):
		// - ProjectileBuilder:      sub_1414903B4
		// - ProjectileBuilderConst: sub_1459DFD6E
		// - ProjectileBuildRequest: sub_1414906D7
		// - WeaponBehavior slot 130 target: sub_1441789C2
		static constexpr const char* kProjectileBuildRequestAob =
			"41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ? "
			"0F 29 B4 24 ? ? ? ? "
			"48 89 CE 48 8B 05 ? ? ? ? "
			"48 31 E0 48 89 84 24 ? ? ? ? "
			"48 85 D2 74";
		static constexpr size_t kProjectileBuildRequestHookLen = 19;
		static constexpr size_t kWeaponBehaviorFireProjectileSlot = 130;

		static constexpr double kArmWindowSeconds = 0.8;
		static constexpr double kLocalSourceMaxDist = 5000.0;

		static uintptr_t g_ProjectileBuildRequestAddr = 0;

		double NowSeconds()
		{
			return static_cast<double>(GetTickCount64()) * 0.001;
		}

		bool IsAimbotActivationAllowed()
		{
			if (!ConfigManager::B("Aimbot.RequireKeyHeld")) return true;
			return g_LastAimbotHotkeyFrame.load() == g_PresentCount.load();
		}

		bool IsSilentArmed()
		{
			return ConfigManager::B("Aimbot.Enabled") &&
				ConfigManager::B("Aimbot.Silent") &&
				ConfigManager::B("Aimbot.NativeProjectileHook") &&
				IsAimbotActivationAllowed() &&
				g_CurrentAimbotTarget &&
				Utils::IsValidActor(g_CurrentAimbotTarget) &&
				(NowSeconds() - g_LastSilentArmTime) <= kArmWindowSeconds;
		}

		void ArmSilentRedirect()
		{
			if (!ConfigManager::B("Aimbot.Enabled") ||
				!ConfigManager::B("Aimbot.Silent") ||
				!IsAimbotActivationAllowed() ||
				!g_CurrentAimbotTarget ||
				!Utils::IsValidActor(g_CurrentAimbotTarget))
			{
				return;
			}

			g_SilentRedirectTargetPos = g_LatestTargetPos;
			g_LastSilentArmTime = NowSeconds();
		}

		bool IsLikelyLocalFireSource(const SDK::FVector& source)
		{
			if (!GVars.POV) return false;
			const SDK::FVector cam = GVars.POV->Location;
			const double dx = source.X - cam.X;
			const double dy = source.Y - cam.Y;
			const double dz = source.Z - cam.Z;
			return (dx * dx + dy * dy + dz * dz) <= (kLocalSourceMaxDist * kLocalSourceMaxDist);
		}

		__int64 __fastcall hkProjectileBuildRequest(
			__int64 outRequest,
			__int64 ownerOrContext,
			__int64* fireData,
			__int64 instigatorOrSource,
			__int64 fireContext,
			void* sourceParam,
			void* dirParam,
			int projectileFlags,
			__int64 extraContext,
			unsigned __int8 requirePawnSource)
		{
			SDK::FVector source = (GVars.POV ? GVars.POV->Location : SDK::FVector{});
			SDK::FVector incomingDir{};
			const bool sourceFromParam = sourceParam && NativeInterop::ReadVec3Param(sourceParam, source);
			const bool dirFromParam = dirParam && NativeInterop::ReadVec3Param(dirParam, incomingDir);
			const bool armed = IsSilentArmed();
			const bool likelyLocal = armed && IsLikelyLocalFireSource(source);
			void* caller = _ReturnAddress();

			Logger::Log(
				Logger::Level::Debug,
				"SilentAimDbg",
				"ProjectileBuilder_BuildRequest hit. caller=%p out=%p owner=%p fireData=%p a4=%p a5=%p sourceOk=%d source=(%.4f, %.4f, %.4f) dirOk=%d dir=(%.4f, %.4f, %.4f) flags=%d extra=%p requirePawn=%u armed=%d local=%d",
				caller,
				reinterpret_cast<void*>(outRequest),
				reinterpret_cast<void*>(ownerOrContext),
				fireData,
				reinterpret_cast<void*>(instigatorOrSource),
				reinterpret_cast<void*>(fireContext),
				sourceFromParam ? 1 : 0,
				source.X, source.Y, source.Z,
				dirFromParam ? 1 : 0,
				incomingDir.X, incomingDir.Y, incomingDir.Z,
				projectileFlags,
				reinterpret_cast<void*>(extraContext),
				static_cast<unsigned int>(requirePawnSource),
				armed ? 1 : 0,
				likelyLocal ? 1 : 0);

			if (likelyLocal && dirParam)
			{
				SDK::FVector redirectedDir{};
				if (NativeInterop::RedirectDirectionFromSource(source, g_SilentRedirectTargetPos, dirParam, &redirectedDir))
				{
					Logger::LogThrottled(
						Logger::Level::Debug,
						"SilentAim",
						120,
						"%s redirected. dir=(%.4f, %.4f, %.4f)",
						"ProjectileBuildRequest",
						redirectedDir.X, redirectedDir.Y, redirectedDir.Z);
					Logger::Log(
						Logger::Level::Debug,
						"SilentAimDbg",
						"ProjectileBuilder_BuildRequest redirected. caller=%p target=(%.4f, %.4f, %.4f) newDir=(%.4f, %.4f, %.4f)",
						caller,
						g_SilentRedirectTargetPos.X, g_SilentRedirectTargetPos.Y, g_SilentRedirectTargetPos.Z,
						redirectedDir.X, redirectedDir.Y, redirectedDir.Z);
				}
			}

			const __int64 result = oProjectileBuildRequest
				? oProjectileBuildRequest(
					outRequest,
					ownerOrContext,
					fireData,
					instigatorOrSource,
					fireContext,
					sourceParam,
					dirParam,
					projectileFlags,
					extraContext,
					requirePawnSource)
				: 0;

			Logger::Log(
				Logger::Level::Debug,
				"SilentAimDbg",
				"ProjectileBuilder_BuildRequest return. caller=%p result=%p out=%p",
				caller,
				reinterpret_cast<void*>(result),
				reinterpret_cast<void*>(outRequest));
			return result;
		}

		__int64 __fastcall hkWeaponBehaviorFireProjectile(void* behaviorThis)
		{
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAimDbg",
				300,
				"hkWeaponBehaviorFireProjectile hit. this=%p",
				behaviorThis);

			ArmSilentRedirect();
			return oWeaponBehaviorFireProjectile ? oWeaponBehaviorFireProjectile(behaviorThis) : 0;
		}

		static uintptr_t ResolveExecTarget(const char* tag, const char* aob, uintptr_t& cache)
		{
			if (cache) return cache;

			const uintptr_t addr = Memory::FindPattern(aob);
			if (!addr)
			{
				Logger::LogThrottled(
					Logger::Level::Error,
					"SilentAim",
					300,
					"%s pattern not found.",
					tag ? tag : "Unknown");
				return 0;
			}

			cache = addr;
			Logger::Log(
				Logger::Level::Info,
				"SilentAim",
				"%s resolved at 0x%llX",
				tag ? tag : "Unknown",
				static_cast<unsigned long long>(addr));
			return addr;
		}

		static bool GetWeaponBehaviorFireProjectileTarget(void** targetOut)
		{
			if (!targetOut) return false;

			SDK::UWeaponBehavior_FireProjectile* def = SDK::UWeaponBehavior_FireProjectile::GetDefaultObj();
			if (!def) return false;

			void*** asVt = reinterpret_cast<void***>(def);
			if (!asVt || !*asVt) return false;
			void** vtable = *asVt;
			void* target = vtable[kWeaponBehaviorFireProjectileSlot];
			if (!target)
			{
				Logger::LogThrottled(
					Logger::Level::Error,
					"SilentAim",
					300,
					"WeaponBehavior_FireProjectile vtable slot is null. slot=%zu",
					kWeaponBehaviorFireProjectileSlot);
				return false;
			}

			*targetOut = target;
			return true;
		}

		static bool PatchWeaponBehaviorFireProjectileVTable(void* detour, void** originalOut)
		{
			if (!detour || !originalOut) return false;

			SDK::UWeaponBehavior_FireProjectile* def = SDK::UWeaponBehavior_FireProjectile::GetDefaultObj();
			if (!def) return false;

			void* target = nullptr;
			if (!GetWeaponBehaviorFireProjectileTarget(&target)) return false;

			if (!Memory::PatchVTableSlot(def, kWeaponBehaviorFireProjectileSlot, detour, originalOut))
			{
				Logger::LogThrottled(
					Logger::Level::Error,
					"SilentAim",
					300,
					"WeaponBehavior_FireProjectile vtable patch failed. slot=%zu",
					kWeaponBehaviorFireProjectileSlot);
				return false;
			}

			Logger::Log(
				Logger::Level::Info,
				"SilentAim",
				"WeaponBehavior_FireProjectile vtable patched. slot=%zu target=%p",
				kWeaponBehaviorFireProjectileSlot,
				target);
			return true;
		}

		bool EnsureNativeProjectileHook()
		{
			std::lock_guard<std::mutex> guard(g_HookInstallMutex);
			if (g_NativeProjectileHookInstalled.load()) return true;
			if (g_NativeProjectileHookBlocked.load()) return false;

			const double now = NowSeconds();
			if (g_NativeProjectileHookAttempted.load() && (now - g_LastNativeHookAttemptTime) < 2.0)
			{
				return false;
			}
			g_NativeProjectileHookAttempted.store(true);
			g_LastNativeHookAttemptTime = now;

			const uintptr_t projectileBuildRequestTarget =
				ResolveExecTarget("ProjectileBuildRequest", kProjectileBuildRequestAob, g_ProjectileBuildRequestAddr);
			if (!projectileBuildRequestTarget) return false;

			MH_STATUS createStatus = MH_CreateHook(
				reinterpret_cast<LPVOID>(projectileBuildRequestTarget),
				&hkProjectileBuildRequest,
				reinterpret_cast<LPVOID*>(&oProjectileBuildRequest));
			if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
			{
				if (createStatus == MH_ERROR_MEMORY_ALLOC)
				{
					if (Memory::HookFunctionAbsolute(
						reinterpret_cast<void*>(projectileBuildRequestTarget),
						reinterpret_cast<void*>(&hkProjectileBuildRequest),
						reinterpret_cast<void**>(&oProjectileBuildRequest),
						kProjectileBuildRequestHookLen))
					{
						g_NativeProjectileHookInstalled.store(true);
						Logger::Log(
							Logger::Level::Info,
							"SilentAim",
							"ProjectileBuildRequest hook installed via absolute detour fallback. addr=0x%llX len=%zu",
							static_cast<unsigned long long>(projectileBuildRequestTarget),
							kProjectileBuildRequestHookLen);
						return true;
					}

					g_NativeProjectileHookBlocked.store(true);
					Logger::Log(
						Logger::Level::Error,
						"SilentAim",
						"ProjectileBuildRequest hook blocked after MEMORY_ALLOC and fallback failed. addr=0x%llX",
						static_cast<unsigned long long>(projectileBuildRequestTarget));
					return false;
				}
				Logger::Log(
					Logger::Level::Error,
					"SilentAim",
					"ProjectileBuildRequest hook create failed. status=%d(%s) addr=0x%llX",
					static_cast<int>(createStatus),
					MH_StatusToString(createStatus),
					static_cast<unsigned long long>(projectileBuildRequestTarget));
				return false;
			}

			MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(projectileBuildRequestTarget));
			if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
			{
				Logger::Log(
					Logger::Level::Error,
					"SilentAim",
					"ProjectileBuildRequest hook enable failed. status=%d(%s) addr=0x%llX",
					static_cast<int>(enableStatus),
					MH_StatusToString(enableStatus),
					static_cast<unsigned long long>(projectileBuildRequestTarget));
				return false;
			}

			g_NativeProjectileHookInstalled.store(true);
			Logger::Log(
				Logger::Level::Info,
				"SilentAim",
				"Native projectile hook installed. BuildRequest=0x%llX",
				static_cast<unsigned long long>(projectileBuildRequestTarget));
			return true;
		}

		bool EnsureWeaponBehaviorFireProjectileHook()
		{
			std::lock_guard<std::mutex> guard(g_HookInstallMutex);
			if (g_WeaponBehaviorFireHookInstalled.load()) return true;

			const double now = NowSeconds();
			if (g_WeaponBehaviorFireHookAttempted.load() && (now - g_LastWeaponBehaviorFireHookAttemptTime) < 1.0)
			{
				return false;
			}
			g_WeaponBehaviorFireHookAttempted.store(true);
			g_LastWeaponBehaviorFireHookAttemptTime = now;

			void* target = nullptr;
			if (!GetWeaponBehaviorFireProjectileTarget(&target)) return false;

			MH_STATUS createStatus = MH_CreateHook(target, &hkWeaponBehaviorFireProjectile, reinterpret_cast<LPVOID*>(&oWeaponBehaviorFireProjectile));
			if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
			{
				if (createStatus == MH_ERROR_MEMORY_ALLOC)
				{
					// MinHook allocation can fail under some anti-tamper states; fall back to a vtable patch.
					if (PatchWeaponBehaviorFireProjectileVTable(
						reinterpret_cast<void*>(&hkWeaponBehaviorFireProjectile),
						reinterpret_cast<void**>(&oWeaponBehaviorFireProjectile)))
					{
						g_WeaponBehaviorFireHookInstalled.store(true);
						return true;
					}
				}

				Logger::Log(
					Logger::Level::Error,
					"SilentAim",
					"WeaponBehavior_FireProjectile hook create failed. status=%d(%s) addr=0x%llX",
					static_cast<int>(createStatus),
					MH_StatusToString(createStatus),
					static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)));
				return false;
			}

			MH_STATUS enableStatus = MH_EnableHook(target);
			if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
			{
				Logger::Log(
					Logger::Level::Error,
					"SilentAim",
					"WeaponBehavior_FireProjectile hook enable failed. status=%d(%s) addr=0x%llX",
					static_cast<int>(enableStatus),
					MH_StatusToString(enableStatus),
					static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)));
				return false;
			}

			g_WeaponBehaviorFireHookInstalled.store(true);
			Logger::Log(
				Logger::Level::Info,
				"SilentAim",
				"WeaponBehavior_FireProjectile hook installed. target=0x%llX slot=%zu",
				static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)),
				kWeaponBehaviorFireProjectileSlot);
			return true;
		}
	}

	void UpdateTarget(SDK::AActor* target, const SDK::FVector& targetPos)
	{
		g_CurrentAimbotTarget = target;
		g_LatestTargetPos = targetPos;
	}

	void Tick()
	{
		if (!ConfigManager::B("Aimbot.Enabled") || !ConfigManager::B("Aimbot.Silent"))
		{
			return;
		}

		EnsureWeaponBehaviorFireProjectileHook();
		if (ConfigManager::B("Aimbot.NativeProjectileHook"))
		{
			EnsureNativeProjectileHook();
		}
	}

	void OnAimbotHotkey()
	{
		g_LastAimbotHotkeyFrame.store(g_PresentCount.load());
	}

	void ResetArm()
	{
		g_LastSilentArmTime = 0.0;
	}
}
