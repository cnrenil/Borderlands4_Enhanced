#include "pch.h"
#include "Engine.h"
#include "Cheats.h"

void Cheats::TriggerBot()
{
	if (!CVars.TriggerBot || Utils::bIsLoading || !GVars.PlayerController || !GVars.Character) return;

	// Check if in menu
	if (ImGui::GetIO().WantCaptureMouse) return;

	if (TriggerBotSettings.RequireKeyHeld)
	{
		if (!ImGui::IsKeyDown(TriggerBotSettings.TriggerKey) && !((GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0)) 
		{
			// If key is released, stop firing if we were
			static bool bForceStop = false;
			if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) // Only if physical button is up
			{
				// We don't want to interfere with manual firing, 
				// but we need to ensure the simulated click is released.
			}
			return;
		}
	}

	// Use the target selection from Utils with the shared Aimbot FOV
	AActor* Target = Utils::GetBestTarget(
		GVars.PlayerController,
		AimbotSettings.MaxFOV, 
		true, // Must have Line Of Sight
		TextVars.AimbotBone,
		TriggerBotSettings.TargetAll
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
