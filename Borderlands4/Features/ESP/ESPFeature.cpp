#include "pch.h"

namespace Features
{
	int32 ViewportX = 0.0f;
	int32 ViewportY = 0.0f;

	auto RenderColor = IM_COL32(255, 255, 255, 255);

struct ESPActorCache {
	ACharacter* TargetActor;
	ImU32 Color;
	float HealthPct;
	FString Name;
	float Distance;
	bool bHasBoundsWorld = false;
	FVector HeadPosWorld{};
	FVector FeetPosWorld{};
	FRotator Rotation{};
	std::array<FVector, 8> BoundsCorners{};
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
			ConfigManager::B("ESP.ShowHealthBar") ||
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
		std::vector<ESPLootCache> PendingLoot;
		std::vector<ESPTracerCache> CachedTracers;
		std::unordered_map<uintptr_t, uint8_t> LootKindCache;
		uint64_t LastActorRefreshMs = 0;
		uint64_t LastLootRefreshMs = 0;
		uintptr_t LastWorld = 0;
		uintptr_t LastLevel = 0;
		int32_t LootScanCursor = 0;
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
		state.PendingLoot.clear();
		state.CachedTracers.clear();
		state.LootKindCache.clear();
		state.LastActorRefreshMs = 0;
		state.LastLootRefreshMs = 0;
		state.LastWorld = 0;
		state.LastLevel = 0;
		state.LootScanCursor = 0;
	}

enum class LootActorKind : uint8_t
{
	None = 0,
	InventoryPickup,
	Interactive
	};
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

static bool ProjectActorScreenBounds(const std::array<FVector, 8>& points, FVector2D& outTopScreen, FVector2D& outBottomScreen, FVector2D& outLeftTopScreen, FVector2D& outRightBottomScreen)
{
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

static bool BuildActorBoundsCorners(AActor* actor, std::array<FVector, 8>& outCorners)
{
	if (!actor)
		return false;

	FVector origin{};
	FVector extent{};
	actor->GetActorBounds(false, &origin, &extent, false);
	if (extent.X <= 0.0f || extent.Y <= 0.0f || extent.Z <= 0.0f)
		return false;

	outCorners = {
		FVector(origin.X - extent.X, origin.Y - extent.Y, origin.Z - extent.Z),
		FVector(origin.X - extent.X, origin.Y - extent.Y, origin.Z + extent.Z),
		FVector(origin.X - extent.X, origin.Y + extent.Y, origin.Z - extent.Z),
		FVector(origin.X - extent.X, origin.Y + extent.Y, origin.Z + extent.Z),
		FVector(origin.X + extent.X, origin.Y - extent.Y, origin.Z - extent.Z),
		FVector(origin.X + extent.X, origin.Y - extent.Y, origin.Z + extent.Z),
		FVector(origin.X + extent.X, origin.Y + extent.Y, origin.Z - extent.Z),
		FVector(origin.X + extent.X, origin.Y + extent.Y, origin.Z + extent.Z),
	};
	return true;
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

static bool IsInteractiveEspTargetClass(UClass* actorClass)
{
	if (!actorClass)
		return false;
	if (actorClass->IsSubclassOf(SDK::ALootableObject::StaticClass()) || actorClass->IsSubclassOf(SDK::AOakInteractiveObject::StaticClass()))
		return true;

	const std::string className = ToLowerAsciiEsp(actorClass->GetName());
	const std::string fullName = ToLowerAsciiEsp(actorClass->GetFullName());
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

static LootActorKind ClassifyLootEspTarget(AActor* actor, ESPState& state)
{
	if (!actor || !actor->Class)
		return LootActorKind::None;

	const uintptr_t classKey = reinterpret_cast<uintptr_t>(actor->Class);
	const auto cached = state.LootKindCache.find(classKey);
	if (cached != state.LootKindCache.end())
		return static_cast<LootActorKind>(cached->second);

	LootActorKind kind = LootActorKind::None;
	if (actor->IsA(SDK::AInventoryPickup::StaticClass()))
		kind = LootActorKind::InventoryPickup;
	else if (IsInteractiveEspTargetClass(actor->Class))
		kind = LootActorKind::Interactive;

	state.LootKindCache[classKey] = static_cast<uint8_t>(kind);
	return kind;
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

static void UpdateESPFeature()
{
	Logger::LogThrottled(Logger::Level::Debug, "ESP", 10000, "UpdateESPFeature() active");
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
	if (!ConfigManager::B("Player.ESP") || !IsAnyESPFeatureEnabled() || !GVars.PlayerController || !GVars.Level || !SelfActor || !GVars.World || !GVars.World->VTable || GVars.World != currentWorld || GVars.Level != currentLevel)
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
			Cache.Rotation = TargetActor->K2_GetActorRotation();
			Cache.FeetPosWorld = TargetActor->K2_GetActorLocation();
			
			// Try to get a more accurate Head position if we have a mesh
			if (TargetActor->Mesh)
			{
				static const FName headBone = UKismetStringLibrary::Conv_StringToName(L"Head");
				Cache.HeadPosWorld = TargetActor->Mesh->GetBoneTransform(headBone, ERelativeTransformSpace::RTS_World).Translation;
			}
			else
			{
				Cache.HeadPosWorld = Cache.FeetPosWorld;
				Cache.HeadPosWorld.Z += 80.0f; // Fallback
			}

			Cache.bHasBoundsWorld = BuildActorBoundsCorners(TargetActor, Cache.BoundsCorners);
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
		const bool showLoot = ConfigManager::B("ESP.ShowLootName");
		const bool showInteractives = ConfigManager::B("ESP.ShowInteractives");
		float maxLootDistance = ConfigManager::F("ESP.LootMaxDistance");
		if (maxLootDistance <= 0.0f) maxLootDistance = 250.0f;
		float maxInteractiveDistance = ConfigManager::F("ESP.InteractiveMaxDistance");
		if (maxInteractiveDistance <= 0.0f) maxInteractiveDistance = 150.0f;
		const float maxDistance = (std::max)(maxLootDistance, maxInteractiveDistance);
		const ImU32 lootColor = Utils::ConvertImVec4toU32(ConfigManager::Color("ESP.LootColor"));
		const ImU32 interactiveColor = Utils::ConvertImVec4toU32(ConfigManager::Color("ESP.InteractiveColor"));
		constexpr int32_t kLootScanBatchSize = 384;

		int32_t scanStart = 0;
		bool shouldRefreshLoot = false;
		{
			std::lock_guard<std::mutex> lock(state.Mutex);
			shouldRefreshLoot = state.LastLootRefreshMs == 0 || (nowMs - state.LastLootRefreshMs) >= kLootRefreshIntervalMs || !state.PendingLoot.empty();
			if (!shouldRefreshLoot)
			{
				NewLoot = state.CachedLoot;
			}
			else
			{
				scanStart = state.LootScanCursor;
				if (scanStart == 0 && state.PendingLoot.empty())
					state.PendingLoot.reserve(128);
			}
		}

		if (shouldRefreshLoot)
		{
			int32_t nextCursor = 0;
			bool completed = false;
			std::vector<ESPLootCache> batchLoot;
			batchLoot.reserve(64);

			if (!Utils::ForEachLevelActorChunk(
				GVars.Level,
				scanStart,
				kLootScanBatchSize,
				[&](AActor* Actor)
				{
					if (!Actor || !Utils::IsValidActor(Actor))
						return true;

					const float distance = Utils::GetDistanceMeters(SelfActor, Actor);
					if (distance < 0.0f || distance > maxDistance)
						return true;

					const LootActorKind lootKind = ClassifyLootEspTarget(Actor, state);
					if (lootKind == LootActorKind::InventoryPickup)
					{
						if (!showLoot || distance > maxLootDistance)
							return true;
					}
					else if (lootKind == LootActorKind::Interactive)
					{
						if (!showInteractives || distance > maxInteractiveDistance)
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
					Cache.Color = lootKind == LootActorKind::InventoryPickup ? lootColor : interactiveColor;
					Cache.bInteractive = lootKind == LootActorKind::Interactive;
					Cache.bInventoryPickup = lootKind == LootActorKind::InventoryPickup;
					Cache.bDrawBox = false;
					Cache.bHasScreenBounds = false;
					Cache.WorldAnchor = Actor->K2_GetActorLocation();
					if (Cache.bInteractive)
						GetActorBoundsAnchor(Actor, Cache.WorldAnchor);
					batchLoot.push_back(std::move(Cache));
					return true;
				},
				&nextCursor,
				nullptr,
				&completed))
			{
				Logger::LogThrottled(Logger::Level::Warning, "ESP", 2000, "Loot ESP skipped: Level->Actors unavailable");
				std::lock_guard<std::mutex> lock(state.Mutex);
				state.PendingLoot.clear();
				state.LootScanCursor = 0;
				NewLoot = state.CachedLoot;
			}
			else
			{
				std::lock_guard<std::mutex> lock(state.Mutex);
				state.PendingLoot.insert(state.PendingLoot.end(),
					std::make_move_iterator(batchLoot.begin()),
					std::make_move_iterator(batchLoot.end()));

				if (completed)
				{
					state.CachedLoot = std::move(state.PendingLoot);
					state.PendingLoot.clear();
					state.LootScanCursor = 0;
					state.LastLootRefreshMs = nowMs;
				}
				else
				{
					state.LootScanCursor = nextCursor;
				}

				NewLoot = state.CachedLoot;
			}
		}
	}
	else
	{
		std::lock_guard<std::mutex> lock(state.Mutex);
		state.PendingLoot.clear();
		state.LootScanCursor = 0;
		state.LastLootRefreshMs = 0;
	}

	if (ConfigManager::B("ESP.BulletTracers") && GVars.PlayerController && GVars.PlayerController->PlayerCameraManager)
	{
		std::lock_guard<std::mutex> lock(Features::Data::TracerMutex);
		float CurrentTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
		NewTracers.reserve(Features::Data::BulletTracers.size());

		for (auto it = Features::Data::BulletTracers.begin(); it != Features::Data::BulletTracers.end(); )
		{
			const float tracerDuration = ConfigManager::F("ESP.TracerDuration");
			if (CurrentTime - it->CreationTime > tracerDuration)
			{
				it = Features::Data::BulletTracers.erase(it);
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

static void RenderESPFeature()
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
		if (!Actor.bHasBoundsWorld)
			continue;

		FVector2D topScreen{};
		FVector2D bottomScreen{};
		FVector2D leftTopScreen{};
		FVector2D rightBottomScreen{};
		if (!ProjectActorScreenBounds(Actor.BoundsCorners, topScreen, bottomScreen, leftTopScreen, rightBottomScreen))
			continue;

		const float Width = (std::max)(0.0f, (float)rightBottomScreen.X - (float)leftTopScreen.X);
		const float Height = (std::max)(0.0f, (float)rightBottomScreen.Y - (float)leftTopScreen.Y);
		if (Height <= 0.0f || Width <= 0.0f) continue;

		ESPActorRenderCache renderCache{};
		renderCache.Actor = &Actor;
		renderCache.TopScreen = topScreen;
		renderCache.BottomScreen = bottomScreen;
		renderCache.LeftTopScreen = leftTopScreen;
		renderCache.RightBottomScreen = rightBottomScreen;
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

		// -- SNAPLINES --
		if (ConfigManager::B("ESP.Snaplines") && GVars.ScreenSize.y > 0.0f)
		{
			FVector2D feetPos;
			if (ProjectForOverlay(Actor.FeetPosWorld, feetPos))
			{
				GUI::Draw::Line(
					ImVec2(GVars.ScreenSize.x * 0.5f, GVars.ScreenSize.y - 10.0f),
					ImVec2((float)feetPos.X, (float)feetPos.Y),
					Actor.Color,
					1.0f,
					Canvas);
			}
		}

		// -- BOXES --
		if (ConfigManager::B("ESP.ShowBox"))
		{
			const int boxType = ConfigManager::I("ESP.BoxType");
			
			if (boxType == 0) // 2D BOX (Head to Feet style)
			{
				FVector2D headScreen, feetScreen;
				FVector topWorld = Actor.HeadPosWorld;
				topWorld.Z += 10.0f;
				FVector bottomWorld = Actor.FeetPosWorld;
				bottomWorld.Z -= 10.0f;

				if (ProjectForOverlay(topWorld, headScreen) && ProjectForOverlay(bottomWorld, feetScreen))
				{
					const float boxHeight = std::abs((float)feetScreen.Y - (float)headScreen.Y);
					const float boxWidth = boxHeight / 1.8f; // Standard human ratio approx

					GUI::Draw::Rect(
						ImVec2((float)feetScreen.X - boxWidth * 0.5f, (float)headScreen.Y),
						ImVec2((float)feetScreen.X + boxWidth * 0.5f, (float)feetScreen.Y),
						Actor.Color,
						1.0f,
						Canvas);
				}
			}
			else if (boxType == 1) // 3D BOX (Rotation-aware)
			{
				FVector root = Actor.FeetPosWorld;
				FVector head = Actor.HeadPosWorld;
				float yaw = Actor.Rotation.Yaw;
				
				// Standard UE coordinate system: X forward, Y right, Z up
				const float boxSize = 50.0f; // Width/Depth half-size
				const float topZ = head.Z + 15.0f;
				const float botZ = root.Z - 10.0f;

				auto GetRotatedPos = [&](float offsetX, float offsetY, float targetZ) -> FVector {
					float rad = yaw * (3.14159265f / 180.0f);
					float s = std::sin(rad);
					float c = std::cos(rad);
					// Rotate point (offsetX, offsetY) by yaw
					return FVector(
						root.X + (offsetX * c - offsetY * s),
						root.Y + (offsetX * s + offsetY * c),
						targetZ
					);
				};

				FVector corners[8];
				corners[0] = GetRotatedPos(boxSize, boxSize, botZ);
				corners[1] = GetRotatedPos(boxSize, -boxSize, botZ);
				corners[2] = GetRotatedPos(-boxSize, -boxSize, botZ);
				corners[3] = GetRotatedPos(-boxSize, boxSize, botZ);
				corners[4] = GetRotatedPos(boxSize, boxSize, topZ);
				corners[5] = GetRotatedPos(boxSize, -boxSize, topZ);
				corners[6] = GetRotatedPos(-boxSize, -boxSize, topZ);
				corners[7] = GetRotatedPos(-boxSize, boxSize, topZ);

				FVector2D screenCorners[8];
				bool valid[8];
				for (int i = 0; i < 8; ++i)
					valid[i] = ProjectForOverlay(corners[i], screenCorners[i]);

				// Bottom square
				if (valid[0] && valid[1]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[0]), Utils::FVector2DToImVec2(screenCorners[1]), Actor.Color, 1.0f, Canvas);
				if (valid[1] && valid[2]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[1]), Utils::FVector2DToImVec2(screenCorners[2]), Actor.Color, 1.0f, Canvas);
				if (valid[2] && valid[3]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[2]), Utils::FVector2DToImVec2(screenCorners[3]), Actor.Color, 1.0f, Canvas);
				if (valid[3] && valid[0]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[3]), Utils::FVector2DToImVec2(screenCorners[0]), Actor.Color, 1.0f, Canvas);

				// Top square
				if (valid[4] && valid[5]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[4]), Utils::FVector2DToImVec2(screenCorners[5]), Actor.Color, 1.0f, Canvas);
				if (valid[5] && valid[6]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[5]), Utils::FVector2DToImVec2(screenCorners[6]), Actor.Color, 1.0f, Canvas);
				if (valid[6] && valid[7]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[6]), Utils::FVector2DToImVec2(screenCorners[7]), Actor.Color, 1.0f, Canvas);
				if (valid[7] && valid[4]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[7]), Utils::FVector2DToImVec2(screenCorners[4]), Actor.Color, 1.0f, Canvas);

				// Verticals
				if (valid[0] && valid[4]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[0]), Utils::FVector2DToImVec2(screenCorners[4]), Actor.Color, 1.0f, Canvas);
				if (valid[1] && valid[5]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[1]), Utils::FVector2DToImVec2(screenCorners[5]), Actor.Color, 1.0f, Canvas);
				if (valid[2] && valid[6]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[2]), Utils::FVector2DToImVec2(screenCorners[6]), Actor.Color, 1.0f, Canvas);
				if (valid[3] && valid[7]) GUI::Draw::Line(Utils::FVector2DToImVec2(screenCorners[3]), Utils::FVector2DToImVec2(screenCorners[7]), Actor.Color, 1.0f, Canvas);
			}
			else if (boxType == 2) // CORNER BOX
			{
				const FVector2D boxPos((float)RenderActor.LeftTopScreen.X, (float)RenderActor.LeftTopScreen.Y);
				const FVector2D boxSize(Width, Height);
				const float lineLen = (std::min)(Width, Height) * 0.25f;

				// Top Left
				GUI::Draw::Line(ImVec2(boxPos.X, boxPos.Y), ImVec2(boxPos.X + lineLen, boxPos.Y), Actor.Color, 1.0f, Canvas);
				GUI::Draw::Line(ImVec2(boxPos.X, boxPos.Y), ImVec2(boxPos.X, boxPos.Y + lineLen), Actor.Color, 1.0f, Canvas);

				// Top Right
				GUI::Draw::Line(ImVec2(boxPos.X + boxSize.X, boxPos.Y), ImVec2(boxPos.X + boxSize.X - lineLen, boxPos.Y), Actor.Color, 1.0f, Canvas);
				GUI::Draw::Line(ImVec2(boxPos.X + boxSize.X, boxPos.Y), ImVec2(boxPos.X + boxSize.X, boxPos.Y + lineLen), Actor.Color, 1.0f, Canvas);

				// Bottom Left
				GUI::Draw::Line(ImVec2(boxPos.X, boxPos.Y + boxSize.Y), ImVec2(boxPos.X + lineLen, boxPos.Y + boxSize.Y), Actor.Color, 1.0f, Canvas);
				GUI::Draw::Line(ImVec2(boxPos.X, boxPos.Y + boxSize.Y), ImVec2(boxPos.X, boxPos.Y + boxSize.Y - lineLen), Actor.Color, 1.0f, Canvas);

				// Bottom Right
				GUI::Draw::Line(ImVec2(boxPos.X + boxSize.X, boxPos.Y + boxSize.Y), ImVec2(boxPos.X + boxSize.X - lineLen, boxPos.Y + boxSize.Y), Actor.Color, 1.0f, Canvas);
				GUI::Draw::Line(ImVec2(boxPos.X + boxSize.X, boxPos.Y + boxSize.Y), ImVec2(boxPos.X + boxSize.X, boxPos.Y + boxSize.Y - lineLen), Actor.Color, 1.0f, Canvas);
			}
		}

		// Health Bar
		if (ConfigManager::B("ESP.ShowHealthBar"))
		{
			const float dpiScale = std::clamp(ImGui::GetIO().DisplaySize.y / 1080.0f, 0.85f, 1.5f);
			const float BarWidth = 4.0f * dpiScale;
			const float BarHeight = Height;
			const float BarX = (float)RenderActor.LeftTopScreen.X - (7.0f * dpiScale);
			const float BarY = (float)RenderActor.LeftTopScreen.Y;

			// Background
			GUI::Draw::RectFilled(ImVec2(BarX, BarY), ImVec2(BarX + BarWidth, BarY + BarHeight), IM_COL32(0, 0, 0, 150), Canvas);

			// Health Fill
			ImU32 HealthColor = IM_COL32(0, 255, 0, 255);
			if (Actor.HealthPct < 0.3f) HealthColor = IM_COL32(255, 0, 0, 255);
			else if (Actor.HealthPct < 0.7f) HealthColor = IM_COL32(255, 255, 0, 255);

			const float FillHeight = BarHeight * std::clamp(Actor.HealthPct, 0.0f, 1.0f);
			GUI::Draw::RectFilled(ImVec2(BarX, BarY + BarHeight - FillHeight), ImVec2(BarX + BarWidth, BarY + BarHeight), HealthColor, Canvas);
		}

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

	void RegisterESPFeature()
	{
		Core::Scheduler::RegisterGameplayTickCallback("ESP", []()
		{
			if (ConfigManager::B("Player.ESP")) UpdateESPFeature();
		});

		Core::Scheduler::RegisterOverlayRenderCallback("ESP", []()
		{
			RenderESPFeature();
		});
	}
}
