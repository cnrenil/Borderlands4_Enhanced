#include "pch.h"

bool Init = false;

void Cheats::Aimbot()
{
    bHasAimbotTarget = false;
	if (!ConfigManager::B("Aimbot.Enabled") || Utils::bIsLoading) return;

	if (!GVars.POV || !GVars.PlayerController || !GVars.Level || !GVars.Character) return;

	// Aimbot key check (using SDK/Engine safe method if possible, or sticking to current for logic)
	bool AimbotKeyDown = ImGui::IsKeyDown((ImGuiKey)ConfigManager::I("Aimbot.Key"));

	if (ConfigManager::B("Aimbot.RequireKeyHeld") && !AimbotKeyDown)
		return;

	AActor* Target = Utils::GetBestTarget(
		GVars.PlayerController,
		ConfigManager::F("Aimbot.MaxFOV"),
		ConfigManager::B("Aimbot.LOS"),
		ConfigManager::S("Aimbot.Bone"),
		ConfigManager::B("Aimbot.TargetAll")
	);

	if (!Target || !Target->IsA(ACharacter::StaticClass())) return;

	ACharacter* TargetChar = reinterpret_cast<ACharacter*>(Target);

	static std::string CachedBoneString = "";
	static FName CachedBoneName;
	if (CachedBoneString != ConfigManager::S("Aimbot.Bone")) {
		std::wstring WideString = UtfN::StringToWString(ConfigManager::S("Aimbot.Bone"));
		CachedBoneName = UKismetStringLibrary::Conv_StringToName(WideString.c_str());
		CachedBoneString = ConfigManager::S("Aimbot.Bone");
	}

	FVector CameraPos = GVars.POV->Location;
	if (!TargetChar->Mesh) return;

	FVector TargetPos;
	if (TargetChar->Mesh->GetBoneIndex(CachedBoneName) != -1)
	{
		TargetPos = TargetChar->Mesh->GetBoneTransform(CachedBoneName, ERelativeTransformSpace::RTS_World).Translation;
	}
	else
	{
		TargetPos = Utils::GetHighestBone(TargetChar);
	}

	double Dist = CameraPos.GetDistanceToInMeters(TargetPos);

	if (ConfigManager::F("Aimbot.MinDistance") > Dist || Dist > ConfigManager::F("Aimbot.MaxDistance"))
		return;

    // Cache the target for the rendering thread
    bHasAimbotTarget = true;
    AimbotTargetPos = TargetPos;

	// Simple target rotation calculation
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

