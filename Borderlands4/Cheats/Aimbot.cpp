#include "pch.h"

static AActor* CurrentAimbotTarget = nullptr;
namespace
{
	static SDK::FVector g_SilentRedirectTargetPos{};
	static double g_LastSilentArmTime = 0.0;
	static double g_LastSilentProjectileEventTime = 0.0;
	static double g_LastSilentPathProbeTime = 0.0;
	static std::unordered_map<std::string, double> g_SilentPathLastLogTime;
	static SDK::ALightProjectileManager* g_CachedLightProjectileManager = nullptr;
	static double g_LastLightProjectileManagerLookupTime = 0.0;
	static double g_LastSilentLegacyScanTime = 0.0;
	struct ProjectileSilentAssignment
	{
		SDK::TWeakObjectPtr<SDK::AActor> Target;
		double LastSeenTime = 0.0;
	};
	static std::unordered_map<SDK::ULightProjectile*, ProjectileSilentAssignment> g_SilentProjectileAssignments;
	struct PendingSilentSnapshot
	{
		SDK::TWeakObjectPtr<SDK::AActor> Target;
		SDK::FVector TargetPos{};
		double CapturedAt = 0.0;
		uint64 Sequence = 0;
	};
	static PendingSilentSnapshot g_PendingSilentSnapshot{};
	static uint64 g_PendingSnapshotSeq = 0;

	double NowSeconds()
	{
		return static_cast<double>(GetTickCount64()) * 0.001;
	}

	bool IsLocalActor(const SDK::AActor* Actor)
	{
		if (!Actor) return false;
		const SDK::AActor* localCharacter = GVars.Character;
		const SDK::AActor* controlledPawn = (GVars.PlayerController ? static_cast<SDK::AActor*>(GVars.PlayerController->Pawn) : nullptr);
		return Actor == localCharacter || Actor == controlledPawn;
	}

	bool IsLocalLightProjectileSpawn(const SDK::FLightProjectileSpawnData& SpawnData)
	{
		SDK::AActor* localCharacter = GVars.Character;
		SDK::AActor* controlledPawn = (GVars.PlayerController ? static_cast<SDK::AActor*>(GVars.PlayerController->Pawn) : nullptr);
		SDK::AActor* instigator = SpawnData.instigator.Get();
		SDK::AActor* source = SpawnData.Source.Get();
		SDK::AActor* damageCauser = SpawnData.DamageCauser.Get();

		if (localCharacter && (instigator == localCharacter || source == localCharacter || damageCauser == localCharacter))
			return true;
		if (controlledPawn && (instigator == controlledPawn || source == controlledPawn || damageCauser == controlledPawn))
			return true;
		return false;
	}

	bool IsLocalWeapon(const SDK::AWeapon* Weapon)
	{
		if (!Weapon || !GVars.Character) return false;
		auto* localChar = reinterpret_cast<SDK::AOakCharacter*>(GVars.Character);
		for (const auto& slot : localChar->ActiveWeapons.Slots)
		{
			if (slot.Weapon == Weapon) return true;
		}
		return false;
	}

	bool IsLikelyFirePathName(const std::string& functionName)
	{
		return functionName.find("Fire") != std::string::npos ||
			functionName.find("Hit") != std::string::npos ||
			functionName.find("Trace") != std::string::npos ||
			functionName.find("Shot") != std::string::npos ||
			functionName.find("Damage") != std::string::npos ||
			functionName.find("Projectile") != std::string::npos ||
			functionName.find("Impact") != std::string::npos ||
			functionName.find("Use") != std::string::npos ||
			functionName.find("StartUsing") != std::string::npos ||
			functionName.find("StopUsing") != std::string::npos ||
			functionName.find("Server") != std::string::npos;
	}

	bool IsLikelyFirePathClass(const std::string& className)
	{
		return className.find("Weapon") != std::string::npos ||
			className.find("Behavior") != std::string::npos ||
			className.find("Projectile") != std::string::npos ||
			className.find("OakPlayerController") != std::string::npos ||
			className.find("OakCharacter") != std::string::npos;
	}

	SDK::ALightProjectileManager* FindLightProjectileManager()
	{
		if (g_CachedLightProjectileManager)
		{
			if (Utils::IsValidActor(g_CachedLightProjectileManager))
			{
				return g_CachedLightProjectileManager;
			}
			g_CachedLightProjectileManager = nullptr;
		}

		const double now = NowSeconds();
		if ((now - g_LastLightProjectileManagerLookupTime) < 0.25) return nullptr;
		g_LastLightProjectileManagerLookupTime = now;

		SDK::UWorld* world = Utils::GetWorldSafe();
		if (!world) return nullptr;

		auto FindInLevel = [](SDK::ULevel* level) -> SDK::ALightProjectileManager*
		{
			if (!level) return nullptr;
			for (int32 i = 0; i < level->Actors.Num(); ++i)
			{
				SDK::AActor* actor = level->Actors[i];
				if (!actor) continue;
				if (!actor->IsA(SDK::ALightProjectileManager::StaticClass())) continue;
				return static_cast<SDK::ALightProjectileManager*>(actor);
			}
			return nullptr;
		};

		if (SDK::ALightProjectileManager* mgr = FindInLevel(world->PersistentLevel))
		{
			g_CachedLightProjectileManager = mgr;
			return mgr;
		}

		for (int32 i = 0; i < world->StreamingLevels.Num(); ++i)
		{
			SDK::ULevelStreaming* streaming = world->StreamingLevels[i];
			if (!streaming) continue;
			SDK::ULevel* loadedLevel = streaming->GetLoadedLevel();
			if (SDK::ALightProjectileManager* mgr = FindInLevel(loadedLevel))
			{
				g_CachedLightProjectileManager = mgr;
				return mgr;
			}
		}

		return nullptr;
	}

	bool IsProjectileOwnedByLocalPlayer(SDK::ULightProjectile* proj)
	{
		if (!proj) return false;
		SDK::AActor* localCharacter = GVars.Character;
		SDK::AActor* localPawn = (GVars.PlayerController ? static_cast<SDK::AActor*>(GVars.PlayerController->Pawn) : nullptr);
		if (!localCharacter && !localPawn) return false;

		SDK::AActor* instigator = proj->GetAssociatedActor(SDK::ELightProjectileQueryActorType::Instigator);
		SDK::AActor* source = proj->GetAssociatedActor(SDK::ELightProjectileQueryActorType::Source);
		SDK::AActor* damageCauser = proj->GetAssociatedActor(SDK::ELightProjectileQueryActorType::DamageCauser);

		if (localCharacter && (instigator == localCharacter || source == localCharacter || damageCauser == localCharacter)) return true;
		if (localPawn && (instigator == localPawn || source == localPawn || damageCauser == localPawn)) return true;
		return false;
	}

	bool ApplySilentToSingleProjectile(SDK::ULightProjectile* proj, SDK::AActor* currentTarget, const SDK::FVector& currentTargetPos);

	void SetWeakActor(SDK::TWeakObjectPtr<SDK::AActor>& weakPtr, SDK::AActor* actor)
	{
		if (!actor)
		{
			weakPtr.ObjectIndex = 0;
			weakPtr.ObjectSerialNumber = 0;
			return;
		}
		weakPtr.ObjectIndex = actor->Index;
		weakPtr.ObjectSerialNumber = 0;
	}

	SDK::AActor* ResolveWeakActor(const SDK::TWeakObjectPtr<SDK::AActor>& weakPtr)
	{
		return const_cast<SDK::TWeakObjectPtr<SDK::AActor>&>(weakPtr).Get();
	}

	void CaptureSilentSnapshot(SDK::AActor* target, const SDK::FVector& targetPos)
	{
		if (!target) return;
		SetWeakActor(g_PendingSilentSnapshot.Target, target);
		g_PendingSilentSnapshot.TargetPos = targetPos;
		g_PendingSilentSnapshot.CapturedAt = NowSeconds();
		g_PendingSilentSnapshot.Sequence = ++g_PendingSnapshotSeq;
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAim",
			120,
			"Spawn snapshot captured. seq=%llu target=%s",
			static_cast<unsigned long long>(g_PendingSilentSnapshot.Sequence),
			target->GetName().c_str());
	}

	bool GetRecentSilentSnapshot(SDK::AActor*& outTarget, SDK::FVector& outTargetPos)
	{
		outTarget = ResolveWeakActor(g_PendingSilentSnapshot.Target);
		outTargetPos = g_PendingSilentSnapshot.TargetPos;
		if (!outTarget) return false;
		return (NowSeconds() - g_PendingSilentSnapshot.CapturedAt) <= 1.0;
	}

	void ApplySilentHomingProjectiles(SDK::AActor* currentTarget, const SDK::FVector& currentTargetPos)
	{
		if (!currentTarget) return;

		SDK::ALightProjectileManager* mgr = FindLightProjectileManager();

		int changedCount = 0;
		auto ApplyToProjectile = [&](SDK::ULightProjectile* proj)
		{
			if (ApplySilentToSingleProjectile(proj, currentTarget, currentTargetPos)) ++changedCount;
		};

		if (mgr)
		{
			for (int i = 0; i < mgr->ActiveProjectiles.Num(); ++i)
			{
				ApplyToProjectile(mgr->ActiveProjectiles[i]);
			}
		}

		// Cleanup stale assignments for projectiles that no longer exist in active processing window.
		const double now = NowSeconds();
		for (auto it = g_SilentProjectileAssignments.begin(); it != g_SilentProjectileAssignments.end();)
		{
			if (!it->first || (now - it->second.LastSeenTime) > 2.5)
			{
				it = g_SilentProjectileAssignments.erase(it);
			}
			else
			{
				++it;
			}
		}

		if (changedCount > 0)
		{
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAim",
				160,
				"Homing redirect applied to %d light projectile(s). current target=%s",
				changedCount,
				currentTarget->GetName().c_str());
		}
		else
		{
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAim",
				1200,
				"LightProjectileManager found but no local active light projectiles.");
		}
	}

	bool ApplySilentToSingleProjectile(SDK::ULightProjectile* proj, SDK::AActor* currentTarget, const SDK::FVector& currentTargetPos)
	{
		if (!proj || !currentTarget) return false;
		if (!IsProjectileOwnedByLocalPlayer(proj)) return false;

		const std::wstring boneNameWide = UtfN::StringToWString(ConfigManager::S("Aimbot.Bone"));
		const SDK::FName homingBone = SDK::UKismetStringLibrary::Conv_StringToName(boneNameWide.c_str());
		const double now = NowSeconds();

		auto& assignment = g_SilentProjectileAssignments[proj];
		if (!assignment.Target.Get())
		{
			assignment.Target.ObjectIndex = currentTarget->Index;
			assignment.Target.ObjectSerialNumber = 0;
		}
		assignment.LastSeenTime = now;

		SDK::AActor* assignedTarget = assignment.Target.Get();
		if (!assignedTarget)
		{
			assignedTarget = currentTarget;
			assignment.Target.ObjectIndex = currentTarget->Index;
			assignment.Target.ObjectSerialNumber = 0;
		}

		SDK::FVector targetPos = currentTargetPos;
		SDK::USceneComponent* targetComp = nullptr;
		if (assignedTarget->IsA(SDK::ACharacter::StaticClass()))
		{
			SDK::ACharacter* assignedChar = reinterpret_cast<SDK::ACharacter*>(assignedTarget);
			if (assignedChar->Mesh)
			{
				targetComp = assignedChar->Mesh;
				if (assignedChar->Mesh->GetBoneIndex(homingBone) != -1)
				{
					targetPos = assignedChar->Mesh->GetBoneTransform(homingBone, SDK::ERelativeTransformSpace::RTS_World).Translation;
				}
				else
				{
					targetPos = Utils::GetHighestBone(assignedChar);
				}
			}
		}
		else
		{
			targetPos = assignedTarget->K2_GetActorLocation();
		}

		const SDK::FVector delta{
			targetPos.X - proj->Location.X,
			targetPos.Y - proj->Location.Y,
			targetPos.Z - proj->Location.Z
		};
		const float lenSq = static_cast<float>(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
		if (lenSq > 0.0001f)
		{
			const float len = sqrtf(lenSq);
			const SDK::FVector dir{ delta.X / len, delta.Y / len, delta.Z / len };
			float speed = static_cast<float>(
				sqrt(proj->Velocity.X * proj->Velocity.X +
					proj->Velocity.Y * proj->Velocity.Y +
					proj->Velocity.Z * proj->Velocity.Z));
			if (speed < 1.0f)
			{
				speed = (proj->maxspeed > 1.0f) ? proj->maxspeed : 25000.0f;
			}
			proj->Velocity = SDK::FVector{ dir.X * speed, dir.Y * speed, dir.Z * speed };
		}

		proj->HomingTurnSpeedScale = 10000.0f;
		proj->GravityScale = 0.0f;
		proj->SetHoming(true);
		proj->SetHomingTarget(assignedTarget, targetComp, homingBone, SDK::FVector{ 0.0, 0.0, 0.0 });
		proj->SetHomingTargetLocation(targetPos);
		return true;
	}

	bool RedirectLightProjectileSpawnDirection(SDK::FLightProjectileSpawnData& SpawnData, const SDK::FVector& TargetPos)
	{
		const SDK::FVector delta = TargetPos - SpawnData.Location;
		const float lenSq = (float)(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
		if (lenSq < 0.0001f) return false;

		const float len = sqrtf(lenSq);
		SDK::FVector newDirection{ delta.X / len, delta.Y / len, delta.Z / len };
		SpawnData.Direction = newDirection;
		SpawnData.EndLocation = TargetPos;
		return true;
	}

	void LogRedirected(const char* Path, const SDK::FLightProjectileSpawnData& SpawnData)
	{
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAim",
			300,
			"%s redirected. newDir=(%.3f, %.3f, %.3f) end=(%.1f, %.1f, %.1f)",
			Path,
			SpawnData.Direction.X, SpawnData.Direction.Y, SpawnData.Direction.Z,
			SpawnData.EndLocation.X, SpawnData.EndLocation.Y, SpawnData.EndLocation.Z);
	}
}

void Cheats::Aimbot()
{
	bHasAimbotTarget = false;
	CurrentAimbotTarget = nullptr;

	if (!ConfigManager::B("Aimbot.Enabled") || !Utils::bIsInGame) return;
	if (!GVars.POV || !GVars.PlayerController || !Utils::GetSelfActor()) return;

	// Ordinary logic: Target acquisition and visual state
	CurrentAimbotTarget = Utils::GetBestTarget(
		GVars.PlayerController,
		ConfigManager::F("Aimbot.MaxFOV"),
		ConfigManager::B("Aimbot.LOS"),
		ConfigManager::S("Aimbot.Bone"),
		ConfigManager::B("Aimbot.TargetAll")
	);

	if (CurrentAimbotTarget && CurrentAimbotTarget->IsA(ACharacter::StaticClass())) 
	{
		ACharacter* TargetChar = reinterpret_cast<ACharacter*>(CurrentAimbotTarget);
		
		static std::string CachedBoneString = "";
		static FName CachedBoneName;
		if (CachedBoneString != ConfigManager::S("Aimbot.Bone")) {
			std::wstring WideString = UtfN::StringToWString(ConfigManager::S("Aimbot.Bone"));
			CachedBoneName = UKismetStringLibrary::Conv_StringToName(WideString.c_str());
			CachedBoneString = ConfigManager::S("Aimbot.Bone");
		}

		FVector TargetPos;
		if (TargetChar->Mesh && TargetChar->Mesh->GetBoneIndex(CachedBoneName) != -1)
			TargetPos = TargetChar->Mesh->GetBoneTransform(CachedBoneName, ERelativeTransformSpace::RTS_World).Translation;
		else
			TargetPos = Utils::GetHighestBone(TargetChar);

		// Cache for snaplines
		bHasAimbotTarget = true;
		AimbotTargetPos = TargetPos;
	}
}

void Cheats::AimbotHotkey()
{
	// 真正自瞄逻辑：负责旋转相机视角 / 静默重定向武装
	if (!Utils::bIsInGame || !GVars.PlayerController || !GVars.POV || !CurrentAimbotTarget)
	{
		g_LastSilentArmTime = 0.0;
		g_SilentProjectileAssignments.clear();
		SetWeakActor(g_PendingSilentSnapshot.Target, nullptr);
		g_PendingSilentSnapshot.CapturedAt = 0.0;
		return;
	}

    FVector CameraPos = GVars.POV->Location;
    FVector TargetPos = AimbotTargetPos;

	if (ConfigManager::B("Aimbot.Silent"))
	{
		g_SilentRedirectTargetPos = TargetPos;
		g_LastSilentArmTime = NowSeconds();
		if (CurrentAimbotTarget)
		{
			// Keep a low-rate fallback scan; primary path is OnBegin hook-based assignment.
			const double now = NowSeconds();
			if ((now - g_LastSilentLegacyScanTime) > 0.12)
			{
				g_LastSilentLegacyScanTime = now;
				ApplySilentHomingProjectiles(CurrentAimbotTarget, TargetPos);
			}
		}
		if ((g_LastSilentArmTime - g_LastSilentProjectileEventTime) > 1.5)
		{
				Logger::LogThrottled(
					Logger::Level::Debug,
					"SilentAim",
					1200,
					"Armed but no projectile spawn events detected recently. Current weapon may be hitscan or uses another fire path.");
			}
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAim",
			250,
			"Silent redirect armed. target=(%.1f, %.1f, %.1f)",
			TargetPos.X, TargetPos.Y, TargetPos.Z);
			return;
	}
	g_LastSilentArmTime = 0.0;
	g_SilentProjectileAssignments.clear();
	SetWeakActor(g_PendingSilentSnapshot.Target, nullptr);
	g_PendingSilentSnapshot.CapturedAt = 0.0;

    // Actual execution of rotation
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

bool Cheats::HandleAimbotEvents(const SDK::UObject* Object, SDK::UFunction* Function, void* Params)
{
	if (!ConfigManager::B("Aimbot.Enabled") || !ConfigManager::B("Aimbot.Silent")) return false;
	if (!Object || !Function || !Params || !Object->Class) return false;

	const std::string functionName = Function->GetName();
	const std::string className = Object->Class->GetName();
	const double now = NowSeconds();
	const bool bArmed = (now - g_LastSilentArmTime) <= 0.35;
	if (!bArmed)
	{
		return false;
	}

	// Primary hook-based silent path: bind target as each light projectile begins.
	if (functionName == "OnBegin" && className.find("LightProjectileScript") != std::string::npos)
	{
		struct LightProjectileOnBeginParams { SDK::ULightProjectile* projectile; };
		auto* p = reinterpret_cast<LightProjectileOnBeginParams*>(Params);
		if (p && p->projectile && CurrentAimbotTarget)
		{
			if (ApplySilentToSingleProjectile(p->projectile, CurrentAimbotTarget, g_SilentRedirectTargetPos))
			{
				g_LastSilentProjectileEventTime = now;
				Logger::LogThrottled(
					Logger::Level::Debug,
					"SilentAim",
					100,
					"Hooked OnBegin: projectile %d bound to %s",
					p->projectile->SyncID,
					CurrentAimbotTarget->GetName().c_str());
			}
		}
		return false;
	}

	// Additional lifecycle hooks: apply directly when a projectile object receives creation-related events.
	if (Object->IsA(SDK::ULightProjectile::StaticClass()))
	{
		const bool bProjectileLifecycleEvent =
			functionName == "OnBegin" ||
			functionName == "ReceiveBeginPlay" ||
			functionName == "OnActivated" ||
			functionName == "OnSpawned" ||
			functionName == "Initialize" ||
			functionName == "ReceiveTick";
		if (bProjectileLifecycleEvent)
		{
			SDK::AActor* snapshotTarget = nullptr;
			SDK::FVector snapshotPos{};
			if (!GetRecentSilentSnapshot(snapshotTarget, snapshotPos))
			{
				snapshotTarget = CurrentAimbotTarget;
				snapshotPos = g_SilentRedirectTargetPos;
			}
			if (snapshotTarget)
			{
				auto* projectile = static_cast<SDK::ULightProjectile*>(const_cast<SDK::UObject*>(Object));
				if (ApplySilentToSingleProjectile(projectile, snapshotTarget, snapshotPos))
				{
					g_LastSilentProjectileEventTime = now;
					Logger::LogThrottled(
						Logger::Level::Debug,
						"SilentAim",
						90,
						"Projectile lifecycle hook applied. fn=%s proj=%d target=%s seq=%llu",
						functionName.c_str(),
						projectile->SyncID,
						snapshotTarget->GetName().c_str(),
						static_cast<unsigned long long>(g_PendingSilentSnapshot.Sequence));
				}
			}
		}
	}

	// Aggressive probe while LMB is held: capture more candidate fire chains for stubborn hitscan weapons.
	const bool bLmbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (bLmbDown)
	{
		const bool bProbeNoise =
			functionName.find("CameraTransition") != std::string::npos ||
			functionName.find("UpdateLevelVisibility") != std::string::npos;
		if (!bProbeNoise && (IsLikelyFirePathName(functionName) || IsLikelyFirePathClass(className)))
		{
			const std::string key = className + "::" + functionName;
			const auto it = g_SilentPathLastLogTime.find(key);
			const bool bShouldLog = (it == g_SilentPathLastLogTime.end()) || ((now - it->second) > 2.0);
			if (bShouldLog)
			{
				g_SilentPathLastLogTime[key] = now;
				Logger::LogThrottled(
					Logger::Level::Debug,
					"SilentAimPath",
					40,
					"Aggressive fire-path candidate: %s",
					key.c_str());
			}
		}
	}

	// Path probe first: while armed, capture likely local fire chain events even if not projectile-based.
	const bool bProbeName =
		functionName.find("Server") != std::string::npos ||
		functionName.find("StartUsing") != std::string::npos ||
		functionName.find("StopUsing") != std::string::npos ||
		functionName.find("Fire") != std::string::npos ||
		functionName.find("Hit") != std::string::npos ||
		functionName.find("Trace") != std::string::npos ||
		functionName.find("Shot") != std::string::npos ||
		functionName.find("Impact") != std::string::npos ||
		functionName.find("Damage") != std::string::npos ||
		functionName.find("Projectile") != std::string::npos ||
		functionName.find("Reload") != std::string::npos;
	const bool bProbeNoise =
		functionName.find("CameraTransition") != std::string::npos ||
		functionName.find("UpdateLevelVisibility") != std::string::npos;
	const bool bProbeClass =
		className.find("Weapon") != std::string::npos ||
		className.find("Projectile") != std::string::npos ||
		className.find("OakPlayerController") != std::string::npos;

	if (bProbeName && !bProbeNoise && bProbeClass)
	{
		bool bLikelyLocal = false;
		if (Object->IsA(SDK::AWeapon::StaticClass()))
		{
			bLikelyLocal = IsLocalWeapon(reinterpret_cast<const SDK::AWeapon*>(Object));
		}
		else
		{
			// Helper/static classes: still useful for path discovery.
			bLikelyLocal = true;
		}

		if (bLikelyLocal && (now - g_LastSilentPathProbeTime) > 0.08)
		{
			g_LastSilentPathProbeTime = now;
			Logger::LogThrottled(
				Logger::Level::Debug,
				"SilentAimPath",
				80,
				"Candidate fire path: %s::%s",
				className.c_str(),
				functionName.c_str());
		}
	}

	const bool bLightPath = functionName.find("SpawnLightProjectile") != std::string::npos;
	const bool bProjectilePath = functionName.find("SpawnProjectile_") != std::string::npos;
	if (!bLightPath && !bProjectilePath)
	{
		return false;
	}
	g_LastSilentProjectileEventTime = now;

	// ----- LightProjectile paths -----
	struct LightDataParams { SDK::FLightProjectileSpawnData Data; };
	struct LightQueryParams { uint8 Pad_00[0x28]; SDK::FLightProjectileSpawnData ProjectileData; };
	struct LightQueryConstParams { SDK::FLightProjectileSpawnData ProjectileData; };
	struct LightThrowAtActorParams { uint8 Pad_00[0x230]; SDK::AActor* target; };
	struct LightThrowAtLocationParams { uint8 Pad_00[0x230]; SDK::FVector Location; };

	if (functionName == "SpawnLightProjectile" ||
		functionName == "SpawnLightProjectile_Source" ||
		functionName == "SpawnLightProjectile_ThrowAtCrosshair" ||
		functionName == "SpawnLightProjectileAsync")
	{
		auto* p = reinterpret_cast<LightDataParams*>(Params);
		if (!p || !IsLocalLightProjectileSpawn(p->Data))
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 1000, "Light path not local; skip.");
			return false;
		}
		if (RedirectLightProjectileSpawnDirection(p->Data, g_SilentRedirectTargetPos))
		{
			LogRedirected(functionName.c_str(), p->Data);
			if (CurrentAimbotTarget) CaptureSilentSnapshot(CurrentAimbotTarget, g_SilentRedirectTargetPos);
		}
		return false;
	}

	if (functionName == "SpawnLightProjectiles_Query")
	{
		auto* p = reinterpret_cast<LightQueryParams*>(Params);
		if (!p || !IsLocalLightProjectileSpawn(p->ProjectileData))
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 1000, "Light query path not local; skip.");
			return false;
		}
		if (RedirectLightProjectileSpawnDirection(p->ProjectileData, g_SilentRedirectTargetPos))
		{
			LogRedirected(functionName.c_str(), p->ProjectileData);
			if (CurrentAimbotTarget) CaptureSilentSnapshot(CurrentAimbotTarget, g_SilentRedirectTargetPos);
		}
		return false;
	}

	if (functionName == "SpawnLightProjectiles_Query_Const")
	{
		auto* p = reinterpret_cast<LightQueryConstParams*>(Params);
		if (!p || !IsLocalLightProjectileSpawn(p->ProjectileData))
		{
			Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 1000, "Light query const path not local; skip.");
			return false;
		}
		if (RedirectLightProjectileSpawnDirection(p->ProjectileData, g_SilentRedirectTargetPos))
		{
			LogRedirected(functionName.c_str(), p->ProjectileData);
			if (CurrentAimbotTarget) CaptureSilentSnapshot(CurrentAimbotTarget, g_SilentRedirectTargetPos);
		}
		return false;
	}

	if (functionName == "SpawnLightProjectile_ThrowAtActor")
	{
		auto* p = reinterpret_cast<LightThrowAtActorParams*>(Params);
		if (p && CurrentAimbotTarget)
		{
			p->target = CurrentAimbotTarget;
			Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 300, "ThrowAtActor target overridden -> %s", CurrentAimbotTarget->GetName().c_str());
		}

		auto* pd = reinterpret_cast<LightDataParams*>(Params);
		if (pd && IsLocalLightProjectileSpawn(pd->Data) &&
			RedirectLightProjectileSpawnDirection(pd->Data, g_SilentRedirectTargetPos))
		{
			LogRedirected(functionName.c_str(), pd->Data);
			if (CurrentAimbotTarget) CaptureSilentSnapshot(CurrentAimbotTarget, g_SilentRedirectTargetPos);
		}
		return false;
	}

	if (functionName == "SpawnLightProjectile_ThrowAtLocation")
	{
		auto* p = reinterpret_cast<LightThrowAtLocationParams*>(Params);
		if (p)
		{
			p->Location = g_SilentRedirectTargetPos;
			Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 300, "ThrowAtLocation overridden -> (%.1f, %.1f, %.1f)",
				p->Location.X, p->Location.Y, p->Location.Z);
		}

		auto* pd = reinterpret_cast<LightDataParams*>(Params);
		if (pd && IsLocalLightProjectileSpawn(pd->Data) &&
			RedirectLightProjectileSpawnDirection(pd->Data, g_SilentRedirectTargetPos))
		{
			LogRedirected(functionName.c_str(), pd->Data);
			if (CurrentAimbotTarget) CaptureSilentSnapshot(CurrentAimbotTarget, g_SilentRedirectTargetPos);
		}
		return false;
	}

	// ----- ProjectileStatics paths -----
	struct ProjectileThrowAtActorParams { uint8 Pad_00[0x40]; SDK::AActor* Source; uint8 Pad_48[0x90]; SDK::AActor* target; };
	struct ProjectileThrowAtActorConstParams { uint8 Pad_00[0x18]; SDK::AActor* Source; uint8 Pad_20[0x90]; SDK::AActor* target; };
	struct ProjectileThrowAtLocationParams { uint8 Pad_00[0x40]; SDK::AActor* Source; uint8 Pad_48[0x90]; SDK::FVector Location; };
	struct ProjectileThrowAtLocationConstParams { uint8 Pad_00[0x18]; SDK::AActor* Source; uint8 Pad_20[0x90]; SDK::FVector Location; };

	if ((functionName == "SpawnProjectile_ThrowAtActor" || functionName == "SpawnProjectile_ThrowAtActor_Const") && CurrentAimbotTarget)
	{
		if (functionName == "SpawnProjectile_ThrowAtActor")
		{
			auto* p = reinterpret_cast<ProjectileThrowAtActorParams*>(Params);
			if (p && IsLocalActor(p->Source))
			{
				p->target = CurrentAimbotTarget;
				Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 300, "ProjectileStatics ThrowAtActor target overridden -> %s", CurrentAimbotTarget->GetName().c_str());
			}
		}
		else
		{
			auto* p = reinterpret_cast<ProjectileThrowAtActorConstParams*>(Params);
			if (p && IsLocalActor(p->Source))
			{
				p->target = CurrentAimbotTarget;
				Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 300, "ProjectileStatics ThrowAtActor_Const target overridden -> %s", CurrentAimbotTarget->GetName().c_str());
			}
		}
		return false;
	}

	if (functionName == "SpawnProjectile_ThrowAtLocation" || functionName == "SpawnProjectile_ThrowAtLocation_Const")
	{
		if (functionName == "SpawnProjectile_ThrowAtLocation")
		{
			auto* p = reinterpret_cast<ProjectileThrowAtLocationParams*>(Params);
			if (p && IsLocalActor(p->Source))
			{
				p->Location = g_SilentRedirectTargetPos;
				Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 300, "ProjectileStatics ThrowAtLocation overridden -> (%.1f, %.1f, %.1f)",
					p->Location.X, p->Location.Y, p->Location.Z);
			}
		}
		else
		{
			auto* p = reinterpret_cast<ProjectileThrowAtLocationConstParams*>(Params);
			if (p && IsLocalActor(p->Source))
			{
				p->Location = g_SilentRedirectTargetPos;
				Logger::LogThrottled(Logger::Level::Debug, "SilentAim", 300, "ProjectileStatics ThrowAtLocation_Const overridden -> (%.1f, %.1f, %.1f)",
					p->Location.X, p->Location.Y, p->Location.Z);
			}
		}
		return false;
	}

	if (functionName == "SpawnProjectile_ThrowAtCrosshair" || functionName == "SpawnProjectile_ThrowAtCrosshair_Const")
	{
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAim",
			1000,
			"%s detected. This path currently uses SourceRotation/trajectory, location override is unavailable in this hook.",
			functionName.c_str());
		return false;
	}

	return false;
}

void Cheats::HandleConstructedObject(const SDK::UObject* Object)
{
	if (!Object || !Object->Class) return;
	if (!ConfigManager::B("Aimbot.Enabled") || !ConfigManager::B("Aimbot.Silent")) return;
	if (!Utils::bIsInGame) return;
	if (!Object->IsA(SDK::ULightProjectile::StaticClass())) return;

	SDK::AActor* snapshotTarget = nullptr;
	SDK::FVector snapshotPos{};
	if (!GetRecentSilentSnapshot(snapshotTarget, snapshotPos))
	{
		snapshotTarget = CurrentAimbotTarget;
		snapshotPos = g_SilentRedirectTargetPos;
	}
	if (!snapshotTarget) return;

	auto* projectile = static_cast<SDK::ULightProjectile*>(const_cast<SDK::UObject*>(Object));
	if (ApplySilentToSingleProjectile(projectile, snapshotTarget, snapshotPos))
	{
		g_LastSilentProjectileEventTime = NowSeconds();
		Logger::LogThrottled(
			Logger::Level::Debug,
			"SilentAim",
			80,
			"StaticConstruct hook applied. proj=%d target=%s seq=%llu",
			projectile->SyncID,
			snapshotTarget->GetName().c_str(),
			static_cast<unsigned long long>(g_PendingSilentSnapshot.Sequence));
	}
}
