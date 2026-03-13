#include "pch.h"


void Cheats::TriggerBot()
{
	if (!ConfigManager::B("Trigger.Enabled") || Utils::bIsLoading || !GVars.PlayerController || !GVars.Character) return;

	// Check if in menu
	if (ImGui::GetIO().WantCaptureMouse) return;

	if (ConfigManager::B("Trigger.RequireKeyHeld"))
	{
		if (!ImGui::IsKeyDown((ImGuiKey)ConfigManager::I("Trigger.Key"))) 
		{
			return;
		}
	}

	// Use the target selection from Utils with the shared Aimbot FOV
	AActor* Target = Utils::GetBestTarget(
		GVars.PlayerController,
		ConfigManager::F("Aimbot.MaxFOV"), 
		true, // Must have Line Of Sight
		ConfigManager::S("Aimbot.Bone"),
		ConfigManager::B("Trigger.TargetAll")
	);

	static bool bIsFiringUnderControl = false;

	if (Target)
	{
		if (!bIsFiringUnderControl)
		{
			// Simulate Mouse Click Down
			INPUT Input = { 0 };
			Input.type = INPUT_MOUSE;
			Input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
			SendInput(1, &Input, sizeof(INPUT));
			bIsFiringUnderControl = true;
		}
	}
	else
	{
		if (bIsFiringUnderControl)
		{
			// Simulate Mouse Click Up
			INPUT Input = { 0 };
			Input.type = INPUT_MOUSE;
			Input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
			SendInput(1, &Input, sizeof(INPUT));
			bIsFiringUnderControl = false;
		}
	}
}
