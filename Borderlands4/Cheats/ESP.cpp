#include "pch.h"


int32 ViewportX = 0.0f;
int32 ViewportY = 0.0f;

auto RenderColor = IM_COL32(255, 255, 255, 255);

struct ESPActorCache {
	ACharacter* TargetActor;
	ImU32 Color;
	float HealthPct;
	FString Name;
	float Distance;
	bool bHasScreenBounds = false;
	FVector2D TopScreen;
	FVector2D BottomScreen;
	FVector2D LeftTopScreen;
	FVector2D RightBottomScreen;
	std::vector<std::pair<FVector, FVector>> SkeletonSegments;
};

struct ESPActorRenderCache {
	const ESPActorCache* Actor;
	FVector2D TopScreen;
	FVector2D BottomScreen;
	FVector2D LeftTopScreen;
	FVector2D RightBottomScreen;
	float Width;
	float Height;
	float Distance;
};

struct ESPLootCache {
	AActor* TargetActor;
	ImU32 Color;
	FString Name;
	float Distance;
	bool bInteractive = false;
	bool bInventoryPickup = false;
	bool bHasScreenBounds = false;
	bool bDrawBox = false;
	FVector WorldAnchor;
	FVector2D LeftTopScreen;
	FVector2D RightBottomScreen;
};

struct ESPTracerCache {
	FVector StartWorld;
	FVector EndWorld;
	ImU32 ColorSegment;
	ImU32 ColorGlow;
	ImU32 ColorCore;
	bool bHasSegment;
	bool bImpact;
	FVector ImpactWorld;
	ImU32 ColorImpactOuter;
	ImU32 ColorImpactInner;
};

namespace
{
	std::atomic<bool> g_LoggedRenderEspCanvas{ false };
	std::atomic<bool> g_LoggedRenderEspImGui{ false };

	bool IsAnyESPFeatureEnabled()
	{
		return ConfigManager::B("ESP.ShowBox") ||
			ConfigManager::B("ESP.ShowEnemyDistance") ||
			ConfigManager::B("ESP.ShowEnemyName") ||
			ConfigManager::B("ESP.ShowEnemyIndicator") ||
			ConfigManager::B("ESP.ShowLootName") ||
			ConfigManager::B("ESP.ShowInteractives") ||
			ConfigManager::B("ESP.Bones") ||
			ConfigManager::B("ESP.BulletTracers");
	}

	struct ESPState
	{
		std::mutex Mutex;
		std::vector<ESPActorCache> CachedActors;
		std::vector<ESPLootCache> CachedLoot;
		std::vector<ESPTracerCache> CachedTracers;
		uint64_t LastActorRefreshMs = 0;
		uint64_t LastLootRefreshMs = 0;
		uintptr_t LastWorld = 0;
		uintptr_t LastLevel = 0;
	};

	ESPState& GetESPState()
	{
		static ESPState state;
		return state;
	}

	constexpr uint64_t kActorRefreshIntervalMs = 100;
	constexpr uint64_t kLootRefreshIntervalMs = 250;

	void ResetESPState(ESPState& state)
	{
		state.CachedActors.clear();
		state.CachedLoot.clear();
		state.CachedTracers.clear();
		state.LastActorRefreshMs = 0;
		state.LastLootRefreshMs = 0;
		state.LastWorld = 0;
		state.LastLevel = 0;
	}
}

struct BonePair { FName Parent; FName Child; };

static const std::vector<BonePair>& GetSkeletonBonePairs()
{
	static const std::vector<BonePair> BonePairs = []()
	{
		auto N = [](const wchar_t* name) -> FName
		{
			return UKismetStringLibrary::Conv_StringToName(name);
		};

		return std::vector<BonePair>{
			{N(L"Hips"), N(L"Spine1")},
			{N(L"Spine1"), N(L"Spine2")},
			{N(L"Spine2"), N(L"Spine3")},
			{N(L"Spine3"), N(L"Neck")},
			{N(L"Neck"), N(L"Head")},
			{N(L"Neck"), N(L"L_Upperarm")},
			{N(L"L_Upperarm"), N(L"L_Forearm")},
			{N(L"L_Forearm"), N(L"L_Hand")},
			{N(L"Neck"), N(L"R_Upperarm")},
			{N(L"R_Upperarm"), N(L"R_Forearm")},
			{N(L"R_Forearm"), N(L"R_Hand")},
			{N(L"Hips"), N(L"L_Thigh")},
			{N(L"L_Thigh"), N(L"L_Shin")},
			{N(L"L_Shin"), N(L"L_Foot")},
			{N(L"Hips"), N(L"R_Thigh")},
			{N(L"R_Thigh"), N(L"R_Shin")},
			{N(L"R_Shin"), N(L"R_Foot")}
		};
	}();

	return BonePairs;
}

static bool ProjectForOverlay(const FVector& worldPos, FVector2D& outScreen)
{
	if (!GVars.PlayerController) return false;
	// Use player-viewport-relative coordinates so OTS/shadow-camera view rect matches UCanvas and ImGui overlay placement.
	return Utils::ProjectWorldLocationToScreen(worldPos, outScreen, true);
}

static bool ProjectActorScreenBounds(const std::vector<FVector>& points, FVector2D& outTopScreen, FVector2D& outBottomScreen, FVector2D& outLeftTopScreen, FVector2D& outRightBottomScreen)
{
	if (points.empty())
		return false;

	bool hasProjectedPoint = false;
	float minX = FLT_MAX;
	float minY = FLT_MAX;
	float maxX = -FLT_MAX;
	float maxY = -FLT_MAX;

	for (const FVector& point : points)
	{
		FVector2D projected;
		if (!ProjectForOverlay(point, projected))
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

static bool ProjectActorBounds(AActor* actor, FVector2D& outLeftTopScreen, FVector2D& outRightBottomScreen)
{
	if (!actor)
		return false;

	FVector origin{};
	FVector extent{};
	actor->GetActorBounds(false, &origin, &extent, false);
	if (extent.X <= 0.0f || extent.Y <= 0.0f || extent.Z <= 0.0f)
		return false;

	const std::vector<FVector> corners = {
		FVector(origin.X - extent.X, origin.Y - extent.Y, origin.Z - extent.Z),
		FVector(origin.X - extent.X, origin.Y - extent.Y, origin.Z + extent.Z),
		FVector(origin.X - extent.X, origin.Y + extent.Y, origin.Z - extent.Z),
		FVector(origin.X - extent.X, origin.Y + extent.Y, origin.Z + extent.Z),
		FVector(origin.X + extent.X, origin.Y - extent.Y, origin.Z - extent.Z),
		FVector(origin.X + extent.X, origin.Y - extent.Y, origin.Z + extent.Z),
		FVector(origin.X + extent.X, origin.Y + extent.Y, origin.Z - extent.Z),
		FVector(origin.X + extent.X, origin.Y + extent.Y, origin.Z + extent.Z),
	};

	FVector2D topScreen{};
	FVector2D bottomScreen{};
	return ProjectActorScreenBounds(corners, topScreen, bottomScreen, outLeftTopScreen, outRightBottomScreen);
}

static bool GetActorBoundsAnchor(AActor* actor, FVector& outAnchor)
{
	if (!actor)
		return false;

	FVector origin{};
	FVector extent{};
	actor->GetActorBounds(false, &origin, &extent, false);
	if (extent.X <= 0.0f || extent.Y <= 0.0f || extent.Z <= 0.0f)
		return false;

	outAnchor = FVector(origin.X, origin.Y, origin.Z + extent.Z + 10.0f);
	return true;
}

static std::string ToLowerAsciiEsp(std::string value)
{
	for (char& c : value)
	{
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	return value;
}

static bool IsInteractiveEspTarget(AActor* actor)
{
	if (!actor)
		return false;
	if (actor->IsA(SDK::ALootableObject::StaticClass()) || actor->IsA(SDK::AOakInteractiveObject::StaticClass()))
		return true;

	const std::string className = ToLowerAsciiEsp(actor->Class ? actor->Class->GetName() : "");
	const std::string fullName = ToLowerAsciiEsp(actor->GetFullName());
	const char* interactiveHints[] = {
		"lootable",
		"interactive",
		"usable",
		"carryable",
		"chest",
		"vending",
		"vendor",
		"switch",
		"button",
		"lever",
		"console",
		"terminal",
		"station"
	};

	for (const char* hint : interactiveHints)
	{
		if (className.find(hint) != std::string::npos || fullName.find(hint) != std::string::npos)
			return true;
	}

	return className.find("lootable") != std::string::npos ||
		className.find("interactive") != std::string::npos ||
		fullName.find("lootableobject") != std::string::npos ||
		fullName.find("oakinteractiveobject") != std::string::npos;
}

static bool BuildFixedSkeletonRenderCache(
	ACharacter* actor,
	std::vector<std::pair<FVector, FVector>>& outSegments)
{
	outSegments.clear();
	if (!actor || !actor->Mesh)
		return false;

	const auto& bonePairs = GetSkeletonBonePairs();
	outSegments.reserve(bonePairs.size());

	for (const auto& bp : bonePairs)
	{
		const int32 parentIndex = actor->Mesh->GetBoneIndex(bp.Parent);
		const int32 childIndex = actor->Mesh->GetBoneIndex(bp.Child);
		if (parentIndex == -1 || childIndex == -1)
			continue;

		const FVector parent = actor->Mesh->GetBoneTransform(bp.Parent, ERelativeTransformSpace::RTS_World).Translation;
		const FVector child = actor->Mesh->GetBoneTransform(bp.Child, ERelativeTransformSpace::RTS_World).Translation;
		outSegments.emplace_back(parent, child);
	}

	return !outSegments.empty();
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
	const FRotator camRot = CameraPOV.Rotation;
	const FVector camFwd = Utils::FRotatorToVector(camRot);
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

	GUI::Draw::TriangleFilled(p1, p2, p3, color, canvas);
}

static float GetTracerScale()
{
	const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	if (displaySize.y <= 1.0f)
		return 1.0f;

	const float normalized = displaySize.y / 1080.0f;
	return std::clamp(normalized, 0.75f, 1.15f);
}

void Cheats::UpdateESP()
{
	Logger::LogThrottled(Logger::Level::Debug, "ESP", 10000, "Cheats::UpdateESP() active");
	SDK::UWorld* currentWorld = Utils::GetWorldSafe();
	SDK::ULevel* currentLevel = currentWorld ? currentWorld->PersistentLevel : nullptr;
	auto& state = GetESPState();
	{
		std::lock_guard<std::mutex> lock(state.Mutex);
		const uintptr_t worldPtr = reinterpret_cast<uintptr_t>(currentWorld);
		const uintptr_t levelPtr = reinterpret_cast<uintptr_t>(currentLevel);
		if (state.LastWorld != worldPtr || state.LastLevel != levelPtr)
		{
			ResetESPState(state);
			state.LastWorld = worldPtr;
			state.LastLevel = levelPtr;
		}
	}

	AActor* SelfActor = Utils::GetSelfActor();
	if (!ConfigManager::B("Player.ESP") || !IsAnyESPFeatureEnabled() || !Utils::bIsInGame || Utils::bIsLoading || !GVars.PlayerController || !GVars.Level || !SelfActor || !GVars.World || !GVars.World->VTable || GVars.World != currentWorld || GVars.Level != currentLevel)
	{
		std::lock_guard<std::mutex> lock(state.Mutex);
		ResetESPState(state);
		state.LastWorld = reinterpret_cast<uintptr_t>(currentWorld);
		state.LastLevel = reinterpret_cast<uintptr_t>(currentLevel);
		return;
	}

	if (!GVars.PlayerController->PlayerCameraManager || !GVars.PlayerController->PlayerCameraManager->VTable)
	{
		std::lock_guard<std::mutex> lock(state.Mutex);
		ResetESPState(state);
		state.LastWorld = reinterpret_cast<uintptr_t>(currentWorld);
		state.LastLevel = reinterpret_cast<uintptr_t>(currentLevel);
		return;
	}

	std::vector<ESPActorCache> NewCache;
	const uint64_t nowMs = GetTickCount64();
	bool shouldRefreshActors = false;
	{
		std::lock_guard<std::mutex> lock(state.Mutex);
		shouldRefreshActors = state.LastActorRefreshMs == 0 || (nowMs - state.LastActorRefreshMs) >= kActorRefreshIntervalMs;
		if (!shouldRefreshActors)
		{
			NewCache = state.CachedActors;
		}
	}

	if (shouldRefreshActors)
	{
		NewCache.reserve(GVars.UnitCache.size());

		for (ACharacter* TargetActor : GVars.UnitCache)
		{
			if (!TargetActor || !Utils::IsValidActor(TargetActor)) continue;
			if (TargetActor == SelfActor) continue;

			ETeamAttitude Attitude = Utils::GetAttitude(TargetActor);
			if (Attitude == ETeamAttitude::Friendly && !ConfigManager::B("ESP.ShowTeam")) continue;

			float HealthPct = Utils::GetHealthPercent(TargetActor);
			if (HealthPct <= 0.0f) continue;

			ImU32 Color = Utils::ConvertImVec4toU32(ConfigManager::Color("ESP.EnemyColor"));
			if (Attitude == ETeamAttitude::Friendly) Color = Utils::ConvertImVec4toU32(ConfigManager::Color("ESP.TeamColor"));
			else if (Attitude == ETeamAttitude::Neutral) Color = IM_COL32(255, 255, 0, 255);

			const float Distance = Utils::GetDistanceMeters(SelfActor, TargetActor);
			if (Distance < 0.0f || Distance > 1000.0f) continue;

			ESPActorCache Cache{};
			Cache.TargetActor = TargetActor;
			Cache.Color = Color;
			Cache.HealthPct = HealthPct;
			Cache.Distance = Distance;
			Cache.bHasScreenBounds = ProjectActorBounds(TargetActor, Cache.LeftTopScreen, Cache.RightBottomScreen);
			if (Cache.bHasScreenBounds)
			{
				Cache.TopScreen = FVector2D(((float)Cache.LeftTopScreen.X + (float)Cache.RightBottomScreen.X) * 0.5f, (float)Cache.LeftTopScreen.Y);
				Cache.BottomScreen = FVector2D(((float)Cache.LeftTopScreen.X + (float)Cache.RightBottomScreen.X) * 0.5f, (float)Cache.RightBottomScreen.Y);
			}
			if (ConfigManager::B("ESP.Bones") && Distance >= 0.0f && Distance < 70.0f)
				BuildFixedSkeletonRenderCache(TargetActor, Cache.SkeletonSegments);
			if (ConfigManager::B("ESP.ShowEnemyName"))
			{
				Cache.Name = UKismetSystemLibrary::GetDisplayName(TargetActor);
			}

			NewCache.push_back(Cache);
		}
	}

	std::vector<ESPTracerCache> NewTracers;
	std::vector<ESPLootCache> NewLoot;
	if ((ConfigManager::B("ESP.ShowLootName") || ConfigManager::B("ESP.ShowInteractives")) && GVars.Level)
	{
		const uint64_t nowMs = GetTickCount64();
		bool shouldRefreshLoot = false;
		{
			std::lock_guard<std::mutex> lock(state.Mutex);
			shouldRefreshLoot = state.LastLootRefreshMs == 0 || (nowMs - state.LastLootRefreshMs) >= kLootRefreshIntervalMs;
			if (!shouldRefreshLoot)
			{
				NewLoot = state.CachedLoot;
			}
		}

		if (shouldRefreshLoot)
		{
			float maxLootDistance = ConfigManager::F("ESP.LootMaxDistance");
			if (maxLootDistance <= 0.0f) maxLootDistance = 250.0f;
			float maxInteractiveDistance = ConfigManager::F("ESP.InteractiveMaxDistance");
			if (maxInteractiveDistance <= 0.0f) maxInteractiveDistance = 150.0f;
			const float maxDistance = (std::max)(maxLootDistance, maxInteractiveDistance);
			const ImU32 lootColor = Utils::ConvertImVec4toU32(ConfigManager::Color("ESP.LootColor"));
			const ImU32 interactiveColor = Utils::ConvertImVec4toU32(ConfigManager::Color("ESP.InteractiveColor"));
			NewLoot.reserve(128);

			if (!Utils::ForEachLevelActor(GVars.Level, [&](AActor* Actor)
					{
						if (!Actor || !Utils::IsValidActor(Actor)) return true;

						const float distance = Utils::GetDistanceMeters(SelfActor, Actor);
						if (distance < 0.0f || distance > maxDistance) return true;

							const bool isInventoryPickup = Actor->IsA(SDK::AInventoryPickup::StaticClass());
							const bool isInteractiveObject = !isInventoryPickup && IsInteractiveEspTarget(Actor);

							if (isInventoryPickup)
							{
								if (!ConfigManager::B("ESP.ShowLootName") || distance > maxLootDistance)
									return true;
							}
							else if (isInteractiveObject)
							{
								if (!ConfigManager::B("ESP.ShowInteractives") || distance > maxInteractiveDistance)
									return true;
						}
						else
						{
							return true;
						}

						ESPLootCache Cache{};
						Cache.TargetActor = Actor;
						Cache.Distance = distance;
						Cache.Name = UKismetSystemLibrary::GetDisplayName(Actor);
						Cache.Color = isInventoryPickup ? lootColor : interactiveColor;
						Cache.bInteractive = isInteractiveObject;
						Cache.bInventoryPickup = isInventoryPickup;
						Cache.bDrawBox = false;
						Cache.bHasScreenBounds = ProjectActorBounds(Actor, Cache.LeftTopScreen, Cache.RightBottomScreen);
						Cache.WorldAnchor = Actor->K2_GetActorLocation();
						if (!isInventoryPickup && !GetActorBoundsAnchor(Actor, Cache.WorldAnchor))
							Cache.WorldAnchor = Actor->K2_GetActorLocation();
						NewLoot.push_back(std::move(Cache));
						return true;
						}))
			{
				Logger::LogThrottled(Logger::Level::Warning, "ESP", 2000, "Loot ESP skipped: Level->Actors unavailable");
				std::lock_guard<std::mutex> lock(state.Mutex);
				NewLoot = state.CachedLoot;
			}

			std::lock_guard<std::mutex> lock(state.Mutex);
			state.LastLootRefreshMs = nowMs;
		}
	}
	else
	{
		std::lock_guard<std::mutex> lock(state.Mutex);
		state.LastLootRefreshMs = 0;
	}

	if (ConfigManager::B("ESP.BulletTracers") && GVars.PlayerController && GVars.PlayerController->PlayerCameraManager)
	{
		std::lock_guard<std::mutex> lock(CheatsData::TracerMutex);
		float CurrentTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		NewTracers.reserve(CheatsData::BulletTracers.size());

		for (auto it = CheatsData::BulletTracers.begin(); it != CheatsData::BulletTracers.end(); )
		{
			const float tracerDuration = ConfigManager::F("ESP.TracerDuration");
			if (CurrentTime - it->CreationTime > tracerDuration)
			{
				it = CheatsData::BulletTracers.erase(it);
				continue;
			}
			
			float Age = CurrentTime - it->CreationTime;
			float FadeRatio = 1.0f - (Age / tracerDuration);
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

			const size_t PointsCount = it->Points.size();
			if (PointsCount >= 2)
			{
				for (size_t i = 0; i + 1 < PointsCount; ++i)
				{
					ESPTracerCache TC{};
					TC.bHasSegment = true;
					TC.StartWorld = it->Points[i];
					TC.EndWorld = it->Points[i + 1];
					TC.ColorSegment = ImGui::ColorConvertFloat4ToU32(ImVec4(BaseColor.x, BaseColor.y, BaseColor.z, BaseColor.w * 0.2f));
					TC.ColorGlow = ImGui::ColorConvertFloat4ToU32(ImVec4(BaseColor.x, BaseColor.y, BaseColor.z, BaseColor.w * 0.5f));
					TC.ColorCore = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, BaseColor.w));

					if (i > 0 && (it->bClosed || i < PointsCount - 1))
					{
						TC.bImpact = true;
						TC.ImpactWorld = it->Points[i];
						TC.ColorImpactOuter = ImGui::ColorConvertFloat4ToU32(ImVec4(BaseColor.x, BaseColor.y, BaseColor.z, BaseColor.w * 0.3f));
						TC.ColorImpactInner = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, BaseColor.w));
					}
					else
					{
						TC.bImpact = false;
					}

					NewTracers.push_back(TC);
				}
			}
			else if (PointsCount == 1 && it->bClosed)
			{
				ESPTracerCache TC{};
				TC.bHasSegment = false;
				TC.bImpact = true;
				TC.ImpactWorld = it->Points[0];
				TC.ColorImpactOuter = ImGui::ColorConvertFloat4ToU32(ImVec4(BaseColor.x, BaseColor.y, BaseColor.z, BaseColor.w * 0.3f));
				TC.ColorImpactInner = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, BaseColor.w));
				NewTracers.push_back(TC);
			}

			++it;
		}
	}

	{
		std::lock_guard<std::mutex> lock(state.Mutex);
		if (shouldRefreshActors)
			state.LastActorRefreshMs = nowMs;
		state.LastWorld = reinterpret_cast<uintptr_t>(currentWorld);
		state.LastLevel = reinterpret_cast<uintptr_t>(currentLevel);
		state.CachedActors = std::move(NewCache);
		state.CachedLoot = std::move(NewLoot);
		state.CachedTracers = std::move(NewTracers);
	}
}

void Cheats::RenderESP()
{
	if (!ConfigManager::B("Player.ESP") || !IsAnyESPFeatureEnabled()) return;
	UCanvas* Canvas = Utils::GetCurrentCanvas();
	if (Canvas)
	{
		if (!g_LoggedRenderEspCanvas.exchange(true))
		{
			LOG_INFO("DrawPath", "RenderESP using UCanvas path.");
		}
	}
	else
	{
		if (!g_LoggedRenderEspImGui.exchange(true))
		{
			LOG_INFO("DrawPath", "RenderESP using ImGui path.");
		}
	}

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

	std::vector<ESPActorRenderCache> RenderActors;
	RenderActors.reserve(LocalActors.size());

	for (const auto& Actor : LocalActors)
	{
		if (!Actor.TargetActor || !Utils::IsValidActor(Actor.TargetActor))
			continue;
		if (!Actor.bHasScreenBounds)
			continue;

		const float Width = (std::max)(0.0f, (float)Actor.RightBottomScreen.X - (float)Actor.LeftTopScreen.X);
		const float Height = (std::max)(0.0f, (float)Actor.RightBottomScreen.Y - (float)Actor.LeftTopScreen.Y);
		if (Height <= 0.0f || Width <= 0.0f) continue;

		ESPActorRenderCache renderCache{};
		renderCache.Actor = &Actor;
		renderCache.TopScreen = Actor.TopScreen;
		renderCache.BottomScreen = Actor.BottomScreen;
		renderCache.LeftTopScreen = Actor.LeftTopScreen;
		renderCache.RightBottomScreen = Actor.RightBottomScreen;
		renderCache.Width = Width;
		renderCache.Height = Height;
		renderCache.Distance = Actor.Distance;
		RenderActors.push_back(renderCache);
	}

	for (const auto& RenderActor : RenderActors)
	{
		const auto& Actor = *RenderActor.Actor;
		const float currentDistance = RenderActor.Distance;
		const float Width = RenderActor.Width;
		const float Height = RenderActor.Height;

		// Skeleton
		if (ConfigManager::B("ESP.Bones") && currentDistance >= 0.0f && currentDistance < 70.0f)
		{
			for (const auto& segment : Actor.SkeletonSegments)
			{
				FVector2D s1, s2;
				if (ProjectForOverlay(segment.first, s1) && ProjectForOverlay(segment.second, s2))
				{
					GUI::Draw::Line(ImVec2((float)s1.X, (float)s1.Y), ImVec2((float)s2.X, (float)s2.Y), Actor.Color, 2.0f, Canvas);
				}
			}
		}

		// Box
		if (ConfigManager::B("ESP.ShowBox"))
		{
			const FVector2D boxPos((float)RenderActor.LeftTopScreen.X, (float)RenderActor.LeftTopScreen.Y);
			const FVector2D boxSize(Width, Height);
			GUI::Draw::Rect(
				ImVec2(boxPos.X, boxPos.Y),
				ImVec2(boxPos.X + boxSize.X, boxPos.Y + boxSize.Y),
				Actor.Color,
				1.0f,
				Canvas);
		}

		// Health Bar
		float BarWidth = 4.0f;
		float BarHeight = Height;
		float BarX = (float)RenderActor.LeftTopScreen.X - 6.0f;
		float BarY = (float)RenderActor.LeftTopScreen.Y;

		// Background
		GUI::Draw::RectFilled(ImVec2(BarX, BarY), ImVec2(BarX + BarWidth, BarY + BarHeight), IM_COL32(0, 0, 0, 150), Canvas);

		// Health Fill
		ImU32 HealthColor = IM_COL32(0, 255, 0, 255);
		if (Actor.HealthPct < 0.3f) HealthColor = IM_COL32(255, 0, 0, 255);
		else if (Actor.HealthPct < 0.7f) HealthColor = IM_COL32(255, 255, 0, 255);

		float FillHeight = BarHeight * Actor.HealthPct;
		GUI::Draw::RectFilled(ImVec2(BarX, BarY + BarHeight - FillHeight), ImVec2(BarX + BarWidth, BarY + BarHeight), HealthColor, Canvas);

		// Distance and Name
		if (ConfigManager::B("ESP.ShowEnemyDistance"))
		{
			char DistanceText[32];
			snprintf(DistanceText, sizeof(DistanceText), "%.0f m", currentDistance >= 0.0f ? currentDistance : Actor.Distance);
			GUI::Draw::Text(DistanceText, ImVec2((float)RenderActor.LeftTopScreen.X, (float)RenderActor.RightBottomScreen.Y + 2), IM_COL32(255, 255, 255, 255), FVector2D(1.0f, 1.0f), false, false, true, Canvas);
		}

		if (ConfigManager::B("ESP.ShowEnemyName"))
		{
			const float minScale = 0.6f;
			const float maxScale = 1.0f;
			const float nameDistance = currentDistance >= 0.0f ? currentDistance : Actor.Distance;
			const float t = std::clamp(nameDistance / 150.0f, 0.0f, 1.0f);
			const float nameScale = maxScale - (maxScale - minScale) * t;
			const FVector2D scale(nameScale, nameScale);
			GUI::Draw::Text(Actor.Name, ImVec2((float)RenderActor.LeftTopScreen.X, (float)RenderActor.LeftTopScreen.Y - 15), Actor.Color, scale, false, false, true, Canvas);
		}
	}

	if (ConfigManager::B("ESP.ShowEnemyIndicator"))
	{
		const FVector2D viewportSize = Utils::ImVec2ToFVector2D(GVars.ScreenSize);
		const ImVec2 screenCenter = GetCustomReticleScreenPos();
		const float maxFOVNormalized = ConfigManager::F("Aimbot.MaxFOV") / 90.0f;
		const float indicatorRadius = (std::max)(16.0f, maxFOVNormalized * ((float)viewportSize.Y * 0.5f));

		for (const auto& RenderActor : RenderActors)
		{
			const ImVec2 actorCenter(
				((float)RenderActor.LeftTopScreen.X + (float)RenderActor.RightBottomScreen.X) * 0.5f,
				((float)RenderActor.LeftTopScreen.Y + (float)RenderActor.RightBottomScreen.Y) * 0.5f
			);
			DrawEnemyIndicator(Canvas, screenCenter, indicatorRadius, actorCenter, RenderActor.Actor->Color);
		}
	}

	for (const auto& Loot : LocalLoot)
	{
		if (!Loot.TargetActor || !Utils::IsValidActor(Loot.TargetActor))
			continue;

		if (Loot.bDrawBox && Loot.bHasScreenBounds)
		{
			const float width = (std::max)(0.0f, (float)Loot.RightBottomScreen.X - (float)Loot.LeftTopScreen.X);
			const float height = (std::max)(0.0f, (float)Loot.RightBottomScreen.Y - (float)Loot.LeftTopScreen.Y);
			if (width > 0.0f && height > 0.0f)
			{
				GUI::Draw::Rect(
					ImVec2((float)Loot.LeftTopScreen.X, (float)Loot.LeftTopScreen.Y),
					ImVec2((float)Loot.RightBottomScreen.X, (float)Loot.RightBottomScreen.Y),
					Loot.Color,
					1.0f,
					Canvas);
			}
		}

		FVector2D screenPos;
		const bool projected = Loot.bInventoryPickup
			? Utils::ProjectWorldLocationToScreen(Loot.WorldAnchor, screenPos, true)
			: ProjectForOverlay(Loot.WorldAnchor, screenPos);
		if (!projected)
		{
			if (!Loot.bInventoryPickup && Loot.bHasScreenBounds)
			{
				screenPos = FVector2D(
					((float)Loot.LeftTopScreen.X + (float)Loot.RightBottomScreen.X) * 0.5f,
					(float)Loot.LeftTopScreen.Y - 2.0f);
			}
			else
			{
				continue;
			}
		}

		const float currentDistance = Loot.Distance;
		const float minScale = 0.55f;
		const float maxScale = 0.95f;
		float maxDistance = Loot.bInteractive ? ConfigManager::F("ESP.InteractiveMaxDistance") : ConfigManager::F("ESP.LootMaxDistance");
		if (maxDistance < 1.0f) maxDistance = 1.0f;
		const float scaleDistance = currentDistance >= 0.0f ? currentDistance : Loot.Distance;
		const float t = std::clamp(scaleDistance / maxDistance, 0.0f, 1.0f);
		const float nameScale = maxScale - (maxScale - minScale) * t;
		const FVector2D scale(nameScale, nameScale);
		const FVector2D drawPos(screenPos.X, screenPos.Y);

		GUI::Draw::Text(Loot.Name, ImVec2(drawPos.X, drawPos.Y), Loot.Color, scale, false, false, true, Canvas);
	}

	if (ConfigManager::B("ESP.BulletTracers"))
	{
		const float tracerScale = GetTracerScale();
		const float segmentThickness = 2.2f * tracerScale;
		const float glowThickness = 1.2f * tracerScale;
		const float coreThickness = 0.8f * tracerScale;
		const float impactOuterRadius = 4.2f * tracerScale;
		const float impactInnerRadius = 2.4f * tracerScale;

		for (const auto& Tracer : LocalTracers)
		{
			if (Tracer.bHasSegment) {
				FVector2D startScreen, endScreen;
				if (ProjectForOverlay(Tracer.StartWorld, startScreen) && ProjectForOverlay(Tracer.EndWorld, endScreen))
				{
					const ImVec2 start((float)startScreen.X, (float)startScreen.Y);
					const ImVec2 end((float)endScreen.X, (float)endScreen.Y);
					GUI::Draw::Line(start, end, Tracer.ColorSegment, segmentThickness, Canvas);
					GUI::Draw::Line(start, end, Tracer.ColorGlow, glowThickness, Canvas);
					GUI::Draw::Line(start, end, Tracer.ColorCore, coreThickness, Canvas);
				}
			}

			if (Tracer.bImpact) {
				FVector2D impactScreen;
				if (ProjectForOverlay(Tracer.ImpactWorld, impactScreen))
				{
					const ImVec2 impact((float)impactScreen.X, (float)impactScreen.Y);
					GUI::Draw::CircleFilled(impact, impactOuterRadius, Tracer.ColorImpactOuter, Canvas);
					GUI::Draw::CircleFilled(impact, impactInnerRadius, Tracer.ColorImpactInner, Canvas);
				}
			}
		}
	}

}
