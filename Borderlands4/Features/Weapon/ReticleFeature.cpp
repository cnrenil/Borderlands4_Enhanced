#include "pch.h"

namespace Features
{
	namespace
	{
		std::atomic<bool> g_LoggedReticleCanvas{ false };
		std::atomic<bool> g_LoggedReticleImGui{ false };

		bool IsOTSAdsActive()
		{
			if (!ConfigManager::B("Player.ThirdPerson") && !ConfigManager::B("Player.OverShoulder"))
				return false;
			if (!GVars.Character || !GVars.Character->IsA(AOakCharacter::StaticClass()))
				return false;
			const AOakCharacter* oakChar = static_cast<AOakCharacter*>(GVars.Character);
			return static_cast<uint8>(oakChar->ZoomState.State) != 0;
		}

		ImVec2 GetCustomReticleScreenPos()
		{
			if (!GVars.PlayerController || !GVars.PlayerController->PlayerCameraManager)
				return ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);

			const FMinimalViewInfo& cameraPOV = GVars.PlayerController->PlayerCameraManager->CameraCachePrivate.POV;
			const FVector camLoc = cameraPOV.Location;
			const FRotator camRot = cameraPOV.Rotation;
			const FVector camFwd = Utils::FRotatorToVector(camRot);
			const FVector aimPoint = camLoc + (camFwd * 50000.0f);

			FVector2D screen{};
			if (Utils::ProjectWorldLocationToScreen(aimPoint, screen, true))
				return ImVec2(static_cast<float>(screen.X), static_cast<float>(screen.Y));

			return ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
		}

		void DrawReticleFeature()
		{
			if (!ConfigManager::B("Misc.Reticle") || !IsOTSAdsActive())
				return;

			const ImVec2 offset = ConfigManager::Vec2("Misc.ReticlePosition");
			const ImVec2 baseCenter = GetCustomReticleScreenPos();
			const ImVec2 center(baseCenter.x + offset.x, baseCenter.y + offset.y);
			const ImVec4 reticleColor = ConfigManager::Color("Misc.ReticleColor");
			const ImU32 inner = ImGui::ColorConvertFloat4ToU32(reticleColor);
			const ImU32 outer = IM_COL32(0, 0, 0, static_cast<int>(reticleColor.w * 180.0f));
			const float size = std::clamp(ConfigManager::F("Misc.ReticleSize"), 2.0f, 30.0f);
			const float gap = size * 0.45f;
			const float len = size * 1.2f;
			const float dotOuterRadius = (std::max)(1.5f, size * 0.32f);
			const float dotInnerRadius = (std::max)(1.0f, size * 0.16f);
			const bool drawCrosshair = ConfigManager::B("Misc.CrossReticle");

			if (GUI::Draw::ResolveBackend() == GUI::Draw::Backend::UCanvas)
			{
				if (!g_LoggedReticleCanvas.exchange(true))
				{
					LOG_INFO("DrawPath", "DrawReticle using UCanvas path.");
				}
			}
			else if (!g_LoggedReticleImGui.exchange(true))
			{
				LOG_INFO("DrawPath", "DrawReticle using ImGui path.");
			}

			GUI::Draw::Circle(center, dotOuterRadius, outer, 16, 2.0f);
			GUI::Draw::Circle(center, dotInnerRadius, inner, 16, 1.5f);

			if (!drawCrosshair)
				return;

			GUI::Draw::Line(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y), outer, 2.5f);
			GUI::Draw::Line(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y), outer, 2.5f);
			GUI::Draw::Line(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap), outer, 2.5f);
			GUI::Draw::Line(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len), outer, 2.5f);
			GUI::Draw::Line(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y), inner, 1.2f);
			GUI::Draw::Line(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y), inner, 1.2f);
			GUI::Draw::Line(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap), inner, 1.2f);
			GUI::Draw::Line(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len), inner, 1.2f);
		}
	}

	void RegisterReticleFeature()
	{
		Core::Scheduler::RegisterRenderCallback("Reticle", []()
		{
			DrawReticleFeature();
		});
	}
}
