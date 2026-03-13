#include "pch.h"

bool Init = false;

void Cheats::Aimbot()
{
	if (!ConfigManager::B("Aimbot.Enabled") || Utils::bIsLoading) return;

	if (ConfigManager::B("Aimbot.DrawFOV"))
		Utils::DrawFOV(ConfigManager::F("Aimbot.MaxFOV"), ConfigManager::F("Aimbot.FOVThickness"));

	if (!GVars.POV || !GVars.PlayerController || !GVars.Level || !GVars.Character) return;

	// Aimbot key check (if required)
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
		// Fallback to the highest bone if the requested bone doesn't exist (e.g., drones, turrets)
		TargetPos = Utils::GetHighestBone(TargetChar);
	}

	double Dist = CameraPos.GetDistanceToInMeters(TargetPos);

	if (ConfigManager::F("Aimbot.MinDistance") > Dist || Dist > ConfigManager::F("Aimbot.MaxDistance"))
		return;

	if (ConfigManager::B("Aimbot.DrawArrow"))
		Utils::DrawSnapLine(TargetPos, ConfigManager::F("Aimbot.ArrowThickness"));

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
