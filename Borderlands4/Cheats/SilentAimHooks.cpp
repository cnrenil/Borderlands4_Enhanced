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
		static std::atomic<bool> g_FireProjectileLoopHookInstalled{ false };
		static std::atomic<bool> g_FireDirectionTraceHookInstalled{ false };

		static std::atomic<bool> g_WeaponBehaviorFireHookInstalled{ false };
		static std::atomic<bool> g_WeaponBehaviorFireHookAttempted{ false };
		static double g_LastWeaponBehaviorFireHookAttemptTime = 0.0;

		using FireProjectileLoopFn = __int64(__fastcall*)(__int64 weapon, __int64* fireParams, int shotIndex, int shotCount, unsigned __int8 simulate);
		using FireDirectionTraceFn = void* (__fastcall*)(void* weapon, void* outTrace, void* startParam, const void* endParam, unsigned __int8 useAltTrace, __int64 traceContext, float traceScale, unsigned __int8 allowThroughActors);
		using WeaponBehaviorFireProjectileFn = __int64(__fastcall*)(void* behaviorThis);

		static FireProjectileLoopFn oFireProjectileLoop = nullptr;
		static FireDirectionTraceFn oFireDirectionTrace = nullptr;
		static WeaponBehaviorFireProjectileFn oWeaponBehaviorFireProjectile = nullptr;

		// IDA references (for the current build this was derived from):
		// - FireProjectileLoop:  sub_1414F12F8
		// - FireDirectionTrace:  sub_1418AD7C2
		// - WeaponBehavior slot 130 target: sub_1441789C2
		static constexpr const char* kFireProjectileLoopAob =
			"41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ? "
			"66 44 0F 29 BC 24 ? ? ? ? "
			"66 44 0F 29 B4 24 ? ? ? ? "
			"66 44 0F 29 AC 24 ? ? ? ? "
			"66 44 0F 29 A4 24 ? ? ? ? "
			"66 44 0F 29 9C 24 ? ? ? ? "
			"66 44 0F 29 94 24 ? ? ? ? "
			"66 44 0F 29 8C 24 ? ? ? ? "
			"66 44 0F 29 84 24 ? ? ? ? "
			"66 0F 29 BC 24 ? ? ? ? "
			"66 0F 29 B4 24 ? ? ? ? "
			"45 89 CF";
		static constexpr const char* kFireDirectionTraceAob =
			"41 56 56 57 53 48 81 EC E8 00 00 00 "
			"4C 89 CF 4C 89 C3 48 89 D6 49 89 CE "
			"4C 8B 8C 24 38 01 00 00 "
			"48 8B 05 ? ? ? ? "
			"48 31 E0 48 89 84 24 E0 00 00 00 "
			"48 8B 41 30 48 8B 88 B0 01 00 00 "
			"4D 85 C9 75 07 4C 8B 0D ? ? ? ? "
			"F3 0F 10 84 24 40 01 00 00 "
			"8A 94 24 48 01 00 00 "
			"44 8A 84 24 30 01 00 00";
		static constexpr size_t kFireProjectileLoopHookLen = 19;
		static constexpr size_t kFireDirectionTraceHookLen = 24;
		static constexpr size_t kWeaponBehaviorFireProjectileSlot = 130;
		static constexpr size_t kFireParamsFlagsOffset = 28 * sizeof(__int64);
		static constexpr size_t kFireParamsAimOriginOffset = 37 * sizeof(__int64);
		static constexpr size_t kFireParamsMuzzleOriginOffset = 40 * sizeof(__int64);
		static constexpr size_t kFireParamsExplicitRotOffset = 46 * sizeof(__int64);

		static constexpr double kArmWindowSeconds = 0.8;
		static constexpr double kLocalSourceMaxDist = 5000.0;

		static uintptr_t g_FireProjectileLoopAddr = 0;
		static uintptr_t g_FireDirectionTraceAddr = 0;

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

		bool ReadVec3At(const void* base, size_t offset, SDK::FVector& out)
		{
			if (!base) return false;
			const auto* ptr = reinterpret_cast<const unsigned char*>(base) + offset;
			return NativeInterop::ReadVec3Param(ptr, out);
		}

		void WriteVec3At(void* base, size_t offset, const SDK::FVector& value)
		{
			if (!base) return;
			auto* ptr = reinterpret_cast<unsigned char*>(base) + offset;
			NativeInterop::WriteVec3Param(ptr, value);
		}

		bool ReadU64At(const void* base, size_t offset, unsigned long long& out)
		{
			if (!base) return false;
			__try
			{
				out = *reinterpret_cast<const unsigned long long*>(reinterpret_cast<const unsigned char*>(base) + offset);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		void WriteU64At(void* base, size_t offset, unsigned long long value)
		{
			if (!base) return;
			__try
			{
				*reinterpret_cast<unsigned long long*>(reinterpret_cast<unsigned char*>(base) + offset) = value;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
		}

		__int64 __fastcall hkFireProjectileLoop(__int64 weapon, __int64* fireParams, int shotIndex, int shotCount, unsigned __int8 simulate)
		{
			SDK::FVector aimOrigin{};
			SDK::FVector muzzleOrigin{};
			SDK::FVector explicitRot{};
			unsigned long long flags = 0;
			const bool aimOriginOk = ReadVec3At(fireParams, kFireParamsAimOriginOffset, aimOrigin);
			const bool muzzleOriginOk = ReadVec3At(fireParams, kFireParamsMuzzleOriginOffset, muzzleOrigin);
			const bool explicitRotOk = ReadVec3At(fireParams, kFireParamsExplicitRotOffset, explicitRot);
			const bool flagsOk = ReadU64At(fireParams, kFireParamsFlagsOffset, flags);
			const bool armed = IsSilentArmed();
			const bool likelyLocal = armed && muzzleOriginOk && IsLikelyLocalFireSource(muzzleOrigin);
			void* caller = _ReturnAddress();

			Logger::Log(
				Logger::Level::Debug,
				"SilentAimDbg",
				"FireProjectileLoop hit. caller=%p weapon=%p params=%p shot=%d/%d simulate=%u flagsOk=%d flags=0x%llX aimOk=%d aim=(%.4f, %.4f, %.4f) muzzleOk=%d muzzle=(%.4f, %.4f, %.4f) rotOk=%d rot=(%.4f, %.4f, %.4f) armed=%d local=%d",
				caller,
				reinterpret_cast<void*>(weapon),
				fireParams,
				shotIndex,
				shotCount,
				static_cast<unsigned int>(simulate),
				flagsOk ? 1 : 0,
				static_cast<unsigned long long>(flags),
				aimOriginOk ? 1 : 0,
				aimOrigin.X, aimOrigin.Y, aimOrigin.Z,
				muzzleOriginOk ? 1 : 0,
				muzzleOrigin.X, muzzleOrigin.Y, muzzleOrigin.Z,
				explicitRotOk ? 1 : 0,
				explicitRot.X, explicitRot.Y, explicitRot.Z,
				armed ? 1 : 0,
				likelyLocal ? 1 : 0);

			if (likelyLocal && fireParams)
			{
				SDK::FRotator desiredRot = Utils::GetRotationToTarget(muzzleOrigin, g_SilentRedirectTargetPos);
				ClampRotator(desiredRot);
				WriteVec3At(fireParams, kFireParamsExplicitRotOffset, SDK::FVector{ desiredRot.Pitch, desiredRot.Yaw, desiredRot.Roll });

				if (flagsOk)
				{
					WriteU64At(fireParams, kFireParamsFlagsOffset, flags | 8ull);
				}

				Logger::Log(
					Logger::Level::Debug,
					"SilentAimDbg",
					"FireProjectileLoop redirected. caller=%p target=(%.4f, %.4f, %.4f) rot=(%.4f, %.4f, %.4f) flagsBefore=0x%llX flagsAfter=0x%llX",
					caller,
					g_SilentRedirectTargetPos.X, g_SilentRedirectTargetPos.Y, g_SilentRedirectTargetPos.Z,
					desiredRot.Pitch, desiredRot.Yaw, desiredRot.Roll,
					static_cast<unsigned long long>(flags),
					static_cast<unsigned long long>(flags | 8ull));
			}

			const __int64 result = oFireProjectileLoop
				? oFireProjectileLoop(
					weapon,
					fireParams,
					shotIndex,
					shotCount,
					simulate)
				: 0;

			Logger::Log(
				Logger::Level::Debug,
				"SilentAimDbg",
				"FireProjectileLoop return. caller=%p weapon=%p result=%p",
				caller,
				reinterpret_cast<void*>(weapon),
				reinterpret_cast<void*>(result));
			return result;
		}

		void* __fastcall hkFireDirectionTrace(
			void* weapon,
			void* outTrace,
			void* startParam,
			const void* endParam,
			unsigned __int8 useAltTrace,
			__int64 traceContext,
			float traceScale,
			unsigned __int8 allowThroughActors)
		{
			SDK::FVector start{};
			SDK::FVector end{};
			const bool startOk = startParam && NativeInterop::ReadVec3Param(startParam, start);
			const bool endOk = endParam && NativeInterop::ReadVec3Param(endParam, end);
			const bool armed = IsSilentArmed();
			const bool likelyLocal = armed && startOk && IsLikelyLocalFireSource(start);
			void* caller = _ReturnAddress();

			Logger::Log(
				Logger::Level::Debug,
				"SilentAimDbg",
				"FireDirectionTrace hit. caller=%p weapon=%p out=%p startOk=%d start=(%.4f, %.4f, %.4f) endOk=%d end=(%.4f, %.4f, %.4f) useAlt=%u scale=%.4f allowThrough=%u armed=%d local=%d ctx=%p",
				caller,
				weapon,
				outTrace,
				startOk ? 1 : 0,
				start.X, start.Y, start.Z,
				endOk ? 1 : 0,
				end.X, end.Y, end.Z,
				static_cast<unsigned int>(useAltTrace),
				traceScale,
				static_cast<unsigned int>(allowThroughActors),
				armed ? 1 : 0,
				likelyLocal ? 1 : 0,
				reinterpret_cast<void*>(traceContext));

			if (likelyLocal && endParam)
			{
				NativeInterop::WriteVec3Param(const_cast<void*>(endParam), g_SilentRedirectTargetPos);
				Logger::Log(
					Logger::Level::Debug,
					"SilentAimDbg",
					"FireDirectionTrace redirected. caller=%p target=(%.4f, %.4f, %.4f) previousEnd=(%.4f, %.4f, %.4f)",
					caller,
					g_SilentRedirectTargetPos.X, g_SilentRedirectTargetPos.Y, g_SilentRedirectTargetPos.Z,
					end.X, end.Y, end.Z);
			}

			return oFireDirectionTrace
				? oFireDirectionTrace(
					weapon,
						outTrace,
						startParam,
						endParam,
						useAltTrace,
						traceContext,
						traceScale,
						allowThroughActors)
				: nullptr;
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
			if (cache == static_cast<uintptr_t>(-1)) return 0;
			if (cache) return cache;

			const uintptr_t addr = Memory::FindPattern(aob);
			if (!addr)
			{
				cache = static_cast<uintptr_t>(-1);
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

		static bool InstallNativeHook(
			const char* tag,
			uintptr_t target,
			void* detour,
			void** originalOut,
			size_t fallbackLen,
			std::atomic<bool>& installedFlag)
		{
			if (installedFlag.load()) return true;
			if (!target || !detour || !originalOut) return false;

			MH_STATUS createStatus = MH_CreateHook(
				reinterpret_cast<LPVOID>(target),
				detour,
				reinterpret_cast<LPVOID*>(originalOut));
			if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
			{
				if (createStatus == MH_ERROR_MEMORY_ALLOC)
				{
					if (Memory::HookFunctionAbsolute(
						reinterpret_cast<void*>(target),
						detour,
						originalOut,
						fallbackLen))
					{
						installedFlag.store(true);
						Logger::Log(
							Logger::Level::Info,
							"SilentAim",
							"%s hook installed via absolute detour fallback. addr=0x%llX len=%zu",
							tag ? tag : "NativeHook",
							static_cast<unsigned long long>(target),
							fallbackLen);
						return true;
					}

					Logger::Log(
						Logger::Level::Error,
						"SilentAim",
						"%s hook blocked after MEMORY_ALLOC and fallback failed. addr=0x%llX",
						tag ? tag : "NativeHook",
						static_cast<unsigned long long>(target));
					return false;
				}

				Logger::Log(
					Logger::Level::Error,
					"SilentAim",
					"%s hook create failed. status=%d(%s) addr=0x%llX",
					tag ? tag : "NativeHook",
					static_cast<int>(createStatus),
					MH_StatusToString(createStatus),
					static_cast<unsigned long long>(target));
				return false;
			}

			MH_STATUS enableStatus = MH_EnableHook(reinterpret_cast<LPVOID>(target));
			if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
			{
				Logger::Log(
					Logger::Level::Error,
					"SilentAim",
					"%s hook enable failed. status=%d(%s) addr=0x%llX",
					tag ? tag : "NativeHook",
					static_cast<int>(enableStatus),
					MH_StatusToString(enableStatus),
					static_cast<unsigned long long>(target));
				return false;
			}

			installedFlag.store(true);
			Logger::Log(
				Logger::Level::Info,
				"SilentAim",
				"%s hook installed. addr=0x%llX",
				tag ? tag : "NativeHook",
				static_cast<unsigned long long>(target));
			return true;
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
			if (g_FireProjectileLoopHookInstalled.load() && g_FireDirectionTraceHookInstalled.load()) return true;
			if (g_NativeProjectileHookBlocked.load()) return false;

			const double now = NowSeconds();
			if (g_NativeProjectileHookAttempted.load() && (now - g_LastNativeHookAttemptTime) < 2.0)
			{
				return false;
			}
			g_NativeProjectileHookAttempted.store(true);
			g_LastNativeHookAttemptTime = now;

			const uintptr_t fireProjectileLoopTarget =
				ResolveExecTarget("FireProjectileLoop", kFireProjectileLoopAob, g_FireProjectileLoopAddr);
			const uintptr_t fireDirectionTraceTarget =
				ResolveExecTarget("FireDirectionTrace", kFireDirectionTraceAob, g_FireDirectionTraceAddr);

			bool installedAny = false;
			if (fireProjectileLoopTarget)
			{
				installedAny |= InstallNativeHook(
					"FireProjectileLoop",
					fireProjectileLoopTarget,
					reinterpret_cast<void*>(&hkFireProjectileLoop),
					reinterpret_cast<void**>(&oFireProjectileLoop),
					kFireProjectileLoopHookLen,
					g_FireProjectileLoopHookInstalled);
			}

			if (fireDirectionTraceTarget)
			{
				installedAny |= InstallNativeHook(
					"FireDirectionTrace",
					fireDirectionTraceTarget,
					reinterpret_cast<void*>(&hkFireDirectionTrace),
					reinterpret_cast<void**>(&oFireDirectionTrace),
					kFireDirectionTraceHookLen,
					g_FireDirectionTraceHookInstalled);
			}

			g_NativeProjectileHookInstalled.store(
				g_FireProjectileLoopHookInstalled.load() || g_FireDirectionTraceHookInstalled.load());
			if (!g_NativeProjectileHookInstalled.load() && !installedAny)
			{
				Logger::LogThrottled(
					Logger::Level::Error,
					"SilentAim",
					300,
					"Native fire hooks not installed. loop=0x%llX trace=0x%llX",
					static_cast<unsigned long long>(fireProjectileLoopTarget),
					static_cast<unsigned long long>(fireDirectionTraceTarget));
			}
			return g_NativeProjectileHookInstalled.load();
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
