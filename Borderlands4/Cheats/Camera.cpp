#include "pch.h"

void Cheats::ToggleThirdPerson() { ConfigManager::B("Player.ThirdPerson") = !ConfigManager::B("Player.ThirdPerson"); if (ConfigManager::B("Player.ThirdPerson")) ConfigManager::B("Player.Freecam") = false; }
void Cheats::ToggleFreecam() { ConfigManager::B("Player.Freecam") = !ConfigManager::B("Player.Freecam"); if (ConfigManager::B("Player.Freecam")) ConfigManager::B("Player.ThirdPerson") = false; }

void Cheats::ChangeFOV()
{
	if (!GVars.PlayerController) return;
	static float LastFOV = -1.0f;
	if (ConfigManager::B("Misc.EnableFOV")) {
		if (LastFOV != ConfigManager::F("Misc.FOV")) {
			GVars.PlayerController->fov(ConfigManager::F("Misc.FOV"));
			LastFOV = ConfigManager::F("Misc.FOV");
		}
	} else LastFOV = -1.0f;

	if (!GVars.Character || !GVars.World) return;
	SDK::AOakPlayerController* OakPC = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
	SDK::AOakCharacter* OakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);

	if (ConfigManager::B("Player.Freecam") && GVars.CameraActor) {
		float FlySpeed = 20.0f;
		if (GetAsyncKeyState(VK_CONTROL) & 0x8000) FlySpeed *= 3.0f;
		SDK::FVector CamLoc = GVars.CameraActor->K2_GetActorLocation();
		SDK::FRotator CamRot = OakPC->GetControlRotation();
		SDK::FVector Forward = Utils::FRotatorToVector(CamRot);
		if (GetAsyncKeyState('W') & 0x8000) CamLoc = CamLoc + (Forward * FlySpeed);
		if (GetAsyncKeyState('S') & 0x8000) CamLoc = CamLoc - (Forward * FlySpeed);
		GVars.CameraActor->K2_SetActorLocation(CamLoc, false, nullptr, false);
        GVars.CameraActor->K2_SetActorRotation(CamRot, false);
		if (OakPC->PlayerCameraManager->ViewTarget.target != GVars.CameraActor)
			OakPC->SetViewTargetWithBlend(GVars.CameraActor, 0.1f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
	} else if (ConfigManager::B("Player.ThirdPerson")) {
		if (OakPC->PlayerCameraManager->ViewTarget.target != OakChar)
			OakPC->SetViewTargetWithBlend(OakChar, 0.0f, SDK::EViewTargetBlendFunction::VTBlend_Linear, 0.0f, false);
	}
}

void Cheats::ChangeGameRenderSettings()
{
	if (!GVars.PlayerController) return;
	static bool ShouldDisableClouds = false;
	if (ConfigManager::B("Misc.DisableVolumetricClouds") != ShouldDisableClouds) {
		ShouldDisableClouds = ConfigManager::B("Misc.DisableVolumetricClouds");
		SDK::FString cmd = ShouldDisableClouds ? L"r.VolumetricCloud 0" : L"r.VolumetricCloud 1";
		SDK::UKismetSystemLibrary::ExecuteConsoleCommand(Utils::GetWorldSafe(), cmd, GVars.PlayerController);
	}
}

void Cheats::UpdateCamera()
{
    Cheats::ChangeFOV();
    Cheats::ChangeGameRenderSettings();
}

bool Cheats::HandleCameraEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
    if (!Object->IsA(SDK::APlayerCameraManager::StaticClass())) return false;
    const std::string FuncName = Function->GetName();

    if (ConfigManager::B("Player.ThirdPerson") && ConfigManager::B("Misc.ThirdPersonOTS") && FuncName == "BlueprintUpdateCamera") {
        auto p = (SDK::Params::PlayerCameraManager_BlueprintUpdateCamera*)Params;
        auto PCM = (SDK::APlayerCameraManager*)Object;
        SDK::FRotator Rot = PCM->GetCameraRotation();
        SDK::FVector Forward, Right, Up;
        SDK::UKismetMathLibrary::GetAxes(Rot, &Forward, &Right, &Up);
        SDK::FVector Offset = (Forward * ConfigManager::F("Misc.OTS_X")) + (Right * ConfigManager::F("Misc.OTS_Y")) + (Up * ConfigManager::F("Misc.OTS_Z"));
        p->NewCameraLocation = p->NewCameraLocation + Offset;
        p->ReturnValue = true;
        PCM->CameraCachePrivate.POV.Location = p->NewCameraLocation;
        return true; 
    }
    return false;
}
