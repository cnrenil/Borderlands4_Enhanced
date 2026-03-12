#include "pch.h"
#include "Engine.h"
#include <mutex>
#include <unordered_set>
#include <fstream>
#include <chrono>
#include <iomanip>



void(*oProcessEvent)(const SDK::UObject*, SDK::UFunction*, void*) = nullptr; 
void(*oPostRender)(SDK::UObject*, class SDK::UCanvas*) = nullptr;

extern std::atomic<bool> Cleaning;
extern std::atomic<int> g_ProcessEventCount;
extern std::atomic<int> g_PresentCount;

static float LastPinTime = 0.0f;
static SDK::FVector LastPinPos = { 0, 0, 0 };
static std::mutex g_LogMutex;

static bool g_IsRecording = false;
static std::ofstream g_RecordStream;

void PerformMapTeleport()
{
	if (!GVars.PlayerController || !GVars.Character) return;
	
	SDK::AActor* TargetActor = GVars.Character;
	if (GVars.Character->GetAttachParentActor()) 
		TargetActor = GVars.Character->GetAttachParentActor();

	SDK::FVector TelePos = LastPinPos;
	SDK::FHitResult HitResult;

	TelePos.Z = 50000.0f;

	TargetActor->K2_SetActorLocation(TelePos, false, &HitResult, false);

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


	if (!Object || !Function || Cleaning.load() || bInsideHook) {
		if (oProcessEvent) oProcessEvent(Object, Function, Params);
		g_ProcessEventCount.fetch_sub(1);
		return;
	}

	bInsideHook = true;

	try {
		if (g_IsRecording)
		{


			std::lock_guard<std::mutex> lock(g_LogMutex);
			if (g_RecordStream.is_open())
			{
				const std::string FuncName = Function->GetName();
				const std::string ClassName = Object->Class ? Object->Class->GetName() : "None";
				const std::string ObjName = Object->GetName();

				g_RecordStream << "[" << std::fixed << std::setprecision(2) << ImGui::GetTime() << "] "
							  << ClassName << "::" << FuncName << " (" << ObjName << ")\n";
			}
		}

		if (Utils::bIsLoading) {
			bInsideHook = false;
			if (oProcessEvent) oProcessEvent(Object, Function, Params);
			g_ProcessEventCount.fetch_sub(1);
			return;
		}



		if (Function && MiscSettings.MapTeleport && Object)

		{
			const std::string FuncName = Function->GetName();



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
			else if (FuncName == "ClientCreatePing")
			{
				struct InPingParams { int32 PingInstigator; class AActor* TargetedActor; struct FVector Location; struct FSName PingFeedbackDefName; }* p = (InPingParams*)Params;
				if (p && CVars.Debug)
				{
					printf("[PingDump] Target: %s | Location: %.1f, %.1f, %.1f\n", p->TargetedActor ? p->TargetedActor->GetName().c_str() : "None", p->Location.X, p->Location.Y, p->Location.Z);
					if (p->TargetedActor && p->TargetedActor->IsA(ACharacter::StaticClass()))
					{
						ACharacter* Char = (ACharacter*)p->TargetedActor;
						if (Char->Mesh)
						{
							int32 NumBones = Char->Mesh->GetNumBones();
							printf("    [Skeleton] Bones: %d\n", NumBones);
							for (int b = 0; b < NumBones; b++)
							{
								printf("      [%d] %s\n", b, Char->Mesh->GetBoneName(b).ToString().c_str());
							}
						}
					}
				}
			}


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
		
		if (Function && Object && WeaponSettings.InstantReload)
		{
			if (Function->GetName() == "ServerStartReloading")
			{
				SDK::AWeapon* weapon = (SDK::AWeapon*)Object;
				if (weapon && weapon->IsA(SDK::AWeapon::StaticClass()))
				{
					struct ReloadParams { uint8 UseModeIndex; uint8 Flags; }* p = (ReloadParams*)Params;
					int32 MaxAmmo = SDK::UWeaponStatics::GetMaxLoadedAmmo(weapon, p->UseModeIndex);
					
					weapon->ClientSetLoadedAmmo(p->UseModeIndex, MaxAmmo);
					weapon->ClientStopReloading();
					weapon->ServerInterruptReloadToUse(MaxAmmo);

					bInsideHook = false;
					g_ProcessEventCount.fetch_sub(1);
					return;
				}
			}
		}



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


				if (ModeStr.find("ThirdPerson") == std::string::npos && ModeStr != "None" && ModeStr != "Default")
				{
					if (MiscSettings.ThirdPersonOTS)

					{
						bInsideHook = false;
						g_ProcessEventCount.fetch_sub(1);
						return;
					}
				}
			}
		}


	}
}
catch (...) {


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

		static bool bF9WasDown = false;
		bool bF9IsDown = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
		if (bF9IsDown && !bF9WasDown)
		{
			g_IsRecording = !g_IsRecording;
			if (g_IsRecording)
			{
				std::lock_guard<std::mutex> lock(g_LogMutex);
				char path[MAX_PATH];
				GetModuleFileNameA(NULL, path, MAX_PATH);
				std::string dir = std::string(path).substr(0, std::string(path).find_last_of("\\/"));
				std::string logPath = dir + "\\EventLog_" + std::to_string((int)time(0)) + ".txt";
				
				g_RecordStream.open(logPath, std::ios::out);
				if (g_RecordStream.is_open())
					printf("[Recorder] STARTED recording to %s\n", logPath.c_str());
				else
					g_IsRecording = false;
			}
			else
			{
				std::lock_guard<std::mutex> lock(g_LogMutex);
				if (g_RecordStream.is_open())
				{
					g_RecordStream.close();
					printf("[Recorder] STOPPED recording.\n");
				}
			}
		}
		bF9WasDown = bF9IsDown;
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
		return false;

	}

	void** TempVTable = *reinterpret_cast<void***>(GVars.PlayerController);
	if (!TempVTable)
	{
		std::cout << "[Hook] ERROR: PlayerController VTable is null!\n";
		return false;
	}

	pcVTable = TempVTable;

	int processEventIdx = 73;


	if (TempVTable[processEventIdx] == &hkProcessEvent)
	{


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
		return true;

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

