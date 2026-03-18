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
		static std::mutex g_HookInstallMutex;
		static std::atomic<unsigned int> g_PendingSilentRedirects{ 0 };
		thread_local bool g_ThreadSilentRedirectActive = false;
		thread_local SDK::FVector g_ThreadSilentRedirectTargetPos{};

		static std::atomic<bool> g_NativeProjectileHookInstalled{ false };
		static std::atomic<bool> g_NativeProjectileHookBlocked{ false };
		static std::atomic<bool> g_FireProjectileLoopHookInstalled{ false };
		static std::atomic<bool> g_FireDirectionTraceHookInstalled{ false };

		static std::atomic<bool> g_WeaponBehaviorFireHookInstalled{ false };

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
		static constexpr SignatureRegistry::Signature kFireProjectileLoopSignature{
			"FireProjectileLoop",
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
			"45 89 CF",
			SignatureRegistry::HookTiming::InGameReady
		};
		static constexpr SignatureRegistry::Signature kFireDirectionTraceSignature{
			"FireDirectionTrace",
			"41 56 56 57 53 48 81 EC E8 00 00 00 "
			"4C 89 CF 4C 89 C3 48 89 D6 49 89 CE "
			"4C 8B 8C 24 38 01 00 00 "
			"48 8B 05 ? ? ? ? "
			"48 31 E0 48 89 84 24 E0 00 00 00 "
			"48 8B 41 30 48 8B 88 B0 01 00 00 "
			"4D 85 C9 75 07 4C 8B 0D ? ? ? ? "
			"F3 0F 10 84 24 40 01 00 00 "
			"8A 94 24 48 01 00 00 "
			"44 8A 84 24 30 01 00 00",
			SignatureRegistry::HookTiming::InGameReady
		};
		static constexpr size_t kFireProjectileLoopHookLen = 19;
		static constexpr size_t kFireDirectionTraceHookLen = 24;
		static constexpr const char* kNativeProjectileHooksKey = "NativeProjectileHooks";
		static constexpr const char* kWeaponBehaviorFireProjectileHookKey = "WeaponBehaviorFireProjectile";
		static constexpr SignatureRegistry::HookTiming kSilentAimHookTiming = SignatureRegistry::HookTiming::InGameReady;
		static constexpr size_t kWeaponBehaviorFireProjectileSlot = 130;
		static constexpr size_t kFireParamsFlagsOffset = 28 * sizeof(__int64);
		static constexpr size_t kFireParamsAimOriginOffset = 37 * sizeof(__int64);
		static constexpr size_t kFireParamsMuzzleOriginOffset = 40 * sizeof(__int64);
		static constexpr size_t kFireParamsExplicitRotOffset = 46 * sizeof(__int64);

		static constexpr double kLocalSourceMaxDist = 5000.0;
		static constexpr double kMagicSpawnBacktrackUnits = 12.0;
		static constexpr double kMagicTraceHalfSpanUnits = 12.0;
		static constexpr int kDebugLogIntervalMs = 1000;
		static constexpr int kInfoLogIntervalMs = 3000;
		static constexpr int kErrorLogIntervalMs = 3000;

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
			const bool localSource = muzzleOriginOk && IsLikelyLocalFireSource(muzzleOrigin);
			const bool likelyLocal = localSource && EnsureThreadSilentRedirectActive();
			void* caller = _ReturnAddress();

#if BL4_DEBUG_BUILD
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAimDbg",
				kDebugLogIntervalMs,
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
#endif

			if (likelyLocal && fireParams)
			{
				const SDK::FVector redirectTarget = g_ThreadSilentRedirectTargetPos;
				SDK::FVector redirectSource = muzzleOriginOk ? muzzleOrigin : (aimOriginOk ? aimOrigin : redirectTarget);
				if (IsMagicEnabled())
				{
					const SDK::FVector magicSpawn = BuildMagicSpawnPosition(redirectSource, redirectTarget);
					WriteVec3At(fireParams, kFireParamsAimOriginOffset, magicSpawn);
					WriteVec3At(fireParams, kFireParamsMuzzleOriginOffset, magicSpawn);
					redirectSource = magicSpawn;
				}

				SDK::FRotator desiredRot = Utils::GetRotationToTarget(redirectSource, redirectTarget);
				ClampRotator(desiredRot);
				WriteVec3At(fireParams, kFireParamsExplicitRotOffset, SDK::FVector{ desiredRot.Pitch, desiredRot.Yaw, desiredRot.Roll });

				if (flagsOk)
				{
					WriteU64At(fireParams, kFireParamsFlagsOffset, flags | 8ull);
				}

#if BL4_DEBUG_BUILD
				Logger::LogThrottled(
					Logger::Level::Debug,
					"SilentAimDbg",
					kDebugLogIntervalMs,
					"FireProjectileLoop redirected. caller=%p target=(%.4f, %.4f, %.4f) rot=(%.4f, %.4f, %.4f) magic=%d flagsBefore=0x%llX flagsAfter=0x%llX",
					caller,
					redirectTarget.X, redirectTarget.Y, redirectTarget.Z,
					desiredRot.Pitch, desiredRot.Yaw, desiredRot.Roll,
					IsMagicEnabled() ? 1 : 0,
					static_cast<unsigned long long>(flags),
					static_cast<unsigned long long>(flags | 8ull));
#endif
			}

			const __int64 result = oFireProjectileLoop
				? oFireProjectileLoop(
					weapon,
					fireParams,
					shotIndex,
					shotCount,
					simulate)
				: 0;

#if BL4_DEBUG_BUILD
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAimDbg",
				kDebugLogIntervalMs,
				"FireProjectileLoop return. caller=%p weapon=%p result=%p",
				caller,
				reinterpret_cast<void*>(weapon),
				reinterpret_cast<void*>(result));
#endif
			FinishThreadSilentRedirect();
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
			const bool hadActiveRedirect = g_ThreadSilentRedirectActive;
			const bool localSource = startOk && IsLikelyLocalFireSource(start);
			const bool likelyLocal = localSource && EnsureThreadSilentRedirectActive();
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

		__int64 __fastcall hkWeaponBehaviorFireProjectile(void* behaviorThis)
		{
#if BL4_DEBUG_BUILD
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAimDbg",
				300,
				"hkWeaponBehaviorFireProjectile hit. this=%p",
				behaviorThis);
#endif

			ArmSilentRedirect();
			return oWeaponBehaviorFireProjectile ? oWeaponBehaviorFireProjectile(behaviorThis) : 0;
		}

		static bool InstallNativeHook(
			const SignatureRegistry::Signature& signature,
			void* detour,
			void** originalOut,
			size_t fallbackLen)
		{
			return SignatureRegistry::EnsureHook(signature, detour, originalOut, fallbackLen, "SilentAim");
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

		bool EnsureNativeProjectileHook()
		{
			std::lock_guard<std::mutex> guard(g_HookInstallMutex);
			if (g_FireProjectileLoopHookInstalled.load() && g_FireDirectionTraceHookInstalled.load()) return true;
			if (g_NativeProjectileHookBlocked.load()) return false;
			if (!SignatureRegistry::ShouldAttempt(kNativeProjectileHooksKey, kSilentAimHookTiming))
			{
				return false;
			}

			bool installedAny = false;
			installedAny |= InstallNativeHook(
				kFireProjectileLoopSignature,
				reinterpret_cast<void*>(&hkFireProjectileLoop),
				reinterpret_cast<void**>(&oFireProjectileLoop),
				kFireProjectileLoopHookLen);
			g_FireProjectileLoopHookInstalled.store(oFireProjectileLoop != nullptr);

			installedAny |= InstallNativeHook(
				kFireDirectionTraceSignature,
				reinterpret_cast<void*>(&hkFireDirectionTrace),
				reinterpret_cast<void**>(&oFireDirectionTrace),
				kFireDirectionTraceHookLen);
			g_FireDirectionTraceHookInstalled.store(oFireDirectionTrace != nullptr);

			g_NativeProjectileHookInstalled.store(g_FireProjectileLoopHookInstalled.load() || g_FireDirectionTraceHookInstalled.load());
			if (!g_NativeProjectileHookInstalled.load() && !installedAny)
			{
				Logger::LogThrottled(
					Logger::Level::Error,
					"SilentAim",
					kErrorLogIntervalMs,
					"Native fire hooks not installed.");
			}
			return g_NativeProjectileHookInstalled.load();
		}

		bool EnsureWeaponBehaviorFireProjectileHook()
		{
			std::lock_guard<std::mutex> guard(g_HookInstallMutex);
			if (g_WeaponBehaviorFireHookInstalled.load()) return true;
			if (!SignatureRegistry::ShouldAttempt(kWeaponBehaviorFireProjectileHookKey, kSilentAimHookTiming))
			{
				return false;
			}

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

				Logger::LogThrottled(
					Logger::Level::Error,
					"SilentAim",
					kErrorLogIntervalMs,
					"WeaponBehavior_FireProjectile hook create failed. status=%d(%s) addr=0x%llX",
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
					kErrorLogIntervalMs,
					"WeaponBehavior_FireProjectile hook enable failed. status=%d(%s) addr=0x%llX",
					static_cast<int>(enableStatus),
					MH_StatusToString(enableStatus),
					static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(target)));
				return false;
			}

			g_WeaponBehaviorFireHookInstalled.store(true);
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAim",
				kInfoLogIntervalMs,
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
		if (!target)
		{
			ClearPendingSilentRedirects();
		}
	}

	void Tick()
	{
		const bool wantsSilentHooks =
			ConfigManager::B("Aimbot.Enabled") && ConfigManager::B("Aimbot.Silent");
		const bool wantsTracerHooks = IsTracerCaptureEnabled();
		if (!wantsSilentHooks && !wantsTracerHooks)
		{
			return;
		}

		if (wantsSilentHooks)
		{
			EnsureWeaponBehaviorFireProjectileHook();
		}
		if (ConfigManager::B("Aimbot.NativeProjectileHook") || wantsTracerHooks)
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
		ClearPendingSilentRedirects();
	}
}
