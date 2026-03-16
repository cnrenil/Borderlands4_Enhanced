#include "pch.h"


namespace
{
    thread_local UCanvas* g_CurrentCanvas = nullptr;
    std::atomic<bool> g_LoggedDrawFovCanvas{ false };
    std::atomic<bool> g_LoggedDrawFovImGui{ false };
    std::atomic<bool> g_LoggedSnapLineCanvas{ false };
    std::atomic<bool> g_LoggedSnapLineImGui{ false };

    // Guard against stale UObject pointers during map transition/game shutdown.
    bool IsReadableUObject(const void* Ptr)
    {
        return Ptr && !IsBadReadPtr(Ptr, sizeof(void*));
    }

    bool TryReadLevelActorCount(ULevel* Level, int32_t& OutActorCount)
    {
        __try
        {
            OutActorCount = Level->Actors.Num();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            OutActorCount = -1;
            return false;
        }
    }

    ImVec2 GetAimScreenCenter()
    {
        ImVec2 fallbackCenter(GVars.ScreenSize.x * 0.5f, GVars.ScreenSize.y * 0.5f);
        if (!ConfigManager::B("Player.OverShoulder")) return fallbackCenter;
        if (GVars.PlayerController && GVars.PlayerController->PlayerCameraManager)
        {
            const FMinimalViewInfo& CameraPOV = GVars.PlayerController->PlayerCameraManager->CameraCachePrivate.POV;
            const FVector camLoc = CameraPOV.Location;
            const FVector camFwd = Utils::FRotatorToVector(CameraPOV.Rotation);
            const FVector aimPoint = camLoc + (camFwd * 50000.0f);
            FVector2D aimScreen{};
            if (GVars.PlayerController->ProjectWorldLocationToScreen(aimPoint, &aimScreen, true)) {
                return ImVec2((float)aimScreen.X, (float)aimScreen.Y);
            }
        }

        return fallbackCenter;
    }
}

std::recursive_mutex gGVarsMutex;


UWorld* Utils::GetWorldSafe() 
{
    UWorld* World = nullptr;
    UEngine* Engine = UEngine::GetEngine();
    if (!Engine) return nullptr;

    UGameViewportClient* Viewport = Engine->GameViewport;
    if (!Viewport) return nullptr;

    World = Viewport->World;
    return World;
}


APlayerController* Utils::GetPlayerController()
{
	UWorld* World = GetWorldSafe();
	if (!World) return nullptr;

	UGameInstance* GameInstance = World->OwningGameInstance;
	if (!GameInstance) return nullptr;

	if (GameInstance->LocalPlayers.Num() <= 0) return nullptr;

	ULocalPlayer* LocalPlayer = GameInstance->LocalPlayers[0];
	if (!LocalPlayer) return nullptr;

	APlayerController* PlayerController = LocalPlayer->PlayerController;
	if (!PlayerController || !Utils::IsValidActor(PlayerController)) return nullptr;

	return PlayerController;
}

AActor* Utils::GetSelfActor()
{
    APawn* controlledPawn = nullptr;
    if (GVars.PlayerController && Utils::IsValidActor(GVars.PlayerController))
        controlledPawn = GVars.PlayerController->Pawn;

    if (GVars.Character && Utils::IsValidActor(GVars.Character))
        return GVars.Character;

    APawn* pawn = nullptr;
    if (controlledPawn && Utils::IsValidActor(controlledPawn))
        pawn = controlledPawn;
    else if (GVars.Pawn && Utils::IsValidActor(GVars.Pawn))
        pawn = GVars.Pawn;

    if (pawn && pawn->IsA(AOakVehicle::StaticClass()))
    {
        auto* vehicle = reinterpret_cast<AOakVehicle*>(pawn);
        if (vehicle->DriverPawn && Utils::IsValidActor(vehicle->DriverPawn))
            return vehicle->DriverPawn;
        return vehicle;
    }

    if (pawn && Utils::IsValidActor(pawn))
        return pawn;

    return nullptr;
}

unsigned Utils::ConvertImVec4toU32(ImVec4 Color)
{
    return IM_COL32((int)(Color.x * 255.0f), (int)(Color.y * 255.0f), (int)(Color.z * 255.0f), (int)(Color.w * 255.0f));
}

void Utils::SetCurrentCanvas(UCanvas* Canvas)
{
    g_CurrentCanvas = Canvas;
}

UCanvas* Utils::GetCurrentCanvas()
{
    return g_CurrentCanvas;
}

FLinearColor Utils::ImVec4ToLinearColor(const ImVec4& Color)
{
    return FLinearColor(Color.x, Color.y, Color.z, Color.w);
}

FLinearColor Utils::U32ToLinearColor(ImU32 Color)
{
    const float inv255 = 1.0f / 255.0f;
    return FLinearColor(
        ((Color >> IM_COL32_R_SHIFT) & 0xFF) * inv255,
        ((Color >> IM_COL32_G_SHIFT) & 0xFF) * inv255,
        ((Color >> IM_COL32_B_SHIFT) & 0xFF) * inv255,
        ((Color >> IM_COL32_A_SHIFT) & 0xFF) * inv255);
}

ALightProjectileManager* Utils::GetLightProjManager()
{
    static ALightProjectileManager* CachedManager = nullptr;
    if (CachedManager && !IsBadReadPtr(CachedManager, sizeof(void*)) && CachedManager->VTable)
        return CachedManager;
        
    if (!GVars.Level) return nullptr;

    Utils::ForEachLevelActor(GVars.Level, [&](AActor* Actor)
    {
        if (Actor && !IsBadReadPtr(Actor, sizeof(void*)) && Actor->VTable && Actor->IsA(ALightProjectileManager::StaticClass()))
        {
            CachedManager = reinterpret_cast<ALightProjectileManager*>(Actor);
            return false;
        }
        return true;
    });
    return nullptr;
}

void Utils::PrintActors(const char* Exclude)
{
    if (!GVars.World || !GVars.World->VTable) return;
	ULevel* Level = GVars.Level;
	if (Level)
	{
		Utils::ForEachLevelActor(Level, [&](AActor* Actor)
		{
			if (!Actor || !Utils::IsValidActor(Actor)) return true;
			if (Exclude && Actor->GetName().find(Exclude) != std::string::npos)
				return true;

			LOG_INFO("Scanner", "Actor: %s - Class: %s", Actor->GetName().c_str(), Actor->Class->Name.ToString().c_str());
			return true;
		});
	}
}

FRotator Utils::VectorToRotation(const FVector& Vec)
{
    FRotator Rot;
    Rot.Yaw = std::atan2(Vec.Y, Vec.X) * (180.0 / std::numbers::pi);
    Rot.Pitch = std::atan2(Vec.Z, std::sqrt(Vec.X * Vec.X + Vec.Y * Vec.Y)) * (180.0 / std::numbers::pi);
    Rot.Roll = 0.0;
    return Rot;
}

FVector Utils::FRotatorToVector(const FRotator& Rot)
{
    double PitchRad = Rot.Pitch * (std::numbers::pi / 180.0);
    double YawRad = Rot.Yaw * (std::numbers::pi / 180.0);

    double CP = cos(PitchRad);
    double SP = sin(PitchRad);
    double CY = cos(YawRad);
    double SY = sin(YawRad);

    return FVector(
        CP * CY,   // X
        CP * SY,   // Y
        SP         // Z
    ).GetNormalized(); // normalize just in case
}

PlayerCheatData& Utils::GetPlayerCheats(ACharacter* Player)
{
    return PlayerCheatMap[Player];
}

bool Utils::IsValidActor(AActor* Actor)
{
    if (!Actor) return false;

    __try
    {
        if (IsBadReadPtr(Actor, sizeof(void*)) || !Actor->VTable) return false;
        if (!Actor->Class || IsBadReadPtr(Actor->Class, sizeof(void*))) return false;

        // Avoid ProcessEvent-based validity checks on possibly stale objects.
        const EObjectFlags Flags = Actor->Flags;
        if (Flags & EObjectFlags::BeginDestroyed) return false;
        if (Flags & EObjectFlags::FinishDestroyed) return false;
        if (Flags & EObjectFlags::MirroredGarbage) return false;
        if (Flags & EObjectFlags::TagGarbageTemp) return false;

        if (Actor->bActorIsBeingDestroyed) return false;
        if (!Actor->RootComponent) return false;

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool Utils::ForEachLevelActor(ULevel* Level, const std::function<bool(AActor*)>& Visitor, int32_t* OutActorCount)
{
    if (OutActorCount) *OutActorCount = 0;
    if (!Level || !Visitor) return false;

    int32_t actorCount = 0;
    if (!TryReadLevelActorCount(Level, actorCount))
        return false;

    if (actorCount <= 0 || actorCount > 200000)
        return false;

    if (OutActorCount) *OutActorCount = actorCount;

    __try
    {
        for (int32_t i = 0; i < actorCount; ++i)
        {
            if (!Visitor(Level->Actors[i]))
                break;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

float Utils::GetDistanceMeters(const FVector& A, const FVector& B)
{
    const double distanceMeters = static_cast<double>(A.GetDistanceToInMeters(B));

    if (!std::isfinite(distanceMeters))
        return FLT_MAX;

    return static_cast<float>(distanceMeters);
}

float Utils::GetDistanceMeters(const AActor* Source, const AActor* Target)
{
    if (!Source || !Target)
        return FLT_MAX;
    if (IsBadReadPtr(Source, sizeof(void*)) || IsBadReadPtr(Target, sizeof(void*)))
        return FLT_MAX;
    if (!Source->VTable || !Target->VTable)
        return FLT_MAX;

    const FVector sourceLocation = Source->K2_GetActorLocation();
    const FVector targetLocation = Target->K2_GetActorLocation();
    const float distanceMeters = sourceLocation.GetDistanceToInMeters(targetLocation);
    if (!std::isfinite(distanceMeters))
        return FLT_MAX;

    return distanceMeters;
}

float Utils::GetFOVFromScreenCoords(const ImVec2& ScreenLocation)
{
    ImVec2 ScreenCenter = GetAimScreenCenter();

    float DeltaX = ScreenLocation.x - ScreenCenter.x;
    float DeltaY = ScreenLocation.y - ScreenCenter.y;

    return std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
}

ImVec2 Utils::FVector2DToImVec2(FVector2D Vector)
{
	return ImVec2((float)Vector.X, (float)Vector.Y);
}

FRotator Utils::GetRotationToTarget(const FVector& Start, const FVector& Target)
{
    FVector Delta = Target - Start;
    Delta.Normalize(); // Important for safe calculations

    FRotator Rotation;
    Rotation.Pitch = std::atan2(Delta.Z, std::sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y)) * (180.0f / std::numbers::pi);
    Rotation.Yaw = std::atan2(Delta.Y, Delta.X) * (180.0f / std::numbers::pi);
    Rotation.Roll = 0.0f;

    return Rotation;
}

FVector2D Utils::ImVec2ToFVector2D(ImVec2 Vector)
{
	return FVector2D(Vector.x, Vector.y);
}

TargetSelectionResult Utils::AcquireTarget(APlayerController* ViewPoint, float MaxFOV, float MinDistance, float MaxDistance, bool RequiresLOS, std::string TargetBone, bool TargetAll, int TargetMode)
{
    TargetSelectionResult bestResult{};
    if (Utils::bIsLoading || !GVars.World || !GVars.World->VTable) return bestResult;
    if (!GVars.Level || !GVars.GameState || !ViewPoint || !ViewPoint->PlayerCameraManager) return bestResult;

    const FVector CameraLocation = ViewPoint->PlayerCameraManager->CameraCachePrivate.POV.Location;
    FVector2D ViewportSize = Utils::ImVec2ToFVector2D(GVars.ScreenSize);
    FVector2D ViewportCenter = Utils::ImVec2ToFVector2D(GetAimScreenCenter());
    float MaxFOVNormalized = MaxFOV / 90.0f;

    AActor* SelfActor = Utils::GetSelfActor();
    const FVector DistanceOrigin = SelfActor ? SelfActor->K2_GetActorLocation() : CameraLocation;
    const float clampedMinDistance = (std::max)(0.0f, MinDistance);
    const float clampedMaxDistance = (MaxDistance <= 0.0f) ? FLT_MAX : MaxDistance;

    auto IsBetterCandidate = [&](float candidateDistance, float candidateFOV) -> bool
    {
        if (!bestResult.IsValid())
            return true;

        switch (TargetMode)
        {
        case 1:
            if (candidateDistance < bestResult.DistanceMeters - 0.001f) return true;
            if (fabsf(candidateDistance - bestResult.DistanceMeters) <= 0.001f && candidateFOV < bestResult.ScreenSpaceFOV) return true;
            return false;
        case 0:
        default:
            if (candidateFOV < bestResult.ScreenSpaceFOV - 0.001f) return true;
            if (fabsf(candidateFOV - bestResult.ScreenSpaceFOV) <= 0.001f && candidateDistance < bestResult.DistanceMeters) return true;
            return false;
        }
    };

    // Use Cache!
    for (ACharacter* TargetChar : GVars.UnitCache)
    {
        if (!TargetChar || !Utils::IsValidActor(TargetChar))
            continue;

        if (SelfActor && TargetChar == SelfActor)
            continue;

        // Skip non-hostiles unless TargetAll is set
        if (!TargetAll && Utils::GetAttitude(TargetChar) != ETeamAttitude::Hostile)
            continue;

        // Skip dead
        if (Utils::GetHealthPercent(TargetChar) <= 0.0f)
            continue;

        // Ensure Mesh is valid before proceeding
        if (!TargetChar->Mesh)
            continue;

        // Quick Distance Check (optional but good)
        const FVector ActorLoc = TargetChar->K2_GetActorLocation();
        const float DistanceMeters = SelfActor
            ? Utils::GetDistanceMeters(SelfActor, TargetChar)
            : Utils::GetDistanceMeters(DistanceOrigin, ActorLoc);
        if (DistanceMeters < clampedMinDistance || DistanceMeters > clampedMaxDistance)
            continue;

        // Project to screen first
        FVector2D ScreenLocation;
        if (!ViewPoint->ProjectWorldLocationToScreen(ActorLoc, &ScreenLocation, true))
            continue;

        FVector2D Delta = ScreenLocation - ViewportCenter;
        float DeltaLength = sqrtf((float)Delta.X * (float)Delta.X + (float)Delta.Y * (float)Delta.Y);
        float NormalizedOffset = DeltaLength / ((float)ViewportSize.Y * 0.5f);
        
        // If not even close to the FOV circle, skip
        if (NormalizedOffset > MaxFOVNormalized * 2.0f) 
            continue;

        // Now get the actual bone location
        const FVector BoneLocation = Utils::GetBestAimPoint(TargetChar, TargetBone);

        // LOS Check is the most expensive, do it last
        bool bHasLOS = true;
        if (RequiresLOS)
        {
            // Internal sight check using camera location as origin
            bHasLOS = GVars.PlayerController->LineOfSightTo(TargetChar, CameraLocation, true);
            if (!bHasLOS)
                continue;
        }

        // Recalculate precise FOV with bone location
        if (ViewPoint->ProjectWorldLocationToScreen(BoneLocation, &ScreenLocation, true))
        {
            Delta = ScreenLocation - ViewportCenter;
            DeltaLength = sqrtf((float)Delta.X * (float)Delta.X + (float)Delta.Y * (float)Delta.Y);
            NormalizedOffset = DeltaLength / ((float)ViewportSize.Y * 0.5f);

            float FOV = NormalizedOffset * 90.0f;

            if (NormalizedOffset < MaxFOVNormalized && IsBetterCandidate(DistanceMeters, FOV))
            {
                bestResult.Target = TargetChar;
                bestResult.AimPoint = BoneLocation;
                bestResult.ScreenLocation = ScreenLocation;
                bestResult.DistanceMeters = DistanceMeters;
                bestResult.ScreenSpaceFOV = FOV;
                bestResult.bHasLOS = bHasLOS;
            }
        }
    }
    return bestResult;
}

void Utils::DrawFOV(float MaxFOV, float Thickness = 1.0f)
{
    FVector2D ViewportSize = Utils::ImVec2ToFVector2D(GVars.ScreenSize);
    FVector2D Center = Utils::ImVec2ToFVector2D(GetAimScreenCenter());

    float MaxFOVNormalized = MaxFOV / 90.0f;
    float RadiusPixels = MaxFOVNormalized * ((float)ViewportSize.Y * 0.5f);

    if (GUI::Draw::ResolveBackend() == GUI::Draw::Backend::UCanvas)
    {
        if (!g_LoggedDrawFovCanvas.exchange(true))
        {
            LOG_INFO("DrawPath", "DrawFOV using UCanvas path.");
        }
    }
    else if (!g_LoggedDrawFovImGui.exchange(true))
    {
        LOG_WARN("DrawPath", "DrawFOV using ImGui fallback path (Canvas unavailable).");
    }

    GUI::Draw::Circle(ImVec2(Center.X, Center.Y), RadiusPixels, IM_COL32(255, 0, 0, 255), 64, Thickness);
}

void Utils::DrawSnapLine(FVector TargetPos, float Thickness = 2.0f)
{
    FVector2D ScreenPos;

    if (!GVars.PlayerController->ProjectWorldLocationToScreen(TargetPos, &ScreenPos, true))
        return;

    ImVec2 Center = GetAimScreenCenter();
    ImVec2 Target((float)ScreenPos.X, (float)ScreenPos.Y);

    if (GUI::Draw::ResolveBackend() == GUI::Draw::Backend::UCanvas)
    {
        if (!g_LoggedSnapLineCanvas.exchange(true))
        {
            LOG_INFO("DrawPath", "DrawSnapLine using UCanvas path.");
        }
    }
    else if (!g_LoggedSnapLineImGui.exchange(true))
    {
        LOG_WARN("DrawPath", "DrawSnapLine using ImGui fallback path (Canvas unavailable).");
    }

    GUI::Draw::Line(Center, Target, IM_COL32(255, 255, 255, 180), Thickness);

    const float angle = atan2f(Target.y - Center.y, Target.x - Center.x);
    const float arrowSize = 10.0f;
    const ImVec2 p1(Target.x - arrowSize * cosf(angle - 0.5f), Target.y - arrowSize * sinf(angle - 0.5f));
    const ImVec2 p2(Target.x - arrowSize * cosf(angle + 0.5f), Target.y - arrowSize * sinf(angle + 0.5f));

    GUI::Draw::TriangleFilled(Target, p1, p2, IM_COL32(255, 255, 255, 220));
    GUI::Draw::CircleFilled(Target, 2.5f, IM_COL32(0, 255, 0, 255));

}

void Utils::Error(std::string msg)
{
    printf("[Error] %s\n", msg.c_str());
}

bool Utils::IsInLoadingState()
{
    if (!IsReadableUObject(GVars.World) || !GVars.World->VTable) {
        Logger::LogThrottled(Logger::Level::Debug, "System", 5000, "LoadingState: World is NULL/Invalid");
        return true;
    }
    
    if (!IsReadableUObject(GVars.Level) || !GVars.Level->VTable) {
        Logger::LogThrottled(Logger::Level::Debug, "System", 5000, "LoadingState: PersistentLevel is NULL");
        return true;
    }

    if (!IsReadableUObject(GVars.GameState) || !GVars.GameState->VTable) {
        // Some phases keep gameplay alive while GameState is not ready yet.
        Logger::LogThrottled(Logger::Level::Debug, "System", 5000, "LoadingState: GameState not ready yet");
    }

    int32_t actorCount = 0;
    if (!TryReadLevelActorCount(GVars.Level, actorCount))
    {
        Logger::LogThrottled(Logger::Level::Warning, "System", 2000, "LoadingState: exception while reading Level->Actors (likely unloading)");
        return true;
    }

    if (actorCount < 1 || actorCount > 200000)
    {
        Logger::LogThrottled(Logger::Level::Debug, "System", 5000, "LoadingState: Level actor count invalid (%d)", actorCount);
        return true;
    }

    return false;
}

bool Utils::IsInPlayableState()
{
    if (Utils::bIsLoading) return false;
    if (!GVars.World || !GVars.World->VTable) return false;
    if (!GVars.Level || !GVars.GameState) return false;
    if (!GVars.PlayerController || !GVars.PlayerController->VTable) return false;
    if (!GVars.PlayerController->PlayerCameraManager || !GVars.PlayerController->PlayerCameraManager->VTable) return false;
    if (!Utils::GetSelfActor()) return false;
    return true;
}

void cerrf(const char* Format, ...)
{
    va_list Args;
    va_start(Args, Format);

    // Print to stderr
    vfprintf(stderr, Format, Args);

    va_end(Args);
}

ACharacter* Utils::GetNearestCharacter(ETeam Team)
{
    if (!GVars.World || !GVars.World->VTable) return nullptr;
    if (!GVars.Level) return nullptr;
    AActor* SelfActor = Utils::GetSelfActor();
    if (!SelfActor) return nullptr;
    
    ACharacter* NearestCharacter = nullptr;
    float NearestDistance = FLT_MAX;

    for (ACharacter* TargetChar : GVars.UnitCache)
    {
        if (!TargetChar || !Utils::IsValidActor(TargetChar))
            continue;

        if (TargetChar == SelfActor)
            continue;

        FVector PlayerLocation = SelfActor->K2_GetActorLocation();
        FVector TargetLocation = TargetChar->K2_GetActorLocation();
        float Distance = (float)PlayerLocation.GetDistanceTo(TargetLocation);
        if (Distance < NearestDistance)
        {
            NearestDistance = Distance;
            NearestCharacter = TargetChar;
        }
    }
	return NearestCharacter;
}
 
ETeamAttitude Utils::GetAttitude(AActor* Target)
{
	AActor* SelfActor = Utils::GetSelfActor();
	if (!SelfActor || !Target) return ETeamAttitude::Neutral;
	return UGbxTeamFunctionLibrary::GetAttitudeTowards(SelfActor, Target);
}
 
float Utils::GetHealthPercent(AActor* Actor)
{
	if (!Actor) return 0.0f;
	return UDamageStatics::GetHealthPoolPercent(Actor, 0); // layer 0 usually is health
}

namespace
{
    std::string ToLowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool IsAuxiliaryBoneName(const std::string& boneNameLower)
    {
        static const std::array<const char*, 16> kAuxPatterns = {
            "ik_",
            "_ik",
            "twist",
            "socket",
            "weapon",
            "camera",
            "attach",
            "fx_",
            "_fx",
            "cloth",
            "corrective",
            "driver",
            "ctrl",
            "control",
            "virtual",
            "lookat"
        };

        if (boneNameLower.find("vb ") != std::string::npos || boneNameLower.find("vb_") != std::string::npos)
            return true;
        if (boneNameLower.find("_end") != std::string::npos || boneNameLower.find("end_") != std::string::npos)
            return true;

        for (const char* pattern : kAuxPatterns)
        {
            if (boneNameLower.find(pattern) != std::string::npos)
                return true;
        }
        return false;
    }

    bool IsFiniteVector(const FVector& point)
    {
        return std::isfinite(static_cast<float>(point.X)) &&
               std::isfinite(static_cast<float>(point.Y)) &&
               std::isfinite(static_cast<float>(point.Z));
    }

    bool BuildReliableMeshBounds(ACharacter* TargetChar, FVector& OutOrigin, FVector& OutExtent, std::vector<std::pair<std::string, FVector>>* OutSampledBones)
    {
        if (OutSampledBones)
            OutSampledBones->clear();
        if (!TargetChar || !TargetChar->Mesh)
            return false;

        const int32_t numBones = TargetChar->Mesh->GetNumBones();
        if (numBones <= 0 || numBones > 5000)
            return false;

        FVector actorOrigin{};
        FVector actorExtent{};
        TargetChar->GetActorBounds(false, &actorOrigin, &actorExtent, false);
        const float maxDx = (std::max)(150.0f, static_cast<float>(actorExtent.X) * 3.5f);
        const float maxDy = (std::max)(150.0f, static_cast<float>(actorExtent.Y) * 3.5f);
        const float maxDz = (std::max)(150.0f, static_cast<float>(actorExtent.Z) * 3.5f);

        float minX = FLT_MAX;
        float minY = FLT_MAX;
        float minZ = FLT_MAX;
        float maxX = -FLT_MAX;
        float maxY = -FLT_MAX;
        float maxZ = -FLT_MAX;

        const int32_t maxToCheck = (std::min)(numBones, 384);
        std::vector<std::pair<std::string, FVector>> sampledBones;
        sampledBones.reserve(maxToCheck);

        for (int32_t i = 0; i < maxToCheck; ++i)
        {
            const FName boneName = TargetChar->Mesh->GetBoneName(i);
            const std::string boneNameStr = boneName.ToString();
            if (boneNameStr.empty())
                continue;

            const std::string boneNameLower = ToLowerAscii(boneNameStr);
            if (IsAuxiliaryBoneName(boneNameLower))
                continue;

            const FVector point = TargetChar->Mesh->GetBoneTransform(boneName, ERelativeTransformSpace::RTS_World).Translation;
            if (!IsFiniteVector(point))
                continue;

            if (fabsf(static_cast<float>(point.X - actorOrigin.X)) > maxDx ||
                fabsf(static_cast<float>(point.Y - actorOrigin.Y)) > maxDy ||
                fabsf(static_cast<float>(point.Z - actorOrigin.Z)) > maxDz)
            {
                continue;
            }

            sampledBones.push_back({ boneNameLower, point });
            minX = (std::min)(minX, static_cast<float>(point.X));
            minY = (std::min)(minY, static_cast<float>(point.Y));
            minZ = (std::min)(minZ, static_cast<float>(point.Z));
            maxX = (std::max)(maxX, static_cast<float>(point.X));
            maxY = (std::max)(maxY, static_cast<float>(point.Y));
            maxZ = (std::max)(maxZ, static_cast<float>(point.Z));
        }

        if (sampledBones.size() < 3)
            return false;

        OutOrigin = FVector((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f);
        OutExtent = FVector((maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f);
        if (OutExtent.X <= 1.0f || OutExtent.Y <= 1.0f || OutExtent.Z <= 1.0f)
            return false;

        if (OutSampledBones)
            *OutSampledBones = std::move(sampledBones);
        return true;
    }
}

bool Utils::GetReliableMeshBounds(ACharacter* TargetChar, FVector& OutOrigin, FVector& OutExtent)
{
    return BuildReliableMeshBounds(TargetChar, OutOrigin, OutExtent, nullptr);
}

FVector Utils::GetBestAimPoint(ACharacter* TargetChar, const std::string& PreferredBone)
{
    if (!TargetChar || !Utils::IsValidActor(TargetChar))
        return FVector{};

    const FVector actorLocation = TargetChar->K2_GetActorLocation();
    if (!TargetChar->Mesh)
        return actorLocation;

    FVector origin{};
    FVector extent{};
    std::vector<std::pair<std::string, FVector>> sampledBones;
    const bool hasReliableMeshBounds = BuildReliableMeshBounds(TargetChar, origin, extent, &sampledBones);
    if (!hasReliableMeshBounds)
        TargetChar->GetActorBounds(false, &origin, &extent, false);

    auto isPointInsideBounds = [&](const FVector& point, float xySlackScale, float zSlackScale)
    {
        const float maxDx = (std::max)(50.0f, static_cast<float>(extent.X) * xySlackScale);
        const float maxDy = (std::max)(50.0f, static_cast<float>(extent.Y) * xySlackScale);
        const float maxDz = (std::max)(50.0f, static_cast<float>(extent.Z) * zSlackScale);
        return fabsf(static_cast<float>(point.X - origin.X)) <= maxDx &&
               fabsf(static_cast<float>(point.Y - origin.Y)) <= maxDy &&
               fabsf(static_cast<float>(point.Z - origin.Z)) <= maxDz;
    };

    auto tryBone = [&](const char* boneName, FVector& outPoint, bool requireInteriorFit) -> bool
    {
        const std::string boneNameLower = ToLowerAscii(std::string(boneName));
        if (IsAuxiliaryBoneName(boneNameLower))
            return false;

        for (const auto& [sampleName, samplePoint] : sampledBones)
        {
            if (sampleName != boneNameLower)
                continue;
            if (requireInteriorFit && !isPointInsideBounds(samplePoint, 1.1f, 1.1f))
                return false;

            outPoint = samplePoint;
            return true;
        }

        const FName bone = UKismetStringLibrary::Conv_StringToName(UtfN::StringToWString(std::string(boneName)).c_str());
        if (TargetChar->Mesh->GetBoneIndex(bone) == -1)
            return false;

        const FVector point = TargetChar->Mesh->GetBoneTransform(bone, ERelativeTransformSpace::RTS_World).Translation;
        if (!IsFiniteVector(point))
            return false;
        if (requireInteriorFit && !isPointInsideBounds(point, 1.1f, 1.1f))
            return false;

        outPoint = point;
        return true;
    };

    FVector aimPoint{};
    if (!PreferredBone.empty() && tryBone(PreferredBone.c_str(), aimPoint, true))
        return aimPoint;

    static const std::array<const char*, 12> kCenterMassBones = {
        "Spine3",
        "Spine2",
        "Spine1",
        "Spine",
        "Chest",
        "UpperChest",
        "Neck",
        "Hips",
        "Pelvis",
        "Root",
        "Base",
        "Body"
    };

    for (const char* boneName : kCenterMassBones)
    {
        if (tryBone(boneName, aimPoint, true))
            return aimPoint;
    }

    if (!PreferredBone.empty() && tryBone(PreferredBone.c_str(), aimPoint, false))
        return aimPoint;

    for (const char* boneName : kCenterMassBones)
    {
        if (tryBone(boneName, aimPoint, false))
            return aimPoint;
    }

    if (hasReliableMeshBounds && extent.Z > 1.0f)
    {
        return FVector(origin.X, origin.Y, origin.Z + extent.Z * 0.1f);
    }

    if (extent.Z > 1.0f)
    {
        return FVector(origin.X, origin.Y, origin.Z + extent.Z * 0.15f);
    }

    return actorLocation;
}

FVector Utils::GetHighestBone(ACharacter* TargetChar)
{
    if (!TargetChar || !TargetChar->Mesh) return TargetChar->K2_GetActorLocation();

    int32_t NumBones = TargetChar->Mesh->GetNumBones();
    if (NumBones <= 0 || NumBones > 5000) return TargetChar->K2_GetActorLocation();

    float MaxZ = -1e10f;
    FVector BestPos = TargetChar->K2_GetActorLocation();
    
    // Safety cap to avoid overhead on extremely complex skeletons
    int32_t MaxToCheck = (NumBones > 256) ? 256 : NumBones;

    for (int i = 0; i < MaxToCheck; i++)
    {
        FName BoneName = TargetChar->Mesh->GetBoneName(i);
        FVector Pos = TargetChar->Mesh->GetBoneTransform(BoneName, ERelativeTransformSpace::RTS_World).Translation;
        if (Pos.Z > MaxZ)
        {
            MaxZ = (float)Pos.Z;
            BestPos = Pos;
        }
    }
    return BestPos;
}

void Utils::SendMouseLeftDown()
{
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(INPUT));
}

void Utils::SendMouseLeftUp()
{
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(INPUT));
}
