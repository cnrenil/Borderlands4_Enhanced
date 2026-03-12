#include "pch.h"
#include "Engine.h"

// tProcessEvent oProcessEvent = nullptr; // Now declared in Engine.h
void(*oProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*) = nullptr; 
void(*oPostRender)(SDK::UObject*, class SDK::UCanvas*) = nullptr;

extern std::atomic<bool> Cleaning;
extern std::atomic<int> g_ProcessEventCount;
extern std::atomic<int> g_PresentCount;

static float LastPinTime = 0.0f;
static SDK::FVector LastPinPos = { 0, 0, 0 };

void PerformMapTeleport()
{
	if (!GVars.PlayerController || !GVars.Character) return;
	
	SDK::AActor* TargetActor = GVars.Character;
	if (GVars.Character->GetAttachParentActor()) 
		TargetActor = GVars.Character->GetAttachParentActor();

	SDK::FVector TelePos = LastPinPos;
	SDK::FHitResult HitResult;

	// 1. Move to high altitude (no sweep)
	TelePos.Z = 50000.0f;
	TargetActor->K2_SetActorLocation(TelePos, false, &HitResult, false);

	// 2. Snap to ground using sweep
	TelePos.Z = -1000.0f;
	TargetActor->K2_SetActorLocation(TelePos, true, &HitResult, false);

	if (CVars.Debug) {
		SDK::FVector FinalPos = TargetActor->K2_GetActorLocation();
		printf("[MapTP] Teleported to: %.1f, %.1f, %.1f\n", FinalPos.X, FinalPos.Y, FinalPos.Z);
	}

	LastPinPos = { 0, 0, 0 }; 
}

void DiscoveryPinWatcher()
{
	static int LastPinCount = 0;
	if (!MiscSettings.MapTeleport || !GVars.Character || !GVars.Character->PlayerState) 
	{
		LastPinCount = 0;
		return;
	}

	try {
		SDK::AOakPlayerState* PS = (SDK::AOakPlayerState*)GVars.Character->PlayerState;
		if (!PS || IsBadReadPtr(PS, sizeof(void*))) return;

		// DiscoveryPinningState is at 0x1310 in OakPlayerState
		auto& PinArray = PS->DiscoveryPinningState.PinnedDatas;

		if (PinArray.Num() > LastPinCount)
		{
			for (int i = 0; i < PinArray.Num(); i++)
			{
				if (PinArray[i].pintype == SDK::EGbxDiscoveryPinType::CustomWaypoint)
				{
					LastPinPos = PinArray[i].PinnedCustomWaypointLocation;
					LastPinTime = (float)ImGui::GetTime();
					if (CVars.Debug) printf("[MapTP-Watch] Pin detected at: %.1f, %.1f, %.1f\n", LastPinPos.X, LastPinPos.Y, LastPinPos.Z);
				}
			}
		}
		else if (PinArray.Num() < LastPinCount)
		{
			float CurrentTime = (float)ImGui::GetTime();
			float Diff = CurrentTime - LastPinTime;
			if (Diff < MiscSettings.MapTPWindow && LastPinPos.X != 0)
			{
				PerformMapTeleport();
			}
		}
		LastPinCount = PinArray.Num();
	}
	catch (...) {
		if (CVars.Debug) printf("[MapTP-Watch] CRASH in Watcher\n");
	}
}

void hkProcessEvent(const UObject* Object, UFunction* Function, void* Params)
{
	g_ProcessEventCount.fetch_add(1);
	static thread_local bool bInsideHook = false;


	if (!Object || !Function || Cleaning.load() || bInsideHook || Utils::bIsLoading) {
		if (oProcessEvent) oProcessEvent(Object, Function, Params);
		g_ProcessEventCount.fetch_sub(1);
		return;
	}

	bInsideHook = true;

	try {
		if (CVars.Debug)
		{
			const std::string FuncName = Function->GetName();
			const std::string ObjName = Object->GetName();

			std::string lowerName = FuncName;
			std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

			bool bHasFunctionFilter = !TextVars.DebugFunctionNameMustInclude.empty();
			bool bHasObjectFilter = !TextVars.DebugFunctionObjectMustInclude.empty();

			bool bFunctionPass = bHasFunctionFilter && FuncName.find(TextVars.DebugFunctionNameMustInclude) != std::string::npos;
			bool bObjectPass = bHasObjectFilter && ObjName.find(TextVars.DebugFunctionObjectMustInclude) != std::string::npos;

			bool bIsInterestingEvent = !bHasFunctionFilter && !bHasObjectFilter && 
								  (lowerName.find("hit") != std::string::npos || 
								   lowerName.find("impact") != std::string::npos || 
								   lowerName.find("damage") != std::string::npos);

			bool bShouldLog = false;
			
			if (bHasFunctionFilter || bHasObjectFilter) {
				bool fMatch = bHasFunctionFilter ? bFunctionPass : true;
				bool oMatch = bHasObjectFilter ? bObjectPass : true;
				bShouldLog = (fMatch && oMatch);
			} else {
				bShouldLog = bIsInterestingEvent;
			}

			if (bShouldLog)
			{
				auto Sanitize = [](const std::string& s) {
					if (s.length() > 128) return s.substr(0, 125) + "...";
					return s;
				};

				const char* objClass = (Object->Class ? Object->Class->GetName().c_str() : "None");
				printf("[DEBUG] Function: %s | Class: %s | Object: %s\n", Sanitize(FuncName).c_str(), objClass, Sanitize(ObjName).c_str());
				
				if (FuncName == "CameraTransition")
				{
					struct TransitionParams {
						SDK::FName NewMode;
						SDK::FName Transition;
						float BlendTime;
					}* p = (TransitionParams*)Params;
					if (p) {
						std::string modeStr = p->NewMode.ToString();
						std::string transStr = p->Transition.ToString();
						printf("    -> Mode: %s | Transition: %s | Time: %.2f\n", Sanitize(modeStr).c_str(), Sanitize(transStr).c_str(), p->BlendTime);
					}
				}
			}
		}



		static bool bF8WasDown = false;
		bool bF8IsDown = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
		static std::unordered_set<std::string> PrintedClasses;
		if (bF8IsDown && !bF8WasDown)
		{
			PrintedClasses.clear();
			std::cout << "--- F8 DUMP ---\n";
			std::cout << "CVars.ESP: " << CVars.ESP << "\n";
			std::cout << "Utils::bIsLoading: " << Utils::bIsLoading << "\n";
			std::cout << "GVars.PlayerController: " << (GVars.PlayerController ? "Valid" : "NULL") << "\n";
			std::cout << "GVars.Level: " << (GVars.Level ? "Valid" : "NULL") << "\n";
			std::cout << "GVars.Character: " << (GVars.Character ? "Valid" : "NULL") << "\n";
			
			if (GVars.World && GVars.World->VTable) std::cout << "World is Valid\n";
			else std::cout << "World is INVALID\n";
			
			if (GVars.Level)
			{
				std::cout << "Level Actors Num: " << GVars.Level->Actors.Num() << "\n";
				for (int i = 0; i < GVars.Level->Actors.Num(); i++)
				{
					AActor* Actor = GVars.Level->Actors[i];
					if (!Actor || IsBadReadPtr(Actor, sizeof(void*)) || !Actor->VTable) continue;
					
					std::cout << "[" << i << "] Name: " << Actor->GetName() << " | Class: " << (Actor->Class ? Actor->Class->GetName() : "None") << "\n";
					
					if (Actor->IsA(ACharacter::StaticClass()))
					{
						ACharacter* Char = reinterpret_cast<ACharacter*>(Actor);
						if (Char->Mesh)
						{
							std::string ClassNameStr = Char->Class ? Char->Class->GetName() : "Unknown";
							if (bF8IsDown && PrintedClasses.find(ClassNameStr) == PrintedClasses.end())
							{
								PrintedClasses.insert(ClassNameStr);
								int32 NumBones = Char->Mesh->GetNumBones();
								try {
									Cheats::RenderESP();
								} catch (...) {}
								std::cout << "    [Skeleton] Class: " << ClassNameStr << " Total Bones: " << NumBones << "\n";
								for (int b = 0; b < NumBones; b++)
								{
									FName BoneName = Char->Mesh->GetBoneName(b);
									std::cout << "      [" << b << "] " << BoneName.ToString() << "\n";
								}
							}
						}
					}
				}
			}
		}
		bF8WasDown = bF8IsDown;

		// Noisy discovery logs removed for stability

		// Map Teleport Implementation (Network Events)
		if (Function && MiscSettings.MapTeleport && Object)
		{
			const std::string FuncName = Function->GetName();

			// DiscoveryPins usually handled on PlayerState
			if (FuncName == "Server_CreateDiscoveryPin" || FuncName == "Server_AddDiscoveryPin")
			{
				struct InPinParams { SDK::FGbxDiscoveryPinningPinData InPinData; }* p = (InPinParams*)Params;
				if (p && p->InPinData.pintype == SDK::EGbxDiscoveryPinType::CustomWaypoint)
				{
					LastPinTime = (float)ImGui::GetTime();
					LastPinPos = p->InPinData.PinnedCustomWaypointLocation;
					if (CVars.Debug) printf("[MapTP] Waypoint created via DiscoveryPin at: %.1f, %.1f, %.1f\n", LastPinPos.X, LastPinPos.Y, LastPinPos.Z);
				}
			}
			// Regular Pings handled on PlayerController
			else if (FuncName == "ServerCreatePing")
			{
				struct InPingParams { class AActor* TargetedActor; struct FVector Location; }* p = (InPingParams*)Params;
				if (p)
				{
					LastPinTime = (float)ImGui::GetTime();
					LastPinPos = p->Location;
					if (CVars.Debug) printf("[MapTP] Waypoint created via Ping at: %.1f, %.1f, %.1f\n", LastPinPos.X, LastPinPos.Y, LastPinPos.Z);
				}
			}
			// Catch removals locally via hook or rely on Watcher for State sync
			else if (FuncName == "Server_RemoveDiscoveryPin" || FuncName == "Server_ClearDiscoveryPin" || FuncName == "Server_RemoveAllDiscoveryPins" || FuncName == "ServerCancelPing")
			{
				float CurrentTime = (float)ImGui::GetTime();
				float Diff = CurrentTime - LastPinTime;
				
				if (CVars.Debug) printf("[MapTP] Remove Event: %s | Time Diff: %.2f\n", FuncName.c_str(), Diff);

				if (Diff < MiscSettings.MapTPWindow && LastPinPos.X != 0)
				{
					PerformMapTeleport();
				}
			}
		}

		// Intercept CameraTransition to prevent flickering when Third Person is active
		if (Function && Object && CVars.ThirdPerson)
		{
			static UClass* OakPCClass = SDK::AOakPlayerController::StaticClass();
			if (OakPCClass && Object->IsA(OakPCClass))
			{
			if (Function->GetName() == "CameraTransition")
			{
				struct InParams {
					SDK::FName NewMode;
					SDK::FName Transition;
					float BlendTimeOverride;
					bool bTeleport;
					bool bForceResetMode;
				}* p = (InParams*)Params;

				std::string ModeStr = p->NewMode.ToString();
				// If trying to transition OUT of Third Person while cheat is active
				if (ModeStr.find("ThirdPerson") == std::string::npos && ModeStr != "None" && ModeStr != "Default")
				{
					// ONLY block if OTS is enabled (because we want to stay in ThirdPerson)
					// If OTS is disabled, we allow the transition to FirstPerson for native ADS feel
					if (MiscSettings.ThirdPersonOTS)
					{
						bInsideHook = false;
						g_ProcessEventCount.fetch_sub(1);
						return;
					}
				}
			}
		}

		if (Function && MiscSettings.NoBMCooldown && Function->GetName() == "OnDecloakCollisionEnter")
		{
			// Python mod: obj.bCooldownOnView = False
			// Since we don't have the exact offset (and it varies), 
			// we can use UKismetSystemLibrary::SetBoolPropertyByName if available, 
			// or assume it's at a known offset if we find it. 
			// For now, let's keep the hook call and hope to find the offset.
		}
	}
}
catch (...) {
		// Suppress exceptions
	}

	bInsideHook = false;
	if (oProcessEvent) oProcessEvent(Object, Function, Params);
	g_ProcessEventCount.fetch_sub(1);
}



void hkPostRender(UObject* ViewportClient, class UCanvas* Canvas)
{
	static int LastUpdateFrame = 0;
	if (LastUpdateFrame != g_PresentCount.load())
	{
		LastUpdateFrame = g_PresentCount.load();
		GVars.AutoSetVariables();
		if (CVars.ESP) Cheats::UpdateESP();
		if (CVars.Aimbot) Cheats::Aimbot();
		Cheats::WeaponModifiers();
		Cheats::Flight();
		Cheats::SetPlayerSpeed();
		Cheats::EnforcePersistence();
		DiscoveryPinWatcher();
		Cheats::ChangeFOV();
		Cheats::ChangeGameRenderSettings();
		Cheats::SilentAimHoming();
		Cheats::TriggerBot();
		
		static bool bF5WasDown = false;
		bool bF5IsDown = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
		if (bF5IsDown && !bF5WasDown)
		{
			Cheats::ToggleThirdPerson();
		}
		bF5WasDown = bF5IsDown;
	}

	if (oPostRender) oPostRender(ViewportClient, Canvas);
}

void Hooks::UnhookAll()
{
	printf("[Hook] Unhooking all VTable hooks...\n");
	
	DWORD old;
	if (pcVTable && oProcessEvent)
	{
		if (VirtualProtect(&pcVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			pcVTable[73] = (void*)oProcessEvent;
			VirtualProtect(&pcVTable[73], sizeof(void*), old, &old);
			printf("[Hook] Restored PlayerController ProcessEvent.\n");
		}
	}

	if (psVTable && oProcessEvent)
	{
		if (VirtualProtect(&psVTable[73], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			psVTable[73] = (void*)oProcessEvent;
			VirtualProtect(&psVTable[73], sizeof(void*), old, &old);
			printf("[Hook] Restored PlayerState ProcessEvent.\n");
		}
	}

	if (viewportVTable && oPostRender)
	{
		if (VirtualProtect(&viewportVTable[0x6D], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
		{
			viewportVTable[0x6D] = (void*)oPostRender;
			VirtualProtect(&viewportVTable[0x6D], sizeof(void*), old, &old);
			printf("[Hook] Restored ViewportClient PostRender.\n");
		}
	}
}

void** Hooks::pcVTable = nullptr;
void** Hooks::psVTable = nullptr;
void** Hooks::viewportVTable = nullptr;

bool Hooks::HookProcessEvent()
{
	if (!GVars.PlayerController)
	{
		return false; // Wait silently until it's valid
	}

	void** TempVTable = *reinterpret_cast<void***>(GVars.PlayerController);
	if (!TempVTable)
	{
		std::cout << "[Hook] ERROR: PlayerController VTable is null!\n";
		return false;
	}

	pcVTable = TempVTable;

	int processEventIdx = 73; // We'll hardcode it locally for clarity since Offsets::ProcessEventIdx seems to be missing in some contexts

	if (TempVTable[processEventIdx] == &hkProcessEvent)
	{
		// Try to hook PostRender if not already done
		if (!oPostRender)
		{
			UWorld* World = Utils::GetWorldSafe();
			if (World && World->OwningGameInstance && World->OwningGameInstance->LocalPlayers.Num() > 0)
			{
				UObject* ViewportClient = World->OwningGameInstance->LocalPlayers[0]->ViewportClient;
				if (ViewportClient)
				{
					viewportVTable = *reinterpret_cast<void***>(ViewportClient);
					int postRenderIndex = 0x6D;
					
					if (viewportVTable && viewportVTable[postRenderIndex] != &hkPostRender)
					{
						oPostRender = reinterpret_cast<void(*)(SDK::UObject*, class SDK::UCanvas*)>(viewportVTable[postRenderIndex]);
						DWORD oldP;
						if (VirtualProtect(&viewportVTable[postRenderIndex], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldP))
						{
							viewportVTable[postRenderIndex] = &hkPostRender;
							VirtualProtect(&viewportVTable[postRenderIndex], sizeof(void*), oldP, &oldP);
							std::cout << "[Hook] SUCCESS: PostRender Hooked!\n";
						}
					}
				}
			}
		}
		return true; // Already hooked
	}

	oProcessEvent = reinterpret_cast<void(*)(const SDK::UObject*, SDK::UFunction*, void*)>(TempVTable[processEventIdx]);
	if (!oProcessEvent)
	{
		std::cout << "[Hook] ERROR: ProcessEvent func ptr from VTable is null! Index: " << processEventIdx << "\n";
		return false;
	}

	std::cout << "[Hook] Overwriting VTable index " << processEventIdx << " at: " << &TempVTable[processEventIdx] << "\n";
	
	DWORD oldProtect;
	if (VirtualProtect(&TempVTable[processEventIdx], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
	{
		TempVTable[processEventIdx] = &hkProcessEvent;
		VirtualProtect(&TempVTable[processEventIdx], sizeof(void*), oldProtect, &oldProtect);
		std::cout << "[Hook] SUCCESS: VTable Overwritten globally for PlayerController!\n";
		return true;
	}
	else
	{
		std::cout << "[Hook] ERROR: VirtualProtect failed to change VTable memory permissions!\n";
		return false;
	}
}

