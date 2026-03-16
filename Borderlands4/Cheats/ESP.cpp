#include "pch.h"


int32 ViewportX = 0.0f;
int32 ViewportY = 0.0f;

auto RenderColor = IM_COL32(255, 255, 255, 255);

struct ESPActorCache {
	FVector2D TopScreen;
	FVector2D BottomScreen;
	FVector2D LeftTopScreen;
	FVector2D RightBottomScreen;
	ImU32 Color;
	float HealthPct;
	FString Name;
	float Distance;
	bool bValidScreen;
	std::vector<std::pair<ImVec2, ImVec2>> SkeletonLines;
};

struct ESPLootCache {
	FVector2D ScreenPos;
	ImU32 Color;
	FString Name;
	float Distance;
	bool bValidScreen;
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
		std::vector<ESPLootCache> CachedLoot;
		std::vector<ESPTracerCache> CachedTracers;
		std::vector<USkeletalMeshComponent*> HighlightedMeshes;
		uint64_t LastLootRefreshMs = 0;
	};

	ESPState& GetESPState()
	{
		static ESPState state;
		return state;
	}

	constexpr uint64_t kLootRefreshIntervalMs = 250;
	constexpr int32 kEnemyHighlightStencil = 252;
}

struct BonePair { FName Parent; FName Child; };

static bool ProjectForOverlay(const FVector& worldPos, FVector2D& outScreen)
{
	if (!GVars.PlayerController) return false;
	// Use player-viewport-relative coordinates so OTS/shadow-camera view rect matches UCanvas and ImGui overlay placement.
	return GVars.PlayerController->ProjectWorldLocationToScreen(worldPos, &outScreen, true);
}

static bool ProjectActorScreenBounds(AActor* actor, FVector2D& outTopScreen, FVector2D& outBottomScreen, FVector2D& outLeftTopScreen, FVector2D& outRightBottomScreen)
{
	if (!actor) return false;

	FVector origin;
	FVector extent;
	if (actor->IsA(ACharacter::StaticClass()))
	{
		ACharacter* character = static_cast<ACharacter*>(actor);
		if (!Utils::GetReliableMeshBounds(character, origin, extent))
			actor->GetActorBounds(false, &origin, &extent, false);
	}
	else
	{
		actor->GetActorBounds(false, &origin, &extent, false);
	}

	if (extent.X <= 0.0f || extent.Y <= 0.0f || extent.Z <= 0.0f)
		return false;

	const FVector corners[8] = {
		FVector(origin.X - extent.X, origin.Y - extent.Y, origin.Z - extent.Z),
		FVector(origin.X - extent.X, origin.Y - extent.Y, origin.Z + extent.Z),
		FVector(origin.X - extent.X, origin.Y + extent.Y, origin.Z - extent.Z),
		FVector(origin.X - extent.X, origin.Y + extent.Y, origin.Z + extent.Z),
		FVector(origin.X + extent.X, origin.Y - extent.Y, origin.Z - extent.Z),
		FVector(origin.X + extent.X, origin.Y - extent.Y, origin.Z + extent.Z),
		FVector(origin.X + extent.X, origin.Y + extent.Y, origin.Z - extent.Z),
		FVector(origin.X + extent.X, origin.Y + extent.Y, origin.Z + extent.Z),
	};

	bool hasProjectedPoint = false;
	float minX = FLT_MAX;
	float minY = FLT_MAX;
	float maxX = -FLT_MAX;
	float maxY = -FLT_MAX;

	for (const FVector& corner : corners)
	{
		FVector2D projected;
		if (!ProjectForOverlay(corner, projected))
			continue;

		hasProjectedPoint = true;
			minX = (std::min)(minX, static_cast<float>(projected.X));
			minY = (std::min)(minY, static_cast<float>(projected.Y));
			maxX = (std::max)(maxX, static_cast<float>(projected.X));
			maxY = (std::max)(maxY, static_cast<float>(projected.Y));
	}

	if (!hasProjectedPoint || minX >= maxX || minY >= maxY)
		return false;

	outLeftTopScreen = FVector2D(minX, minY);
	outRightBottomScreen = FVector2D(maxX, maxY);
	outTopScreen = FVector2D((minX + maxX) * 0.5f, minY);
	outBottomScreen = FVector2D((minX + maxX) * 0.5f, maxY);
	return true;
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

static void DrawEnemyIndicator(UCanvas* canvas, const ImVec2& screenCenter, float indicatorRadius, const ImVec2& targetScreenPos, ImU32 color)
{
	const ImVec2 delta(targetScreenPos.x - screenCenter.x, targetScreenPos.y - screenCenter.y);
	const float len = sqrtf(delta.x * delta.x + delta.y * delta.y);
	if (len <= indicatorRadius + 2.0f || len <= 0.001f)
		return;

	const ImVec2 dir(delta.x / len, delta.y / len);
	const ImVec2 perp(-dir.y, dir.x);
	const ImVec2 tip(screenCenter.x + dir.x * (indicatorRadius + 18.0f), screenCenter.y + dir.y * (indicatorRadius + 18.0f));
	const ImVec2 baseCenter(screenCenter.x + dir.x * (indicatorRadius + 6.0f), screenCenter.y + dir.y * (indicatorRadius + 6.0f));
	const ImVec2 p1 = tip;
	const ImVec2 p2(baseCenter.x + perp.x * 7.0f, baseCenter.y + perp.y * 7.0f);
	const ImVec2 p3(baseCenter.x - perp.x * 7.0f, baseCenter.y - perp.y * 7.0f);

	if (canvas)
	{
		const FLinearColor lineColor = Utils::U32ToLinearColor(color);
		Utils::DrawCanvasLine(canvas, FVector2D(p1.x, p1.y), FVector2D(p2.x, p2.y), 2.0f, lineColor);
		Utils::DrawCanvasLine(canvas, FVector2D(p2.x, p2.y), FVector2D(p3.x, p3.y), 2.0f, lineColor);
		Utils::DrawCanvasLine(canvas, FVector2D(p3.x, p3.y), FVector2D(p1.x, p1.y), 2.0f, lineColor);
	}
	else
	{
		ImGui::GetBackgroundDrawList()->AddTriangleFilled(p1, p2, p3, color);
	}
}

static bool IsValidHighlightMesh(USkeletalMeshComponent* Mesh)
{
	return Mesh && !IsBadReadPtr(Mesh, sizeof(void*)) && Mesh->VTable;
}

static void SetMeshHighlightState(USkeletalMeshComponent* Mesh, bool bEnabled)
{
	if (!IsValidHighlightMesh(Mesh))
		return;

	__try
	{
		Mesh->SetRenderCustomDepth(bEnabled);
		if (bEnabled)
		{
			Mesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
			Mesh->SetCustomDepthStencilValue(kEnemyHighlightStencil);
		}
		else
		{
			Mesh->SetCustomDepthStencilValue(0);
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}

static void ClearHighlightedMeshes(ESPState& State)
{
	for (USkeletalMeshComponent* Mesh : State.HighlightedMeshes)
	{
		SetMeshHighlightState(Mesh, false);
	}
	State.HighlightedMeshes.clear();
}

static void SyncHighlightedMeshes(ESPState& State, const std::vector<USkeletalMeshComponent*>& NextMeshes)
{
	std::unordered_set<USkeletalMeshComponent*> nextSet(NextMeshes.begin(), NextMeshes.end());
	std::unordered_set<USkeletalMeshComponent*> currentSet(State.HighlightedMeshes.begin(), State.HighlightedMeshes.end());

	for (USkeletalMeshComponent* Mesh : State.HighlightedMeshes)
	{
		if (nextSet.find(Mesh) == nextSet.end())
			SetMeshHighlightState(Mesh, false);
	}

	for (USkeletalMeshComponent* Mesh : NextMeshes)
	{
		if (currentSet.find(Mesh) == currentSet.end())
			SetMeshHighlightState(Mesh, true);
	}

	State.HighlightedMeshes = NextMeshes;
}

void Cheats::UpdateESP()
{
	Logger::LogThrottled(Logger::Level::Debug, "ESP", 10000, "Cheats::UpdateESP() active");
	AActor* SelfActor = Utils::GetSelfActor();
	if (!ConfigManager::B("Player.ESP") || !Utils::bIsInGame || !GVars.PlayerController || !GVars.Level || !SelfActor || !GVars.World || !GVars.World->VTable)
	{
		auto& state = GetESPState();
		std::lock_guard<std::mutex> lock(state.Mutex);
		ClearHighlightedMeshes(state);
		state.CachedActors.clear();
		state.CachedLoot.clear();
		state.CachedTracers.clear();
		state.LastLootRefreshMs = 0;
		return;
	}

	if (!GVars.PlayerController->PlayerCameraManager || !GVars.PlayerController->PlayerCameraManager->VTable)
	{
		auto& state = GetESPState();
		std::lock_guard<std::mutex> lock(state.Mutex);
		ClearHighlightedMeshes(state);
		state.CachedActors.clear();
		state.CachedLoot.clear();
		state.CachedTracers.clear();
		state.LastLootRefreshMs = 0;
		return;
	}

	std::vector<ESPActorCache> NewCache;
	std::vector<USkeletalMeshComponent*> NewHighlightedMeshes;
	std::unordered_set<USkeletalMeshComponent*> HighlightedMeshSet;
	const bool bMeshHighlightEnabled = ConfigManager::B("ESP.ShadedFill");

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

			const FVector ActorLocation = TargetActor->K2_GetActorLocation();
			
			// Distance check
			const float Distance = Utils::GetDistanceMeters(SelfActor, TargetActor);
			if (Distance < 0.0f || Distance > 1000.0f) continue;

		if (bMeshHighlightEnabled && TargetActor->Mesh && HighlightedMeshSet.insert(TargetActor->Mesh).second)
		{
			NewHighlightedMeshes.push_back(TargetActor->Mesh);
		}

		FVector2D TopScreen, BottomScreen, LeftTopScreen, RightBottomScreen;
		const bool bVisible = ProjectActorScreenBounds(TargetActor, TopScreen, BottomScreen, LeftTopScreen, RightBottomScreen);

		if (bVisible)
		{
			ESPActorCache Cache;
			Cache.bValidScreen = true;
			Cache.TopScreen = TopScreen;
			Cache.BottomScreen = BottomScreen;
			Cache.LeftTopScreen = LeftTopScreen;
			Cache.RightBottomScreen = RightBottomScreen;
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
	std::vector<ESPLootCache> NewLoot;
	if (ConfigManager::B("ESP.ShowLootName") && GVars.Level)
	{
		const uint64_t nowMs = GetTickCount64();
		bool shouldRefreshLoot = false;
		{
			auto& state = GetESPState();
			std::lock_guard<std::mutex> lock(state.Mutex);
			shouldRefreshLoot = state.LastLootRefreshMs == 0 || (nowMs - state.LastLootRefreshMs) >= kLootRefreshIntervalMs;
			if (!shouldRefreshLoot)
			{
				NewLoot = state.CachedLoot;
			}
		}

		if (shouldRefreshLoot)
		{
			float maxDistance = ConfigManager::F("ESP.LootMaxDistance");
			if (maxDistance <= 0.0f) maxDistance = 250.0f;
			const ImU32 lootColor = Utils::ConvertImVec4toU32(ConfigManager::Color("ESP.LootColor"));
			NewLoot.reserve(128);

			if (!Utils::ForEachLevelActor(GVars.Level, [&](AActor* Actor)
				{
					if (!Actor || !Utils::IsValidActor(Actor)) return true;
					if (!Actor->IsA(SDK::AInventoryPickup::StaticClass())) return true;

					const FVector actorLoc = Actor->K2_GetActorLocation();
					const float distance = Utils::GetDistanceMeters(SelfActor, Actor);
					if (distance < 0.0f || distance > maxDistance) return true;

					FVector2D screen;
					if (!ProjectForOverlay(actorLoc, screen)) return true;

					ESPLootCache Cache{};
					Cache.bValidScreen = true;
					Cache.ScreenPos = screen;
					Cache.Color = lootColor;
					Cache.Distance = distance;
					Cache.Name = UKismetSystemLibrary::GetDisplayName(Actor);
					NewLoot.push_back(std::move(Cache));
					return true;
				}))
			{
				Logger::LogThrottled(Logger::Level::Warning, "ESP", 2000, "Loot ESP skipped: Level->Actors unavailable");
				auto& state = GetESPState();
				std::lock_guard<std::mutex> lock(state.Mutex);
				NewLoot = state.CachedLoot;
			}

			auto& state = GetESPState();
			std::lock_guard<std::mutex> lock(state.Mutex);
			state.LastLootRefreshMs = nowMs;
		}
	}
	else
	{
		auto& state = GetESPState();
		std::lock_guard<std::mutex> lock(state.Mutex);
		state.LastLootRefreshMs = 0;
	}

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
		if (bMeshHighlightEnabled)
			SyncHighlightedMeshes(state, NewHighlightedMeshes);
		else
			ClearHighlightedMeshes(state);
		state.CachedActors = std::move(NewCache);
		state.CachedLoot = std::move(NewLoot);
		state.CachedTracers = std::move(NewTracers);
	}
}

void Cheats::RenderESP()
{
	if (!ConfigManager::B("Player.ESP")) return;
	UCanvas* Canvas = Utils::GetCurrentCanvas();

	std::vector<ESPActorCache> LocalActors;
	std::vector<ESPLootCache> LocalLoot;
	std::vector<ESPTracerCache> LocalTracers;
	{
		auto& state = GetESPState();
		std::lock_guard<std::mutex> lock(state.Mutex);
		LocalActors = state.CachedActors;
		LocalLoot = state.CachedLoot;
		LocalTracers = state.CachedTracers;
	}

	for (const auto& Actor : LocalActors)
	{
		const float Height = (std::max)(0.0f, (float)Actor.RightBottomScreen.Y - (float)Actor.LeftTopScreen.Y);
		const float Width = (std::max)(0.0f, (float)Actor.RightBottomScreen.X - (float)Actor.LeftTopScreen.X);
		if (Height <= 0.0f || Width <= 0.0f) continue;

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
			const FVector2D boxPos((float)Actor.LeftTopScreen.X, (float)Actor.LeftTopScreen.Y);
			const FVector2D boxSize(Width, Height);
			if (Canvas)
			{
				Utils::DrawCanvasBox(Canvas, boxPos, boxSize, 1.0f, Utils::U32ToLinearColor(Actor.Color));
			}
			else
			{
				ImGui::GetBackgroundDrawList()->AddRect(
					ImVec2(boxPos.X, boxPos.Y),
					ImVec2(boxPos.X + boxSize.X, boxPos.Y + boxSize.Y),
					Actor.Color, 0.0f, 0, 1.0f
				);
			}
		}

		// Health Bar
		float BarWidth = 4.0f;
		float BarHeight = Height;
		float BarX = (float)Actor.LeftTopScreen.X - 6.0f;
		float BarY = (float)Actor.LeftTopScreen.Y;

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
				Utils::DrawCanvasText(Canvas, DistanceText, FVector2D((float)Actor.LeftTopScreen.X, (float)Actor.RightBottomScreen.Y + 2), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
			else
				ImGui::GetBackgroundDrawList()->AddText(
					ImVec2((float)Actor.LeftTopScreen.X, (float)Actor.RightBottomScreen.Y + 2),
					IM_COL32(255, 255, 255, 255),
					DistanceText
				);
		}

		if (ConfigManager::B("ESP.ShowEnemyName"))
		{
			const float minScale = 0.6f;
			const float maxScale = 1.0f;
			const float t = std::clamp(Actor.Distance / 150.0f, 0.0f, 1.0f);
			const float nameScale = maxScale - (maxScale - minScale) * t;
			const FVector2D scale(nameScale, nameScale);
			if (Canvas)
				Utils::DrawCanvasText(Canvas, Actor.Name, FVector2D((float)Actor.LeftTopScreen.X, (float)Actor.LeftTopScreen.Y - 15), Utils::U32ToLinearColor(Actor.Color), scale);
			else
				ImGui::GetBackgroundDrawList()->AddText(
					ImVec2((float)Actor.LeftTopScreen.X, (float)Actor.LeftTopScreen.Y - 15),
					Actor.Color,
					Actor.Name.ToString().c_str()
				);
		}
	}

	if (ConfigManager::B("ESP.ShowEnemyIndicator"))
	{
		const FVector2D viewportSize = Utils::ImVec2ToFVector2D(GVars.ScreenSize);
		const ImVec2 screenCenter = IsOTSAdsActive()
			? GetCustomReticleScreenPos()
			: ImVec2(GVars.ScreenSize.x * 0.5f, GVars.ScreenSize.y * 0.5f);
		const float maxFOVNormalized = ConfigManager::F("Aimbot.MaxFOV") / 90.0f;
		const float indicatorRadius = (std::max)(16.0f, maxFOVNormalized * ((float)viewportSize.Y * 0.5f));

		for (const auto& Actor : LocalActors)
		{
			const ImVec2 actorCenter(
				((float)Actor.LeftTopScreen.X + (float)Actor.RightBottomScreen.X) * 0.5f,
				((float)Actor.LeftTopScreen.Y + (float)Actor.RightBottomScreen.Y) * 0.5f
			);
			DrawEnemyIndicator(Canvas, screenCenter, indicatorRadius, actorCenter, Actor.Color);
		}
	}

	for (const auto& Loot : LocalLoot)
	{
		const float minScale = 0.55f;
		const float maxScale = 0.95f;
		float maxDistance = ConfigManager::F("ESP.LootMaxDistance");
		if (maxDistance < 1.0f) maxDistance = 1.0f;
		const float t = std::clamp(Loot.Distance / maxDistance, 0.0f, 1.0f);
		const float nameScale = maxScale - (maxScale - minScale) * t;
		const FVector2D scale(nameScale, nameScale);
		const FVector2D drawPos(Loot.ScreenPos.X, Loot.ScreenPos.Y);

		if (Canvas)
			Utils::DrawCanvasText(Canvas, Loot.Name, drawPos, Utils::U32ToLinearColor(Loot.Color), scale);
		else
			ImGui::GetBackgroundDrawList()->AddText(
				ImVec2(drawPos.X, drawPos.Y),
				Loot.Color,
				Loot.Name.ToString().c_str()
			);
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

}
