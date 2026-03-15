#include "pch.h"


namespace
{
    thread_local UCanvas* g_CurrentCanvas = nullptr;

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
            if (GVars.PlayerController->ProjectWorldLocationToScreen(aimPoint, &aimScreen, false)) {
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

void Utils::DrawCanvasLine(UCanvas* Canvas, const FVector2D& A, const FVector2D& B, float Thickness, const FLinearColor& Color)
{
    if (!Canvas) return;
    Canvas->K2_DrawLine(A, B, Thickness, Color);
}

void Utils::DrawCanvasBox(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, float Thickness, const FLinearColor& Color)
{
    if (!Canvas) return;
    Canvas->K2_DrawBox(Position, Size, Thickness, Color);
}

void Utils::DrawCanvasFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
{
    if (!Canvas || Size.X <= 0.0 || Size.Y <= 0.0) return;

    UTexture2D* FillTexture = Canvas->DefaultTexture ? Canvas->DefaultTexture : nullptr;
    if (!FillTexture)
    {
        UEngine* Engine = UEngine::GetEngine();
        FillTexture = Engine ? Engine->DefaultTexture : nullptr;
    }
    if (!FillTexture) return;

    Canvas->K2_DrawTexture(
        FillTexture,
        Position,
        Size,
        FVector2D(0.0, 0.0),
        FVector2D(1.0, 1.0),
        Color,
        EBlendMode::BLEND_Translucent,
        0.0f,
        FVector2D(0.0, 0.0));
}

void Utils::DrawCanvasCircle(UCanvas* Canvas, const FVector2D& Center, float Radius, int32 Sides, float Thickness, const FLinearColor& Color)
{
    if (!Canvas || Radius <= 0.0f || Sides < 3) return;

    const float step = (2.0f * static_cast<float>(std::numbers::pi)) / static_cast<float>(Sides);
    FVector2D prev(Center.X + Radius, Center.Y);
    for (int32 i = 1; i <= Sides; ++i)
    {
        const float angle = step * static_cast<float>(i);
        const FVector2D next(
            Center.X + std::cos(angle) * Radius,
            Center.Y + std::sin(angle) * Radius);
        DrawCanvasLine(Canvas, prev, next, Thickness, Color);
        prev = next;
    }
}

void Utils::DrawCanvasText(UCanvas* Canvas, const std::string& Text, const FVector2D& Position, const FLinearColor& Color, const FVector2D& Scale, bool bCenterX, bool bCenterY, bool bOutlined)
{
    if (!Canvas || Text.empty()) return;

    UEngine* Engine = UEngine::GetEngine();
    UFont* Font = Engine ? (Engine->SmallFont ? Engine->SmallFont : Engine->TinyFont) : nullptr;
    const FString RenderText(UtfN::StringToWString(Text).c_str());
    Canvas->K2_DrawText(
        Font,
        RenderText,
        Position,
        Scale,
        Color,
        0.0f,
        FLinearColor(0.0f, 0.0f, 0.0f, Color.A),
        FVector2D(1.0f, 1.0f),
        bCenterX,
        bCenterY,
        bOutlined,
        FLinearColor(0.0f, 0.0f, 0.0f, Color.A));
}

ALightProjectileManager* Utils::GetLightProjManager()
{
    static ALightProjectileManager* CachedManager = nullptr;
    if (CachedManager && !IsBadReadPtr(CachedManager, sizeof(void*)) && CachedManager->VTable)
        return CachedManager;
        
    if (!GVars.Level) return nullptr;
    
    int32_t NumActors = GVars.Level->Actors.Num();
    if (NumActors < 0 || NumActors > 200000) return nullptr;

    for (int i = 0; i < NumActors; i++)
    {
        AActor* Actor = GVars.Level->Actors[i];
        if (Actor && !IsBadReadPtr(Actor, sizeof(void*)) && Actor->VTable && Actor->IsA(ALightProjectileManager::StaticClass()))
        {
            CachedManager = reinterpret_cast<ALightProjectileManager*>(Actor);
            return CachedManager;
        }
    }
    return nullptr;
}

void Utils::PrintActors(const char* Exclude)
{
    if (!GVars.World || !GVars.World->VTable) return;
	ULevel* Level = GVars.Level;
	if (Level)
	{
		if (Level->Actors.Num() > 0 && Level->Actors.Num() < 100000)
		{
			for (int i = 0; i < Level->Actors.Num(); i++)
			{
				AActor* Actor = Level->Actors[i];
				if (Actor)
				{
					if (!Utils::IsValidActor(Actor)) continue;
					if (Exclude && Actor->GetName().find(Exclude) != std::string::npos)
						continue;

					LOG_INFO("Scanner", "Actor %d: %s - Class: %s", i, Actor->GetName().c_str(), Actor->Class->Name.ToString().c_str());
				}
			}
		}
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

AActor* Utils::GetBestTarget(APlayerController* ViewPoint, float MaxFOV, bool RequiresLOS, std::string TargetBone, bool TargetAll)
{
    if (Utils::bIsLoading || !GVars.World || !GVars.World->VTable) return nullptr;
    if (!GVars.Level || !GVars.GameState || !ViewPoint || !ViewPoint->PlayerCameraManager) return nullptr;

    static std::string CachedBoneString = "";
    static FName CachedBoneName;
    if (CachedBoneString != TargetBone) {
        std::wstring WideString = UtfN::StringToWString(TargetBone);
        CachedBoneName = UKismetStringLibrary::Conv_StringToName(WideString.c_str());
        CachedBoneString = TargetBone;
    }

    AActor* BestTarget = nullptr;
    float BestFOV = MaxFOV;

    FVector CameraLocation = ViewPoint->PlayerCameraManager->CameraCachePrivate.POV.Location;
    FVector2D ViewportSize = Utils::ImVec2ToFVector2D(GVars.ScreenSize);
    FVector2D ViewportCenter = Utils::ImVec2ToFVector2D(GetAimScreenCenter());
    float MaxFOVNormalized = MaxFOV / 90.0f;

    AActor* SelfActor = Utils::GetSelfActor();

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
        FVector ActorLoc = TargetChar->K2_GetActorLocation();
        if (CameraLocation.GetDistanceTo(ActorLoc) > 15000.0f) // 150 meters
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
        FVector BoneLocation = TargetChar->Mesh->GetBoneTransform(CachedBoneName, ERelativeTransformSpace::RTS_World).Translation;

        // LOS Check is the most expensive, do it last
        if (RequiresLOS)
        {
            // Internal sight check using camera location as origin
            bool bHasLOS = GVars.PlayerController->LineOfSightTo(TargetChar, CameraLocation, true);
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

            if (NormalizedOffset < MaxFOVNormalized && FOV < BestFOV)
            {
                BestFOV = FOV;
                BestTarget = TargetChar;
            }
        }
    }
    return BestTarget;
}

void Utils::DrawFOV(float MaxFOV, float Thickness = 1.0f)
{
    FVector2D ViewportSize = Utils::ImVec2ToFVector2D(GVars.ScreenSize);
    FVector2D Center = Utils::ImVec2ToFVector2D(GetAimScreenCenter());

    float MaxFOVNormalized = MaxFOV / 90.0f;
    float RadiusPixels = MaxFOVNormalized * ((float)ViewportSize.Y * 0.5f);

    if (UCanvas* Canvas = GetCurrentCanvas())
    {
        DrawCanvasCircle(Canvas, Center, RadiusPixels, 64, Thickness, FLinearColor(1.0f, 0.0f, 0.0f, 1.0f));
        return;
    }

    ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(Center.X, Center.Y), RadiusPixels, IM_COL32(255, 0, 0, 255), 64, Thickness);
}

void Utils::DrawSnapLine(FVector TargetPos, float Thickness = 2.0f)
{
    FVector2D ScreenPos;

    if (!GVars.PlayerController->ProjectWorldLocationToScreen(TargetPos, &ScreenPos, false))
        return;

    ImVec2 Center = GetAimScreenCenter();
    ImVec2 Target((float)ScreenPos.X, (float)ScreenPos.Y);

    if (UCanvas* Canvas = GetCurrentCanvas())
    {
        const FVector2D centerVec(Center.x, Center.y);
        const FVector2D targetVec(Target.x, Target.y);
        DrawCanvasLine(Canvas, centerVec, targetVec, Thickness, FLinearColor(1.0f, 1.0f, 1.0f, 0.7f));

        const float angle = atan2f(Target.y - Center.y, Target.x - Center.x);
        const float arrowSize = 10.0f;
        const FVector2D p1(
            Target.x - arrowSize * cosf(angle - 0.5f),
            Target.y - arrowSize * sinf(angle - 0.5f));
        const FVector2D p2(
            Target.x - arrowSize * cosf(angle + 0.5f),
            Target.y - arrowSize * sinf(angle + 0.5f));
        DrawCanvasLine(Canvas, targetVec, p1, Thickness, FLinearColor(1.0f, 1.0f, 1.0f, 0.85f));
        DrawCanvasLine(Canvas, targetVec, p2, Thickness, FLinearColor(1.0f, 1.0f, 1.0f, 0.85f));
        DrawCanvasCircle(Canvas, targetVec, 2.5f, 12, 1.5f, FLinearColor(0.0f, 1.0f, 0.0f, 1.0f));
        return;
    }
    
    ImGui::GetBackgroundDrawList()->AddLine(Center, Target, IM_COL32(255, 255, 255, 180), Thickness);
    
    // Draw an arrow head
    float Angle = atan2f(Target.y - Center.y, Target.x - Center.x);
    float ArrowSize = 10.0f;
    ImVec2 P1(Target.x - ArrowSize * cosf(Angle - 0.5f), Target.y - ArrowSize * sinf(Angle - 0.5f));
    ImVec2 P2(Target.x - ArrowSize * cosf(Angle + 0.5f), Target.y - ArrowSize * sinf(Angle + 0.5f));
    
    ImGui::GetBackgroundDrawList()->AddTriangleFilled(Target, P1, P2, IM_COL32(255, 255, 255, 220));
    ImGui::GetBackgroundDrawList()->AddCircleFilled(Target, 2.5f, IM_COL32(0, 255, 0, 255));

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
