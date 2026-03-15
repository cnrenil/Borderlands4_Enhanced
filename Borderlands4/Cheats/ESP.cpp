#include "pch.h"


int32 ViewportX = 0.0f;
int32 ViewportY = 0.0f;

auto RenderColor = IM_COL32(255, 255, 255, 255);

struct ESPActorCache {
	FVector2D TopScreen;
	FVector2D BottomScreen;
	ImU32 Color;
	float HealthPct;
	FString Name;
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

namespace
{
	struct ESPState
	{
		std::mutex Mutex;
		std::vector<ESPActorCache> CachedActors;
		std::vector<ESPTracerCache> CachedTracers;
	};

	ESPState& GetESPState()
	{
		static ESPState state;
		return state;
	}
}

struct BonePair { FName Parent; FName Child; };

static float SafeDistanceMeters(const FVector& A, const FVector& B)
{
	const double dx = (double)A.X - (double)B.X;
	const double dy = (double)A.Y - (double)B.Y;
	const double dz = (double)A.Z - (double)B.Z;
	return (float)(sqrt(dx * dx + dy * dy + dz * dz) / 100.0); // UE units -> meters
}

static bool ProjectForOverlay(const FVector& worldPos, FVector2D& outScreen)
{
	if (!GVars.PlayerController) return false;
	// Use absolute viewport coords to avoid vertical drift when UE applies letterboxing/constrained view.
	return GVars.PlayerController->ProjectWorldLocationToScreen(worldPos, &outScreen, false);
}

static bool IsOTSAdsActive()
{
	if (!ConfigManager::B("Player.ThirdPerson") && !ConfigManager::B("Player.OverShoulder")) return false;
	if (!GVars.Character || !GVars.Character->IsA(AOakCharacter::StaticClass())) return false;
	const AOakCharacter* oakChar = static_cast<AOakCharacter*>(GVars.Character);
	return ((uint8)oakChar->ZoomState.State != 0);
}

static ImVec2 GetCustomReticleScreenPos()
{
	if (!GVars.PlayerController || !GVars.PlayerController->PlayerCameraManager) {
		return ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
	}

	const FMinimalViewInfo& CameraPOV = GVars.PlayerController->PlayerCameraManager->CameraCachePrivate.POV;
	const FVector camLoc = CameraPOV.Location;
	const FVector camFwd = Utils::FRotatorToVector(CameraPOV.Rotation);
	const FVector aimPoint = camLoc + (camFwd * 50000.0f);

	FVector2D screen{};
	if (ProjectForOverlay(aimPoint, screen)) {
		return ImVec2((float)screen.X, (float)screen.Y);
	}

	return ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
}

void Cheats::UpdateESP()
{
	Logger::LogThrottled(Logger::Level::Debug, "ESP", 10000, "Cheats::UpdateESP() active");
	AActor* SelfActor = Utils::GetSelfActor();
	if (!ConfigManager::B("Player.ESP") || !Utils::bIsInGame || !GVars.PlayerController || !GVars.Level || !SelfActor || !GVars.World || !GVars.World->VTable)
	{
		auto& state = GetESPState();
		std::lock_guard<std::mutex> lock(state.Mutex);
		state.CachedActors.clear();
		state.CachedTracers.clear();
		return;
	}

	if (!GVars.PlayerController->PlayerCameraManager || !GVars.PlayerController->PlayerCameraManager->VTable)
	{
		auto& state = GetESPState();
		std::lock_guard<std::mutex> lock(state.Mutex);
		state.CachedActors.clear();
		state.CachedTracers.clear();
		return;
	}

	// Read camera location once by value; don't depend on GVars.POV pointer lifetime here.
	const FVector CameraLocation = GVars.PlayerController->PlayerCameraManager->CameraCachePrivate.POV.Location;
	
	std::vector<ESPActorCache> NewCache;

	for (ACharacter* TargetActor : GVars.UnitCache)
	{
		if (!TargetActor || !Utils::IsValidActor(TargetActor)) continue;

			if (TargetActor == SelfActor) continue;

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
			float Distance = SafeDistanceMeters(CameraLocation, ActorLocation);
			if (Distance < 0.0f || Distance > 1000.0f) continue;

		FVector TopPos;
		FVector BottomPos;
		bool bBoneBoundsFound = false;

		if (TargetActor->Mesh)
		{
			int HeadIdx = TargetActor->Mesh->GetBoneIndex(UKismetStringLibrary::Conv_StringToName(UtfN::StringToWString(CheatsData::BoneList.HeadBone).c_str()));
			int RootIdx = TargetActor->Mesh->GetBoneIndex(UKismetStringLibrary::Conv_StringToName(L"Root"));
			if (HeadIdx != -1)
			{
				FVector HeadPos = TargetActor->Mesh->GetBoneTransform(UKismetStringLibrary::Conv_StringToName(UtfN::StringToWString(CheatsData::BoneList.HeadBone).c_str()), ERelativeTransformSpace::RTS_World).Translation;
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
		bool bVisible = ProjectForOverlay(TopPos, TopScreen) &&
						ProjectForOverlay(BottomPos, BottomScreen);

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
				Cache.Name = UKismetSystemLibrary::GetDisplayName(TargetActor);
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
					if (ProjectForOverlay(P1, S1) &&
						ProjectForOverlay(P2, S2)) {
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
		std::lock_guard<std::mutex> lock(CheatsData::TracerMutex);
		float CurrentTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		
		const FMinimalViewInfo& CameraPOV = GVars.PlayerController->PlayerCameraManager->CameraCachePrivate.POV;
		FVector CamLoc = CameraPOV.Location;
		FVector CamFwd = Utils::FRotatorToVector(CameraPOV.Rotation);

		for (auto it = CheatsData::BulletTracers.begin(); it != CheatsData::BulletTracers.end(); )
		{
			if (CurrentTime - it->CreationTime > ConfigManager::F("ESP.TracerDuration"))
			{
				it = CheatsData::BulletTracers.erase(it);
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
				bool bP1Visible = ProjectForOverlay(P1, p1Screen);

				if (i + 1 < PointsCount)
				{
					FVector P2 = it->Points[i+1];
					FVector2D p2Screen;
					bool bP2Visible = ProjectForOverlay(P2, p2Screen);
					
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
							ProjectForOverlay(P1_Final, p1Screen);
							ProjectForOverlay(P2_Final, p2Screen);
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
		auto& state = GetESPState();
		std::lock_guard<std::mutex> lock(state.Mutex);
		state.CachedActors = std::move(NewCache);
		state.CachedTracers = std::move(NewTracers);
	}
}

void Cheats::RenderESP()
{
	if (!ConfigManager::B("Player.ESP")) return;
	UCanvas* Canvas = Utils::GetCurrentCanvas();

	std::vector<ESPActorCache> LocalActors;
	std::vector<ESPTracerCache> LocalTracers;
	{
		auto& state = GetESPState();
		std::lock_guard<std::mutex> lock(state.Mutex);
		LocalActors = state.CachedActors;
		LocalTracers = state.CachedTracers;
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
				if (Canvas)
					Utils::DrawCanvasLine(Canvas, FVector2D(Line.first.x, Line.first.y), FVector2D(Line.second.x, Line.second.y), 2.0f, Utils::U32ToLinearColor(Actor.Color));
				else
					ImGui::GetBackgroundDrawList()->AddLine(Line.first, Line.second, Actor.Color, 2.0f);
			}
		}

		// Box
		if (ConfigManager::B("ESP.ShowBox"))
		{
			const FVector2D boxPos((float)Actor.TopScreen.X - Width / 2, (float)Actor.TopScreen.Y);
			const FVector2D boxSize(Width, Height);
			if (Canvas)
				Utils::DrawCanvasBox(Canvas, boxPos, boxSize, 1.0f, Utils::U32ToLinearColor(Actor.Color));
			else
				ImGui::GetBackgroundDrawList()->AddRect(
					ImVec2(boxPos.X, boxPos.Y),
					ImVec2(boxPos.X + boxSize.X, boxPos.Y + boxSize.Y),
					Actor.Color, 0.0f, 0, 1.0f
				);
		}

		// Health Bar
		float BarWidth = 4.0f;
		float BarHeight = Height;
		float BarX = (float)Actor.TopScreen.X - Width / 2 - 6.0f;
		float BarY = (float)Actor.TopScreen.Y;

		// Background
		if (Canvas)
			Utils::DrawCanvasFilledRect(Canvas, FVector2D(BarX, BarY), FVector2D(BarWidth, BarHeight), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
		else
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
		if (Canvas)
			Utils::DrawCanvasFilledRect(Canvas, FVector2D(BarX, BarY + BarHeight - FillHeight), FVector2D(BarWidth, FillHeight), Utils::U32ToLinearColor(HealthColor));
		else
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
			if (Canvas)
				Utils::DrawCanvasText(Canvas, DistanceText, FVector2D((float)Actor.TopScreen.X - Width / 2, (float)Actor.BottomScreen.Y + 2), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
			else
				ImGui::GetBackgroundDrawList()->AddText(
					ImVec2((float)Actor.TopScreen.X - Width / 2, (float)Actor.BottomScreen.Y + 2),
					IM_COL32(255, 255, 255, 255),
					DistanceText
				);
		}

		if (ConfigManager::B("ESP.ShowEnemyName"))
		{
			if (Canvas)
				Utils::DrawCanvasText(Canvas, Actor.Name, FVector2D((float)Actor.TopScreen.X - Width / 2, (float)Actor.TopScreen.Y - 15), Utils::U32ToLinearColor(Actor.Color));
			else
				ImGui::GetBackgroundDrawList()->AddText(
					ImVec2((float)Actor.TopScreen.X - Width / 2, (float)Actor.TopScreen.Y - 15),
					Actor.Color,
					Actor.Name.ToString().c_str()
				);
		}
	}

	if (ConfigManager::B("ESP.BulletTracers"))
	{
		for (const auto& Tracer : LocalTracers)
		{
			if (Tracer.bVisible) {
				if (Canvas)
				{
					Utils::DrawCanvasLine(Canvas, FVector2D(Tracer.Start.x, Tracer.Start.y), FVector2D(Tracer.End.x, Tracer.End.y), 6.0f, Utils::U32ToLinearColor(Tracer.ColorSegment));
					Utils::DrawCanvasLine(Canvas, FVector2D(Tracer.Start.x, Tracer.Start.y), FVector2D(Tracer.End.x, Tracer.End.y), 3.0f, Utils::U32ToLinearColor(Tracer.ColorGlow));
					Utils::DrawCanvasLine(Canvas, FVector2D(Tracer.Start.x, Tracer.Start.y), FVector2D(Tracer.End.x, Tracer.End.y), 1.0f, Utils::U32ToLinearColor(Tracer.ColorCore));
				}
				else
				{
					ImGui::GetBackgroundDrawList()->AddLine(Tracer.Start, Tracer.End, Tracer.ColorSegment, 6.0f);
					ImGui::GetBackgroundDrawList()->AddLine(Tracer.Start, Tracer.End, Tracer.ColorGlow, 3.0f);
					ImGui::GetBackgroundDrawList()->AddLine(Tracer.Start, Tracer.End, Tracer.ColorCore, 1.0f);
				}
			}

			if (Tracer.bImpact) {
				if (Canvas)
				{
					Utils::DrawCanvasCircle(Canvas, FVector2D(Tracer.ImpactPos.x, Tracer.ImpactPos.y), 6.5f, 16, 2.0f, Utils::U32ToLinearColor(Tracer.ColorImpactOuter));
					Utils::DrawCanvasCircle(Canvas, FVector2D(Tracer.ImpactPos.x, Tracer.ImpactPos.y), 4.0f, 16, 2.0f, Utils::U32ToLinearColor(Tracer.ColorImpactInner));
				}
				else
				{
					ImGui::GetBackgroundDrawList()->AddCircleFilled(Tracer.ImpactPos, 6.5f, Tracer.ColorImpactOuter);
					ImGui::GetBackgroundDrawList()->AddCircleFilled(Tracer.ImpactPos, 4.0f, Tracer.ColorImpactInner);
				}
			}
		}
	}

	if (IsOTSAdsActive())
	{
		const ImVec2 center = GetCustomReticleScreenPos();
		const ImU32 outer = IM_COL32(0, 0, 0, 180);
		const ImU32 inner = IM_COL32(255, 255, 255, 255);
		const float gap = 3.0f;
		const float len = 8.0f;
		if (Canvas)
		{
			const FVector2D centerVec(center.x, center.y);
			Utils::DrawCanvasCircle(Canvas, centerVec, 2.0f, 12, 2.0f, Utils::U32ToLinearColor(outer));
			Utils::DrawCanvasCircle(Canvas, centerVec, 1.0f, 12, 1.5f, Utils::U32ToLinearColor(inner));
			Utils::DrawCanvasLine(Canvas, FVector2D(center.x - gap - len, center.y), FVector2D(center.x - gap, center.y), 2.5f, Utils::U32ToLinearColor(outer));
			Utils::DrawCanvasLine(Canvas, FVector2D(center.x + gap, center.y), FVector2D(center.x + gap + len, center.y), 2.5f, Utils::U32ToLinearColor(outer));
			Utils::DrawCanvasLine(Canvas, FVector2D(center.x, center.y - gap - len), FVector2D(center.x, center.y - gap), 2.5f, Utils::U32ToLinearColor(outer));
			Utils::DrawCanvasLine(Canvas, FVector2D(center.x, center.y + gap), FVector2D(center.x, center.y + gap + len), 2.5f, Utils::U32ToLinearColor(outer));
			Utils::DrawCanvasLine(Canvas, FVector2D(center.x - gap - len, center.y), FVector2D(center.x - gap, center.y), 1.2f, Utils::U32ToLinearColor(inner));
			Utils::DrawCanvasLine(Canvas, FVector2D(center.x + gap, center.y), FVector2D(center.x + gap + len, center.y), 1.2f, Utils::U32ToLinearColor(inner));
			Utils::DrawCanvasLine(Canvas, FVector2D(center.x, center.y - gap - len), FVector2D(center.x, center.y - gap), 1.2f, Utils::U32ToLinearColor(inner));
			Utils::DrawCanvasLine(Canvas, FVector2D(center.x, center.y + gap), FVector2D(center.x, center.y + gap + len), 1.2f, Utils::U32ToLinearColor(inner));
		}
		else
		{
			auto* draw = ImGui::GetBackgroundDrawList();
			draw->AddCircle(center, 2.0f, outer, 12, 2.0f);
			draw->AddCircle(center, 1.0f, inner, 12, 1.5f);
			draw->AddLine(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y), outer, 2.5f);
			draw->AddLine(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y), outer, 2.5f);
			draw->AddLine(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap), outer, 2.5f);
			draw->AddLine(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len), outer, 2.5f);
			draw->AddLine(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y), inner, 1.2f);
			draw->AddLine(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y), inner, 1.2f);
			draw->AddLine(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap), inner, 1.2f);
			draw->AddLine(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len), inner, 1.2f);
		}
	}
}
