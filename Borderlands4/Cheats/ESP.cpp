#include "pch.h"

int32 ViewportX = 0.0f;
int32 ViewportY = 0.0f;

auto RenderColor = IM_COL32(255, 255, 255, 255);

struct ESPActorCache {
	FVector2D TopScreen;
	FVector2D BottomScreen;
	ImU32 Color;
	float HealthPct;
	std::string Name;
	float Distance;
	bool bValidScreen;
	std::vector<std::pair<ImVec2, ImVec2>> SkeletonLines;
};

struct ESPTracerCache {
	ImVec2 Start;
	ImVec2 End;
	ImU32 ColorSegment;
	ImU32 ColorGlow;
	ImU32 ColorCore;
	bool bVisible;
	bool bImpact;
	ImVec2 ImpactPos;
	ImU32 ColorImpactOuter;
	ImU32 ColorImpactInner;
};

std::mutex ESPMutex;
std::vector<ESPActorCache> CachedESPActors;
std::vector<ESPTracerCache> CachedESPTracers;

struct BonePair { FName Parent; FName Child; };

void Cheats::UpdateESP()
{
	Logger::LogThrottled(Logger::Level::Info, "ESP", 5000, "Cheats::UpdateESP() active");
	if (!ConfigManager::B("Player.ESP") || Utils::bIsLoading || !GVars.PlayerController || !GVars.Level || !GVars.Character || !GVars.World || !GVars.World->VTable)
	{
		std::lock_guard<std::mutex> lock(ESPMutex);
		CachedESPActors.clear();
		CachedESPTracers.clear();
		return;
	}

	std::vector<ESPActorCache> NewCache;

	for (ACharacter* TargetActor : GVars.UnitCache)
	{
		if (!TargetActor || !Utils::IsValidActor(TargetActor)) continue;

		if (TargetActor == GVars.Character) continue;

		// Check attitude and skip if friendly and setting is off
		ETeamAttitude Attitude = Utils::GetAttitude(TargetActor);
		if (Attitude == ETeamAttitude::Friendly && !ConfigManager::B("ESP.ShowTeam")) continue;

		// Check health - skip dead
		float HealthPct = Utils::GetHealthPercent(TargetActor);
		if (HealthPct <= 0.0f) continue;

		// Determine color
		ImU32 Color = Utils::ConvertImVec4toU32(ConfigManager::Color("ESP.EnemyColor"));
		if (Attitude == ETeamAttitude::Friendly) Color = Utils::ConvertImVec4toU32(ConfigManager::Color("ESP.TeamColor"));
		else if (Attitude == ETeamAttitude::Neutral) Color = IM_COL32(255, 255, 0, 255); 

		FVector ActorLocation = TargetActor->K2_GetActorLocation();
		
		// Distance check
		float Distance = GVars.POV->Location.GetDistanceToInMeters(ActorLocation);
		if (Distance < 0.0f || Distance > 1000.0f) continue;

		FVector TopPos;
		FVector BottomPos;
		bool bBoneBoundsFound = false;

		if (TargetActor->Mesh)
		{
			int HeadIdx = TargetActor->Mesh->GetBoneIndex(UKismetStringLibrary::Conv_StringToName(UtfN::StringToWString(BoneList.HeadBone).c_str()));
			int RootIdx = TargetActor->Mesh->GetBoneIndex(UKismetStringLibrary::Conv_StringToName(L"Root"));
			if (HeadIdx != -1)
			{
				FVector HeadPos = TargetActor->Mesh->GetBoneTransform(UKismetStringLibrary::Conv_StringToName(UtfN::StringToWString(BoneList.HeadBone).c_str()), ERelativeTransformSpace::RTS_World).Translation;
				TopPos = HeadPos + FVector(0, 0, 20.0f); // Give head some padding
				
				if (RootIdx != -1) {
					BottomPos = TargetActor->Mesh->GetBoneTransform(UKismetStringLibrary::Conv_StringToName(L"Root"), ERelativeTransformSpace::RTS_World).Translation;
				} else {
					BottomPos = TargetActor->K2_GetActorLocation() - FVector(0, 0, 90.0f);
				}
				bBoneBoundsFound = true;
			}
		}

		if (!bBoneBoundsFound)
		{
			FVector Origin, Extent;
			TargetActor->GetActorBounds(false, &Origin, &Extent, false);
			Extent.X *= 0.5f; 
			Extent.Y *= 0.5f;

			TopPos = Origin + FVector(0, 0, Extent.Z);
			BottomPos = Origin - FVector(0, 0, Extent.Z);
		}

		FVector2D TopScreen, BottomScreen;
		bool bVisible = GVars.PlayerController->ProjectWorldLocationToScreen(TopPos, &TopScreen, true) &&
						GVars.PlayerController->ProjectWorldLocationToScreen(BottomPos, &BottomScreen, true);

		if (bVisible)
		{
			float ExtentHeight = abs(TopScreen.Y - BottomScreen.Y);
			float BoxWidth = ExtentHeight * 0.55f; // Box aspect ratio

			ESPActorCache Cache;
			Cache.bValidScreen = true;
			Cache.TopScreen = TopScreen;
			Cache.BottomScreen = BottomScreen;
			Cache.Color = Color;
			Cache.HealthPct = HealthPct;
			Cache.Distance = Distance;
			if (ConfigManager::B("ESP.ShowEnemyName"))
			{
				Cache.Name = UKismetSystemLibrary::GetDisplayName(TargetActor).ToString();
			}

			// Don't draw skeletons for far away targets to save FPS (Skeletal LOD)
			if (ConfigManager::B("ESP.Bones") && TargetActor->Mesh && Distance < 70.0f) // 70m limit
			{
				auto GetCachedBone = [&](const std::string& name) -> FName {
					return UKismetStringLibrary::Conv_StringToName(UtfN::StringToWString(name).c_str());
				};

				std::vector<BonePair> BonesToDraw = {
					{GetCachedBone("Hips"), GetCachedBone("Spine1")},
					{GetCachedBone("Spine1"), GetCachedBone("Spine2")},
					{GetCachedBone("Spine2"), GetCachedBone("Spine3")},
					{GetCachedBone("Spine3"), GetCachedBone("Neck")},
					{GetCachedBone("Neck"), GetCachedBone("Head")},
					// Left Arm
					{GetCachedBone("Neck"), GetCachedBone("L_Upperarm")},
					{GetCachedBone("L_Upperarm"), GetCachedBone("L_Forearm")},
					{GetCachedBone("L_Forearm"), GetCachedBone("L_Hand")},
					// Right Arm
					{GetCachedBone("Neck"), GetCachedBone("R_Upperarm")},
					{GetCachedBone("R_Upperarm"), GetCachedBone("R_Forearm")},
					{GetCachedBone("R_Forearm"), GetCachedBone("R_Hand")},
					// Left Leg
					{GetCachedBone("Hips"), GetCachedBone("L_Thigh")},
					{GetCachedBone("L_Thigh"), GetCachedBone("L_Shin")},
					{GetCachedBone("L_Shin"), GetCachedBone("L_Foot")},
					// Right Leg
					{GetCachedBone("Hips"), GetCachedBone("R_Thigh")},
					{GetCachedBone("R_Thigh"), GetCachedBone("R_Shin")},
					{GetCachedBone("R_Shin"), GetCachedBone("R_Foot")}
				};

				for (const auto& bp : BonesToDraw)
				{
					int32 Idx1 = TargetActor->Mesh->GetBoneIndex(bp.Parent);
					int32 Idx2 = TargetActor->Mesh->GetBoneIndex(bp.Child);
					
					if (Idx1 == -1 || Idx2 == -1) continue;

					FTransform T1 = TargetActor->Mesh->GetBoneTransform(bp.Parent, ERelativeTransformSpace::RTS_World);
					FTransform T2 = TargetActor->Mesh->GetBoneTransform(bp.Child, ERelativeTransformSpace::RTS_World);
					
					FVector P1 = T1.Translation;
					FVector P2 = T2.Translation;

					FVector2D S1, S2;
					if (GVars.PlayerController->ProjectWorldLocationToScreen(P1, &S1, true) && 
						GVars.PlayerController->ProjectWorldLocationToScreen(P2, &S2, true)) {
						Cache.SkeletonLines.push_back({ImVec2((float)S1.X, (float)S1.Y), ImVec2((float)S2.X, (float)S2.Y)});
					}
				}
			}

			NewCache.push_back(Cache);
		}
	}

	std::vector<ESPTracerCache> NewTracers;
	if (ConfigManager::B("ESP.BulletTracers") && GVars.PlayerController && GVars.PlayerController->PlayerCameraManager)
	{
		std::lock_guard<std::mutex> lock(TracerMutex);
		float CurrentTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		
		FVector CamLoc = GVars.PlayerController->PlayerCameraManager->GetCameraLocation();
		FVector CamFwd = GVars.PlayerController->PlayerCameraManager->GetActorForwardVector();

		for (auto it = BulletTracersList.begin(); it != BulletTracersList.end(); )
		{
			if (CurrentTime - it->CreationTime > ConfigManager::F("ESP.TracerDuration"))
			{
				it = BulletTracersList.erase(it);
				continue;
			}
			
			float Age = CurrentTime - it->CreationTime;
			float FadeRatio = 1.0f - (Age / ConfigManager::F("ESP.TracerDuration"));
			if (FadeRatio < 0.0f) FadeRatio = 0.0f;
			
			ImVec4 BaseColor;
			if (ConfigManager::B("ESP.TracerRainbow"))
			{
				float Hue = fmodf(it->CreationTime * 0.5f, 1.0f);
				float R, G, B;
				ImGui::ColorConvertHSVtoRGB(Hue, 1.0f, 1.0f, R, G, B);
				BaseColor = ImVec4(R, G, B, FadeRatio);
			}
			else 
			{
				BaseColor = ImVec4(ConfigManager::Color("ESP.TracerColor").x, ConfigManager::Color("ESP.TracerColor").y, ConfigManager::Color("ESP.TracerColor").z, FadeRatio);
			}

			size_t PointsCount = it->Points.size();

			for (size_t i = 0; i < PointsCount; i++)
			{
				FVector P1 = it->Points[i];
				FVector2D p1Screen;
				bool bP1Visible = GVars.PlayerController->ProjectWorldLocationToScreen(P1, &p1Screen, true);

				if (i + 1 < PointsCount)
				{
					FVector P2 = it->Points[i+1];
					FVector2D p2Screen;
					bool bP2Visible = GVars.PlayerController->ProjectWorldLocationToScreen(P2, &p2Screen, true);
					
					FVector P1_Final = P1;
					FVector P2_Final = P2;
					bool bCanDraw = false;

					if (bP1Visible && bP2Visible)
					{
						bCanDraw = true;
					}
					else if (bP1Visible || bP2Visible) 
					{
						auto GetDist = [&](const FVector& P) { return ((P.X - CamLoc.X) * CamFwd.X + (P.Y - CamLoc.Y) * CamFwd.Y + (P.Z - CamLoc.Z) * CamFwd.Z); };
						float d1 = GetDist(P1);
						float d2 = GetDist(P2);
						float epsilon = 1.0f;

						if ((d1 < epsilon && d2 >= epsilon) || (d2 < epsilon && d1 >= epsilon))
						{
							float t = (epsilon - d1) / (d2 - d1);
							FVector ClippedPoint = P1 + (P2 - P1) * t;
							if (d1 < epsilon) P1_Final = ClippedPoint;
							else P2_Final = ClippedPoint;
							GVars.PlayerController->ProjectWorldLocationToScreen(P1_Final, &p1Screen, true);
							GVars.PlayerController->ProjectWorldLocationToScreen(P2_Final, &p2Screen, true);
							bCanDraw = true;
						}
					}

					if (bCanDraw)
					{
						ESPTracerCache TC;
						TC.bVisible = true;
						TC.Start = ImVec2((float)p1Screen.X, (float)p1Screen.Y);
						TC.End = ImVec2((float)p2Screen.X, (float)p2Screen.Y);
						TC.ColorSegment = ImGui::ColorConvertFloat4ToU32(ImVec4(BaseColor.x, BaseColor.y, BaseColor.z, BaseColor.w * 0.2f));
						TC.ColorGlow = ImGui::ColorConvertFloat4ToU32(ImVec4(BaseColor.x, BaseColor.y, BaseColor.z, BaseColor.w * 0.5f));
						TC.ColorCore = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, BaseColor.w));
						
						if (i > 0 && bP1Visible && (it->bClosed || i < PointsCount - 1))
						{
							TC.bImpact = true;
							TC.ImpactPos = ImVec2((float)p1Screen.X, (float)p1Screen.Y);
							TC.ColorImpactOuter = ImGui::ColorConvertFloat4ToU32(ImVec4(BaseColor.x, BaseColor.y, BaseColor.z, BaseColor.w * 0.3f));
							TC.ColorImpactInner = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, BaseColor.w));
						} else {
							TC.bImpact = false;
						}
						NewTracers.push_back(TC);
					}
				}
				else if (i > 0 && bP1Visible && it->bClosed)
				{
					ESPTracerCache TC;
					TC.bVisible = false;
					TC.bImpact = true;
					TC.ImpactPos = ImVec2((float)p1Screen.X, (float)p1Screen.Y);
					TC.ColorImpactOuter = ImGui::ColorConvertFloat4ToU32(ImVec4(BaseColor.x, BaseColor.y, BaseColor.z, BaseColor.w * 0.3f));
					TC.ColorImpactInner = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, BaseColor.w));
					NewTracers.push_back(TC);
				}
			}
			++it;
		}
	}

	{
		std::lock_guard<std::mutex> lock(ESPMutex);
		CachedESPActors = std::move(NewCache);
		CachedESPTracers = std::move(NewTracers);
	}
}

void Cheats::RenderESP()
{
	if (!ConfigManager::B("Player.ESP")) return;

	std::vector<ESPActorCache> LocalActors;
	std::vector<ESPTracerCache> LocalTracers;
	{
		std::lock_guard<std::mutex> lock(ESPMutex);
		LocalActors = CachedESPActors;
		LocalTracers = CachedESPTracers;
	}

	for (const auto& Actor : LocalActors)
	{
		float Height = abs((float)Actor.BottomScreen.Y - (float)Actor.TopScreen.Y);
		float Width = Height * 0.6f;

		// Skeleton
		if (ConfigManager::B("ESP.Bones") && Actor.SkeletonLines.size() > 0)
		{
			for (const auto& Line : Actor.SkeletonLines)
			{
				ImGui::GetBackgroundDrawList()->AddLine(Line.first, Line.second, Actor.Color, 2.0f);
			}
		}

		// Box
		if (ConfigManager::B("ESP.ShowBox"))
		{
			ImGui::GetBackgroundDrawList()->AddRect(
				ImVec2((float)Actor.TopScreen.X - Width / 2, (float)Actor.TopScreen.Y),
				ImVec2((float)Actor.TopScreen.X + Width / 2, (float)Actor.BottomScreen.Y),
				Actor.Color, 0.0f, 0, 1.0f
			);
		}

		// Health Bar
		float BarWidth = 4.0f;
		float BarHeight = Height;
		float BarX = (float)Actor.TopScreen.X - Width / 2 - 6.0f;
		float BarY = (float)Actor.TopScreen.Y;

		// Background
		ImGui::GetBackgroundDrawList()->AddRectFilled(
			ImVec2(BarX, BarY),
			ImVec2(BarX + BarWidth, BarY + BarHeight),
			IM_COL32(0, 0, 0, 150)
		);

		// Health Fill
		ImU32 HealthColor = IM_COL32(0, 255, 0, 255);
		if (Actor.HealthPct < 0.3f) HealthColor = IM_COL32(255, 0, 0, 255);
		else if (Actor.HealthPct < 0.7f) HealthColor = IM_COL32(255, 255, 0, 255);

		float FillHeight = BarHeight * Actor.HealthPct;
		ImGui::GetBackgroundDrawList()->AddRectFilled(
			ImVec2(BarX, BarY + BarHeight - FillHeight),
			ImVec2(BarX + BarWidth, BarY + BarHeight),
			HealthColor
		);

		// Distance and Name
		if (ConfigManager::B("ESP.ShowEnemyDistance"))
		{
			char DistanceText[32];
			snprintf(DistanceText, sizeof(DistanceText), "%.0f m", Actor.Distance);
			ImGui::GetBackgroundDrawList()->AddText(
				ImVec2((float)Actor.TopScreen.X - Width / 2, (float)Actor.BottomScreen.Y + 2),
				IM_COL32(255, 255, 255, 255),
				DistanceText
			);
		}

		if (ConfigManager::B("ESP.ShowEnemyName"))
		{
			ImGui::GetBackgroundDrawList()->AddText(
				ImVec2((float)Actor.TopScreen.X - Width / 2, (float)Actor.TopScreen.Y - 15),
				Actor.Color,
				Actor.Name.c_str()
			);
		}
	}

	if (ConfigManager::B("ESP.BulletTracers"))
	{
		for (const auto& Tracer : LocalTracers)
		{
			if (Tracer.bVisible) {
				ImGui::GetBackgroundDrawList()->AddLine(Tracer.Start, Tracer.End, Tracer.ColorSegment, 6.0f);
				ImGui::GetBackgroundDrawList()->AddLine(Tracer.Start, Tracer.End, Tracer.ColorGlow, 3.0f);
				ImGui::GetBackgroundDrawList()->AddLine(Tracer.Start, Tracer.End, Tracer.ColorCore, 1.0f);
			}

			if (Tracer.bImpact) {
				ImGui::GetBackgroundDrawList()->AddCircleFilled(Tracer.ImpactPos, 6.5f, Tracer.ColorImpactOuter);
				ImGui::GetBackgroundDrawList()->AddCircleFilled(Tracer.ImpactPos, 4.0f, Tracer.ColorImpactInner);
			}
		}
	}

	if (ConfigManager::B("Player.ThirdPerson") && (ConfigManager::B("Misc.ThirdPersonOTS") || ConfigManager::B("Misc.ThirdPersonCentered")) && GVars.Character && GVars.Character->IsA(SDK::AOakCharacter::StaticClass()))
	{
		SDK::AOakCharacter* OakChar = static_cast<SDK::AOakCharacter*>(GVars.Character);
		if ((uint8)OakChar->ZoomState.State != 0)
		{
			ImVec2 Center = ImVec2(ImGui::GetIO().DisplaySize.x / 2.0f, ImGui::GetIO().DisplaySize.y / 2.0f);
			ImGui::GetBackgroundDrawList()->AddLine(ImVec2(Center.x - 5, Center.y), ImVec2(Center.x + 5, Center.y), IM_COL32(255, 255, 255, 255), 1.0f);
			ImGui::GetBackgroundDrawList()->AddLine(ImVec2(Center.x, Center.y - 5), ImVec2(Center.x, Center.y + 5), IM_COL32(255, 255, 255, 255), 1.0f);
		}
	}
}
