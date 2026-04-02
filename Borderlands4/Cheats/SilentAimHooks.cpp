#include "pch.h"
#include <intrin.h>
#include "Config/ConfigManager.h"
#include "Utils/Logger.h"

extern std::atomic<int> g_PresentCount;

namespace SilentAimHooks
{
	static SDK::AActor* g_CurrentAimbotTarget = nullptr;
	static SDK::FVector g_LatestTargetPos{};

	namespace
	{
		static std::atomic<int> g_LastAimbotHotkeyFrame{ -1 };
		static SDK::FVector g_SilentRedirectTargetPos{};
		static std::mutex g_HookInstallMutex;
		static std::atomic<unsigned int> g_PendingSilentRedirects{ 0 };
		thread_local bool g_ThreadSilentRedirectActive = false;
		thread_local SDK::FVector g_ThreadSilentRedirectTargetPos{};

		static std::atomic<bool> g_NativeProjectileHookInstalled{ false };
		static std::atomic<bool> g_NativeProjectileHookBlocked{ false };
		static std::atomic<bool> g_FireProjectileLoopHookInstalled{ false };
		static std::atomic<bool> g_FireDirectionTraceHookInstalled{ false };

		static std::atomic<bool> g_WeaponBehaviorFireHookInstalled{ false };
		static std::atomic<bool> g_WeaponBehaviorFireNativeHookInstalled{ false };
		static std::atomic<bool> g_KnownNewWeaponBehaviorBuild{ false };
		static std::atomic<ULONGLONG> g_LastNativeProjectileAttemptMs{ 0 };
		static std::atomic<ULONGLONG> g_LastWeaponBehaviorAttemptMs{ 0 };

		using FireProjectileLoopFn = __int64(__fastcall*)(__int64 weapon, __int64 fireParams, __int64 a3, double a4, __int64 a5, __int64 a6);
		using FireDirectionTraceFn = void* (__fastcall*)(void* weapon, void* outTrace, void* startParam, const void* endParam, unsigned __int8 useAltTrace, __int64 traceContext, float traceScale, unsigned __int8 allowThroughActors);
		using WeaponBehaviorFireProjectileFn = __int64(__fastcall*)(void* behaviorThis, void* a2);

		static FireProjectileLoopFn oFireProjectileLoop = nullptr;
		static FireDirectionTraceFn oFireDirectionTrace = nullptr;
		static WeaponBehaviorFireProjectileFn oWeaponBehaviorFireProjectile = nullptr;

		// IDA references:
		// Legacy build:
		// - FireProjectileLoop:  sub_1414F12F8
		// - FireDirectionTrace:  sub_1418AD7C2
		// - WeaponBehavior slot 130 target: sub_1441789C2
		// Current/new build:
		// - WeaponBehavior slot 130 target: sub_1412D5D40
		// - sub_1412D5D40 -> sub_1412CB890 is the new firing chain currently under re-analysis.
		//
		// The two native signatures below are intentionally kept as legacy-only until the new build's
		// projectile redirect point is re-derived. The new build no longer matches the old fire-param layout.
		static constexpr uintptr_t kNewWeaponBehaviorFireProjectileRva = 0x12D5D40;
		static constexpr SignatureRegistry::Signature kWeaponBehaviorFireProjectileNativeSignature{
			"WeaponBehaviorFireProjectileNative",
			"41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ? 66 44 0F 29 8C 24 ? ? ? ? 66 44 0F 29 84 24 ? ? ? ? 66 0F 29 BC 24 ? ? ? ? 66 0F 29 B4 24 ? ? ? ? 48 8B 05 ? ? ? ? 48 31 E0 48 89 84 24 ? ? ? ? 8B 81 10 02 00 00 85 C0",
			SignatureRegistry::HookTiming::InGameReady
		};
		static constexpr SignatureRegistry::Signature kFireProjectileLoopSignature{
			"FireProjectileLoop",
			"41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ?",
			SignatureRegistry::HookTiming::InGameReady
		};
		static constexpr SignatureRegistry::Signature kFireDirectionTraceSignature{
			"FireDirectionTrace",
			"41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ? 44 89 4C 24 ? 44 88 44 24 ? 48 89 54 24 ? 49 89 CF",
			SignatureRegistry::HookTiming::InGameReady
		};
		static constexpr size_t kFireProjectileLoopHookLen = 19;
		static constexpr size_t kFireDirectionTraceHookLen = 24;
		static constexpr SignatureRegistry::HookTiming kSilentAimHookTiming = SignatureRegistry::HookTiming::InGameReady;
		static constexpr size_t kWeaponBehaviorFireProjectileSlot = 130;
		static constexpr int kFireParamsAimOriginOffset = 144;    // 0x90 (Vector3d/double[3])
		static constexpr int kFireParamsMuzzleOriginOffset = 168; // 0xA8
		static constexpr int kFireParamsExplicitRotOffset = 192;  // 0xC0
		static constexpr int kFireParamsFlagsOffset = 136;        // 0x88 (Byte flag)
		static constexpr int kFireParamsForceRedirectBit = 0;    // Bit 0 of flag at 0x88

		static constexpr double kLocalSourceMaxDist = 5000.0;
		static constexpr double kMagicSpawnBacktrackUnits = 12.0;

#pragma pack(push, 1)
		struct FireParams_LWC {
			unsigned char padding_0[136];
			unsigned char flags;          // 0x88
			unsigned char padding_1[7];
			double aim_origin[3];         // 0x90
			double muzzle_origin[3];      // 0xA8
			double explicit_rot[3];       // 0xC0
		};
#pragma pack(pop)



		static constexpr double kMagicTraceHalfSpanUnits = 12.0;
		static constexpr int kDebugLogIntervalMs = 1000;
		static constexpr int kInfoLogIntervalMs = 3000;
		static constexpr int kErrorLogIntervalMs = 3000;
		static constexpr ULONGLONG kHookRetryIntervalMs = 5000;

		void RegisterSilentAimSignatures()
		{
			// Always register - EnsureHook will handle the state
			SignatureRegistry::Register(
				kWeaponBehaviorFireProjectileNativeSignature.Name,
				kWeaponBehaviorFireProjectileNativeSignature.Pattern,
				kWeaponBehaviorFireProjectileNativeSignature.Timing);

			SignatureRegistry::Register(
				kFireProjectileLoopSignature.Name,
				kFireProjectileLoopSignature.Pattern,
				kFireProjectileLoopSignature.Timing);
/*
			SignatureRegistry::Register(
				kFireDirectionTraceSignature.Name,
				kFireDirectionTraceSignature.Pattern,
				kFireDirectionTraceSignature.Timing);
*/
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
				g_CurrentAimbotTarget &&
				Utils::IsValidActor(g_CurrentAimbotTarget) &&
				g_PendingSilentRedirects.load() > 0;
		}

		bool IsMagicEnabled()
		{
			return ConfigManager::B("Aimbot.Magic");
		}

		bool IsTracerCaptureEnabled()
		{
			return ConfigManager::B("Player.ESP") && ConfigManager::B("ESP.BulletTracers");
		}

		bool ShouldAttemptDelayedHook(std::atomic<ULONGLONG>& lastAttemptMs)
		{
			if (!SignatureRegistry::IsTimingReady(kSilentAimHookTiming))
				return false;

			const ULONGLONG nowMs = GetTickCount64();
			const ULONGLONG previous = lastAttemptMs.load();
			if (previous != 0 && (nowMs - previous) < kHookRetryIntervalMs)
				return false;

			lastAttemptMs.store(nowMs);
			return true;
		}

		void CaptureBulletTracer(const SDK::FVector& start, const SDK::FVector& end)
		{
			if (!IsTracerCaptureEnabled())
			{
				return;
			}

			CheatsData::BulletTracer tracer{};
			tracer.CreationTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
			tracer.Seed = 0;
			tracer.bClosed = true;
			tracer.Points.push_back(start);
			tracer.Points.push_back(end);

			std::lock_guard<std::mutex> lock(CheatsData::TracerMutex);
			CheatsData::BulletTracers.push_back(std::move(tracer));
		}

		void ArmSilentRedirect()
		{
			if (!ConfigManager::B("Aimbot.Enabled") ||
				!ConfigManager::B("Aimbot.Silent") ||
				!ConfigManager::B("Aimbot.NativeProjectileHook") ||
				!IsAimbotActivationAllowed() ||
				!g_CurrentAimbotTarget ||
				!Utils::IsValidActor(g_CurrentAimbotTarget))
			{
				return;
			}

			g_SilentRedirectTargetPos = g_LatestTargetPos;
			g_PendingSilentRedirects.fetch_add(1);
		}

		void ClearPendingSilentRedirects()
		{
			g_PendingSilentRedirects.store(0);
			g_ThreadSilentRedirectActive = false;
		}

		bool TryActivateThreadSilentRedirect()
		{
			if (!ConfigManager::B("Aimbot.Enabled") ||
				!ConfigManager::B("Aimbot.Silent") ||
				!ConfigManager::B("Aimbot.NativeProjectileHook") ||
				!g_CurrentAimbotTarget ||
				!Utils::IsValidActor(g_CurrentAimbotTarget))
			{
				return false;
			}

			unsigned int pending = g_PendingSilentRedirects.load();
			while (pending > 0)
			{
				if (g_PendingSilentRedirects.compare_exchange_weak(pending, pending - 1))
				{
					g_ThreadSilentRedirectTargetPos = g_SilentRedirectTargetPos;
					g_ThreadSilentRedirectActive = true;
					return true;
				}
			}

			return false;
		}

		bool EnsureThreadSilentRedirectActive()
		{
			if (g_ThreadSilentRedirectActive)
			{
				return true;
			}

			return TryActivateThreadSilentRedirect();
		}

		bool TryActivateFallbackSilentRedirect()
		{
			if (!ConfigManager::B("Aimbot.Enabled") ||
				!ConfigManager::B("Aimbot.Silent") ||
				!ConfigManager::B("Aimbot.NativeProjectileHook") ||
				!IsAimbotActivationAllowed() ||
				!g_CurrentAimbotTarget ||
				!Utils::IsValidActor(g_CurrentAimbotTarget))
			{
				return false;
			}

			g_ThreadSilentRedirectTargetPos = g_LatestTargetPos;
			g_ThreadSilentRedirectActive = true;
			return true;
		}

		void FinishThreadSilentRedirect()
		{
			g_ThreadSilentRedirectActive = false;
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

		SDK::FVector AddVector(const SDK::FVector& a, const SDK::FVector& b)
		{
			return SDK::FVector{ a.X + b.X, a.Y + b.Y, a.Z + b.Z };
		}

		SDK::FVector SubtractVector(const SDK::FVector& a, const SDK::FVector& b)
		{
			return SDK::FVector{ a.X - b.X, a.Y - b.Y, a.Z - b.Z };
		}

		SDK::FVector ScaleVector(const SDK::FVector& v, double scale)
		{
			return SDK::FVector{
				static_cast<float>(v.X * scale),
				static_cast<float>(v.Y * scale),
				static_cast<float>(v.Z * scale)
			};
		}

		SDK::FVector NormalizeVector(const SDK::FVector& v)
		{
			const double lenSq =
				static_cast<double>(v.X) * static_cast<double>(v.X) +
				static_cast<double>(v.Y) * static_cast<double>(v.Y) +
				static_cast<double>(v.Z) * static_cast<double>(v.Z);
			if (lenSq <= 1e-6)
			{
				return SDK::FVector{ 1.0f, 0.0f, 0.0f };
			}

			const double invLen = 1.0 / sqrt(lenSq);
			return ScaleVector(v, invLen);
		}

		SDK::FVector BuildMagicSpawnPosition(const SDK::FVector& source, const SDK::FVector& target)
		{
			const SDK::FVector dir = NormalizeVector(SubtractVector(target, source));
			return SubtractVector(target, ScaleVector(dir, kMagicSpawnBacktrackUnits));
		}

		void BuildMagicTraceSegment(const SDK::FVector& source, const SDK::FVector& target, SDK::FVector& outStart, SDK::FVector& outEnd)
		{
			const SDK::FVector dir = NormalizeVector(SubtractVector(target, source));
			const SDK::FVector offset = ScaleVector(dir, kMagicTraceHalfSpanUnits);
			outStart = SubtractVector(target, offset);
			outEnd = AddVector(target, offset);
		}

		__int64 __fastcall hkFireProjectileLoop(__int64 weapon, __int64 fireParams, __int64 a3, double a4, __int64 a5, __int64 a6)
		{
			if (ConfigManager::B("Aimbot.Silent") && g_CurrentAimbotTarget && fireParams)
			{
				FireParams_LWC* params = reinterpret_cast<FireParams_LWC*>(fireParams);
				const SDK::FVector redirectTarget = g_LatestTargetPos;
				const SDK::FVector currentMuzzle = *(SDK::FVector*)params->muzzle_origin;
				
				bool magicActive = false;
				SDK::FVector finalSource = currentMuzzle;

				if (ConfigManager::B("Aimbot.Magic") && g_CurrentAimbotTarget)
				{
					SDK::FVector magicSpawnPos = BuildMagicSpawnPosition(currentMuzzle, redirectTarget);
					params->aim_origin[0] = magicSpawnPos.X;
					params->aim_origin[1] = magicSpawnPos.Y;
					params->aim_origin[2] = magicSpawnPos.Z;
					
					params->muzzle_origin[0] = magicSpawnPos.X;
					params->muzzle_origin[1] = magicSpawnPos.Y;
					params->muzzle_origin[2] = magicSpawnPos.Z;
					
					finalSource = magicSpawnPos;
					magicActive = true;
				}
				else
				{
					params->aim_origin[0] = redirectTarget.X;
					params->aim_origin[1] = redirectTarget.Y;
					params->aim_origin[2] = redirectTarget.Z;
				}

				// Calculate Rotator
				double dx = redirectTarget.X - finalSource.X;
				double dy = redirectTarget.Y - finalSource.Y;
				double dz = redirectTarget.Z - finalSource.Z;
				
				double yaw = atan2(dy, dx) * (180.0 / 3.14159265358979323846);
				double pitch = atan2(dz, sqrt(dx * dx + dy * dy)) * (180.0 / 3.14159265358979323846);

				params->explicit_rot[0] = pitch;
				params->explicit_rot[1] = yaw;
				params->explicit_rot[2] = 0.0;
				
				// Force Redirect Bit
				params->flags |= (1 << kFireParamsForceRedirectBit);

#if BL4_DEBUG_BUILD
				LOG_DEBUG("SilentAim", "[SilentAim] Redirected fire. Magic=%d Target=(%.1f, %.1f)", magicActive ? 1 : 0, redirectTarget.X, redirectTarget.Y);
#endif
			}

			return oFireProjectileLoop ? oFireProjectileLoop(weapon, fireParams, a3, a4, a5, a6) : 0;
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
			const bool hadActiveRedirect = g_ThreadSilentRedirectActive;
			const bool localSource = startOk && IsLikelyLocalFireSource(start);
			const bool likelyLocal = localSource &&
				(EnsureThreadSilentRedirectActive() || (!armed && TryActivateFallbackSilentRedirect()));
			void* caller = _ReturnAddress();

#if BL4_DEBUG_BUILD
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAimDbg",
				kDebugLogIntervalMs,
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
#endif

			if (likelyLocal && endParam)
			{
				const SDK::FVector redirectTarget = g_ThreadSilentRedirectTargetPos;
				if (IsMagicEnabled())
				{
					SDK::FVector magicStart = redirectTarget;
					SDK::FVector magicEnd = redirectTarget;
					BuildMagicTraceSegment(startOk ? start : redirectTarget, redirectTarget, magicStart, magicEnd);
					if (startParam)
					{
						NativeInterop::WriteVec3Param(startParam, magicStart);
					}
					NativeInterop::WriteVec3Param(const_cast<void*>(endParam), magicEnd);
				}
				else
				{
					NativeInterop::WriteVec3Param(const_cast<void*>(endParam), redirectTarget);
				}

#if BL4_DEBUG_BUILD
				Logger::LogThrottled(
					Logger::Level::Debug,
					"SilentAimDbg",
					kDebugLogIntervalMs,
					"FireDirectionTrace redirected. caller=%p target=(%.4f, %.4f, %.4f) previousEnd=(%.4f, %.4f, %.4f) magic=%d",
					caller,
					redirectTarget.X, redirectTarget.Y, redirectTarget.Z,
					end.X, end.Y, end.Z,
					IsMagicEnabled() ? 1 : 0);
#endif
			}

			SDK::FVector tracerStart = startOk ? start : SDK::FVector{};
			SDK::FVector tracerEnd = endOk ? end : SDK::FVector{};
			bool tracerReady = localSource && startOk && endOk;
			if (tracerReady && endParam)
			{
				SDK::FVector updatedEnd{};
				if (NativeInterop::ReadVec3Param(endParam, updatedEnd))
				{
					tracerEnd = updatedEnd;
				}

				if (startParam)
				{
					SDK::FVector updatedStart{};
					if (NativeInterop::ReadVec3Param(startParam, updatedStart))
					{
						tracerStart = updatedStart;
					}
				}
			}

			void* result = oFireDirectionTrace
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

			if (tracerReady)
			{
				CaptureBulletTracer(tracerStart, tracerEnd);
			}
			if (!hadActiveRedirect)
			{
				FinishThreadSilentRedirect();
			}
			return result;
		}

		__int64 __fastcall hkWeaponBehaviorFireProjectile(void* behaviorThis, void* a2)
		{
#if BL4_DEBUG_BUILD
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAimDbg",
				300,
				"hkWeaponBehaviorFireProjectile hit. this=%p ctx=%p",
				behaviorThis, a2);
#endif

			ArmSilentRedirect();
			return oWeaponBehaviorFireProjectile ? oWeaponBehaviorFireProjectile(behaviorThis, a2) : 0;
		}

		static bool InstallNativeHook(
			const SignatureRegistry::Signature& signature,
			void* detour,
			void** originalOut,
			size_t fallbackLen)
		{
			return SignatureRegistry::EnsureHook(signature, detour, originalOut, fallbackLen, "SilentAim", true, true);
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
					kErrorLogIntervalMs,
					"WeaponBehavior_FireProjectile vtable slot is null. slot=%zu",
					kWeaponBehaviorFireProjectileSlot);
				return false;
			}

			*targetOut = target;
			const uintptr_t imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
			const uintptr_t targetRva = reinterpret_cast<uintptr_t>(target) - imageBase;
			if (targetRva == kNewWeaponBehaviorFireProjectileRva)
			{
				g_KnownNewWeaponBehaviorBuild.store(true);
			}
			return true;
		}

		static bool PatchWeaponBehaviorFireProjectileVTable(void* detour, void** originalOut)
		{
			if (!detour || !originalOut) return false;

			SDK::UWeaponBehavior_FireProjectile* def = SDK::UWeaponBehavior_FireProjectile::GetDefaultObj();
			if (!def) return false;

			void* target = nullptr;
			if (!GetWeaponBehaviorFireProjectileTarget(&target)) return false;

			if (!Memory::PatchVTableSlot(def, kWeaponBehaviorFireProjectileSlot, detour, originalOut, true)) // true for stealth
			{
				Logger::LogThrottled(
					Logger::Level::Error,
					"SilentAim",
					kErrorLogIntervalMs,
					"WeaponBehavior_FireProjectile vtable patch failed. slot=%zu",
					kWeaponBehaviorFireProjectileSlot);
				return false;
			}

			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAim",
				kInfoLogIntervalMs,
				"WeaponBehavior_FireProjectile vtable patched. slot=%zu target=%p",
				kWeaponBehaviorFireProjectileSlot,
				target);
			return true;
		}


		bool EnsureNativeSilentHooks()
		{
			// Only install if timing is ready
			if (!SignatureRegistry::IsTimingReady(kSilentAimHookTiming))
				return false;

			if (!g_FireProjectileLoopHookInstalled.load())
			{
				if (SignatureRegistry::EnsureHook(kFireProjectileLoopSignature, (void*)&hkFireProjectileLoop, reinterpret_cast<void**>(&oFireProjectileLoop), true))
				{
					g_FireProjectileLoopHookInstalled.store(true);
					LOG_INFO("SilentAim", "FireProjectileLoop Hooked successfully.");
				}
			}

/*
			if (!g_FireDirectionTraceHookInstalled.load())
			{
				if (SignatureRegistry::EnsureHook(kFireDirectionTraceSignature, &hkFireDirectionTrace, reinterpret_cast<void**>(&oFireDirectionTrace), true))
				{
					g_FireDirectionTraceHookInstalled.store(true);
					LOG_INFO("SilentAim", "FireDirectionTrace Hooked successfully.");
				}
			}
*/
			return g_FireProjectileLoopHookInstalled.load();
		}

		bool EnsureWeaponBehaviorFireProjectileHook()
		{
			std::lock_guard<std::mutex> guard(g_HookInstallMutex);
			if (g_WeaponBehaviorFireHookInstalled.load()) return true;
			if (!ShouldAttemptDelayedHook(g_LastWeaponBehaviorAttemptMs))
			{
				return false;
			}

			SDK::UWeaponBehavior_FireProjectile* def = SDK::UWeaponBehavior_FireProjectile::GetDefaultObj();
			if (!def) return false;

			void* target = nullptr;
			if (!GetWeaponBehaviorFireProjectileTarget(&target)) return false;

			// Prefer a native inline hook on the current build's unified fire-behavior implementation.
			// Keep the VMT path as fallback when the signature is unavailable.
			if (!g_WeaponBehaviorFireNativeHookInstalled.load())
			{
				if (SignatureRegistry::EnsureHook(
						kWeaponBehaviorFireProjectileNativeSignature,
						reinterpret_cast<void*>(&hkWeaponBehaviorFireProjectile),
						reinterpret_cast<void**>(&oWeaponBehaviorFireProjectile),
						true))
				{
					g_WeaponBehaviorFireNativeHookInstalled.store(true);
					g_WeaponBehaviorFireHookInstalled.store(true);
					Logger::LogThrottled(
						Logger::Level::Debug,
						"SilentAim",
						kInfoLogIntervalMs,
						"WeaponBehavior_FireProjectile native hook installed. vtableTarget=%p expectedRva=0x%llX",
						target,
						static_cast<unsigned long long>(kNewWeaponBehaviorFireProjectileRva));
					return true;
				}
			}

			// Use VMT Hijacking for WeaponBehavior - Transparent to CRC
			if (StealthHook::HookVMT(def, kWeaponBehaviorFireProjectileSlot, reinterpret_cast<void*>(&hkWeaponBehaviorFireProjectile), reinterpret_cast<void**>(&oWeaponBehaviorFireProjectile)))
			{
				g_WeaponBehaviorFireHookInstalled.store(true);
				Logger::LogThrottled(
					Logger::Level::Debug,
					"SilentAim",
					kInfoLogIntervalMs,
					"WeaponBehavior_FireProjectile VMT Hooked. slot=%zu target=%p",
					kWeaponBehaviorFireProjectileSlot,
					target);
				return true;
			}

			Logger::LogThrottled(
				Logger::Level::Error,
				"SilentAim",
				kErrorLogIntervalMs,
				"WeaponBehavior_FireProjectile VMT hijacking failed.");
			return false;
		}
	}

	void UpdateTarget(SDK::AActor* target, const SDK::FVector& targetPos)
	{
		g_CurrentAimbotTarget = target;
		g_LatestTargetPos = targetPos;
		if (!target)
		{
			ClearPendingSilentRedirects();
		}
	}

	void Tick()
	{
		RegisterSilentAimSignatures();

		const bool wantsSilentHooks =
			ConfigManager::B("Aimbot.Enabled") && ConfigManager::B("Aimbot.Silent");
		const bool wantsTracerHooks = IsTracerCaptureEnabled();
		if (!wantsSilentHooks && !wantsTracerHooks)
		{
			return;
		}

		if (wantsSilentHooks)
		{
			EnsureNativeSilentHooks();
			EnsureWeaponBehaviorFireProjectileHook();
		}
	}

	void OnAimbotHotkey()
	{
		g_LastAimbotHotkeyFrame.store(g_PresentCount.load());
	}

	void ResetArm()
	{
		ClearPendingSilentRedirects();
	}
}
