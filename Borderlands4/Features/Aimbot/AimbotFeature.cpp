#include "pch.h"

extern std::atomic<int> g_PresentCount;

namespace Features
{
	namespace Aimbot::SilentAim
	{
		struct CachedState
		{
			std::mutex Mtx;
			bool bIsAimbotActive = false;
			SDK::FVector OriginalLocation{};
			SDK::FRotator OriginalRotation{};
			SDK::FVector SilentLocation{};
			SDK::FRotator SilentRotation{};
			uint32_t LastUpdateFrame = 0;
		};

		static CachedState g_State;
		static SDK::AActor* g_CurrentTarget = nullptr;
		static SDK::FVector g_LatestPos{};
		static std::atomic<int> g_LastHotkeyFrame{ -1 };
		static std::mutex g_InstallMutex;
		static std::atomic<bool> g_PlayerViewPointHooked{ false };
		static std::atomic<bool> g_ViewPointHooked{ false };
		static std::atomic<ULONGLONG> g_LastAttemptMs{ 0 };

		using GetPlayerViewPointFn = void(__fastcall*)(const SDK::AController*, SDK::FVector*, SDK::FRotator*);
		using GetViewPointFn = void(__fastcall*)(const SDK::ULocalPlayer*, SDK::FMinimalViewInfo*, int);

		static GetPlayerViewPointFn oGetPlayerViewPoint = nullptr;
		static GetViewPointFn oGetViewPoint = nullptr;

		void __fastcall hkGetPlayerViewPoint(const SDK::AController* PC, SDK::FVector* Loc, SDK::FRotator* Rot)
		{
			if (oGetPlayerViewPoint) oGetPlayerViewPoint(PC, Loc, Rot);
			if (Loc && Rot)
			{
				std::lock_guard lock(g_State.Mtx);
				g_State.OriginalLocation = *Loc;
				g_State.OriginalRotation = *Rot;
				g_State.bIsAimbotActive = false;

				if (ConfigManager::B("Aimbot.Enabled") && ConfigManager::B("Aimbot.Silent") && 
					(ConfigManager::B("Aimbot.RequireKeyHeld") ? g_LastHotkeyFrame.load() == g_PresentCount.load() : true) &&
					g_CurrentTarget && Utils::IsValidActor(g_CurrentTarget))
				{
					SDK::FVector targetPos = g_LatestPos;
					SDK::FVector source = *Loc;
					SDK::FVector diff = { targetPos.X - source.X, targetPos.Y - source.Y, targetPos.Z - source.Z };
					double distSq = diff.X * diff.X + diff.Y * diff.Y + diff.Z * diff.Z;
					if (distSq > 1.0)
					{
						double dist = sqrt(distSq);
						Rot->Pitch = asin(diff.Z / dist) * (180.0 / 3.141592653589793);
						Rot->Yaw = atan2(diff.Y, diff.X) * (180.0 / 3.141592653589793);
						Rot->Roll = 0.0;
						g_State.SilentLocation = *Loc;
						g_State.SilentRotation = *Rot;
						g_State.bIsAimbotActive = true;
						g_State.LastUpdateFrame = (uint32_t)g_PresentCount.load();
					}
				}
			}
		}

		void __fastcall hkGetViewPoint(const SDK::ULocalPlayer* LP, SDK::FMinimalViewInfo* OutView, int Pass)
		{
			if (oGetViewPoint) oGetViewPoint(LP, OutView, Pass);
			if (OutView)
			{
				std::lock_guard lock(g_State.Mtx);
				if (g_State.bIsAimbotActive && ((uint32_t)g_PresentCount.load() - g_State.LastUpdateFrame < 5))
				{
					OutView->Location = g_State.OriginalLocation;
					OutView->Rotation = g_State.OriginalRotation;
				}
			}
		}

		void EnsureHooks()
		{
			if (g_PlayerViewPointHooked && g_ViewPointHooked) return;
			if (!SignatureRegistry::IsTimingReady(SignatureRegistry::HookTiming::InGameReady)) return;
			
			const ULONGLONG now = GetTickCount64();
			if (g_LastAttemptMs != 0 && (now - g_LastAttemptMs < 5000)) return;
			g_LastAttemptMs = now;

			SDK::APlayerController* PC = GVars.PlayerController;
			if (!PC || !PC->player) return;

			std::lock_guard lock(g_InstallMutex);

			if (!g_PlayerViewPointHooked)
			{
				if (Memory::PatchVTableSlot(PC, 132, (void*)&hkGetPlayerViewPoint, (void**)&oGetPlayerViewPoint, false))
				{
					g_PlayerViewPointHooked = true;
				}
				else
				{
					void*** asVt = reinterpret_cast<void***>(PC);
					Memory::DumpVTableWindow((asVt && *asVt) ? *asVt : nullptr, 132, 8, "AController", "Aimbot");
				}
			}

			if (!g_ViewPointHooked)
			{
				if (Memory::PatchVTableSlot(PC->player, 88, (void*)&hkGetViewPoint, (void**)&oGetViewPoint, false))
				{
					g_ViewPointHooked = true;
				}
				else
				{
					void*** asVt = reinterpret_cast<void***>(PC->player);
					Memory::DumpVTableWindow((asVt && *asVt) ? *asVt : nullptr, 88, 8, "ULocalPlayer", "Aimbot");
				}
			}
		}

		void UpdateTarget(SDK::AActor* target, const SDK::FVector& pos) { g_CurrentTarget = target; g_LatestPos = pos; }
		void OnHotkey() { g_LastHotkeyFrame = g_PresentCount.load(); }
		void Reset() { g_CurrentTarget = nullptr; std::lock_guard lock(g_State.Mtx); g_State.bIsAimbotActive = false; }
	}

	namespace Aimbot
	{
		SDK::AActor* g_AimbotTarget = nullptr;

		void ClearTarget()
		{
			Data::bHasAimbotTarget = false;
			g_AimbotTarget = nullptr;
			SilentAim::UpdateTarget(nullptr, SDK::FVector{});
		}

		void RunTick()
		{
			if (!ConfigManager::B("Aimbot.Enabled") || !GVars.PlayerController)
			{
				ClearTarget();
				return;
			}

			if (ConfigManager::B("Aimbot.Silent")) SilentAim::EnsureHooks();

			const TargetSelectionResult res = Utils::AcquireTarget(
				GVars.PlayerController,
				ConfigManager::F("Aimbot.MaxFOV"),
				ConfigManager::F("Aimbot.MinDistance"),
				ConfigManager::F("Aimbot.MaxDistance"),
				ConfigManager::B("Aimbot.LOS"),
				ConfigManager::S("Aimbot.Bone"),
				ConfigManager::B("Aimbot.TargetAll"),
				ConfigManager::I("Aimbot.TargetMode")
			);

			g_AimbotTarget = res.Target;
			if (g_AimbotTarget && g_AimbotTarget->IsA(SDK::ACharacter::StaticClass()))
			{
				Data::bHasAimbotTarget = true;
				Data::AimbotTargetPos = res.AimPoint;
				SilentAim::UpdateTarget(g_AimbotTarget, Data::AimbotTargetPos);
				return;
			}
			ClearTarget();
		}

		void RunHotkey()
		{
			SilentAim::OnHotkey();
			if (!GVars.PlayerController || !GVars.POV || ConfigManager::B("Aimbot.Silent"))
			{
				SilentAim::Reset();
				return;
			}

			if (!g_AimbotTarget) return;

			SilentAim::Reset();
			const SDK::FVector cam = GVars.POV->Location;
			const SDK::FVector target = Data::AimbotTargetPos;
			SDK::FRotator rot = Utils::GetRotationToTarget(cam, target);
			SDK::FRotator cur = GVars.PlayerController->GetControlRotation();

			if (ConfigManager::B("Aimbot.Smooth"))
			{
				float smooth = (std::max)(ConfigManager::F("Aimbot.SmoothingVector"), 1.0f);
				SDK::FRotator delta = rot - cur;
				delta.Normalize();
				SDK::FRotator smoothed = cur + (delta / smooth);
				smoothed.Normalize();
				GVars.PlayerController->ClientSetRotation(smoothed, true);
			}
			else
			{
				GVars.PlayerController->ClientSetRotation(rot, true);
			}
		}

		void HandleConstructedObject(const SDK::UObject* object)
		{
			(void)object;
		}
	}

	void RegisterAimbotFeature()
	{
		HotkeyManager::Register("Aimbot.Key", "AIMBOT_KEY", ImGuiKey_MouseX2, []() { Aimbot::RunHotkey(); }, true);

		Core::Scheduler::RegisterGameplayTickCallback("Aimbot", []()
		{
			if (!ConfigManager::B("Aimbot.Enabled")) return;
			Aimbot::RunTick();
			if (!ConfigManager::B("Aimbot.RequireKeyHeld")) Aimbot::RunHotkey();
		});

		Core::Scheduler::RegisterOverlayRenderCallback("AimbotOverlay", []()
		{
			if (!ConfigManager::B("Aimbot.Enabled")) return;
			if (ConfigManager::B("Aimbot.DrawFOV"))
				Utils::DrawFOV(ConfigManager::F("Aimbot.MaxFOV"), ConfigManager::F("Aimbot.FOVThickness"));
			if (Data::bHasAimbotTarget && ConfigManager::B("Aimbot.DrawArrow"))
				Utils::DrawSnapLine(Data::AimbotTargetPos, ConfigManager::F("Aimbot.ArrowThickness"));
		});
	}
}
