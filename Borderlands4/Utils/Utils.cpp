#include "pch.h"

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

unsigned Utils::ConvertImVec4toU32(ImVec4 Color)
{
    return IM_COL32((int)(Color.x * 255.0f), (int)(Color.y * 255.0f), (int)(Color.z * 255.0f), (int)(Color.w * 255.0f));
}

ALightProjectileManager* Utils::GetLightProjManager()
{
    static ALightProjectileManager* CachedManager = nullptr;
    if (CachedManager && !IsBadReadPtr(CachedManager, sizeof(void*)) && CachedManager->VTable)
        return CachedManager;
        
    if (!GVars.Level) return nullptr;
    
    for (int i = 0; i < GVars.Level->Actors.Num(); i++)
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

					printf("Actor %d: %s - Class: %s\n", i, Actor->GetName().c_str(), Actor->Class->Name.ToString().c_str());
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
    if (!Actor || IsBadReadPtr(Actor, sizeof(void*)) || !Actor->VTable) return false;

    // Use Unreal's built-in validation where possible
    if (!UKismetSystemLibrary::IsValid(Actor)) return false;

    if (!Actor->Class || !Actor->GetLevel()) return false;

    if (Actor->IsActorBeingDestroyed() || Actor->bActorIsBeingDestroyed) return false;

    // Additional check for RootComponent which is often accessed
    if (!Actor->RootComponent) return false;

    return true;
}


float Utils::GetFOVFromScreenCoords(const ImVec2& ScreenLocation)
{
    ImVec2 ScreenCenter(GVars.ScreenSize.x / 2.0f, GVars.ScreenSize.y / 2.0f);

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
    FVector2D ViewportCenter = ViewportSize / 2.0;
    float MaxFOVNormalized = MaxFOV / 90.0f;

    // Use Cache!
    for (ACharacter* TargetChar : GVars.UnitCache)
    {
        if (!TargetChar || !Utils::IsValidActor(TargetChar))
            continue;

        if (TargetChar == GVars.Character)
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
            // Internal sight check Ex using LineOfSightTo (very fast)
            bool bHasLOS = GVars.PlayerController->LineOfSightTo(TargetChar, FVector(), true);
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
    FVector2D Center = ViewportSize * 0.5f;

    float MaxFOVNormalized = MaxFOV / 90.0f;
    float RadiusPixels = MaxFOVNormalized * ((float)ViewportSize.Y * 0.5f);

    // Draw using ImGui (pixel radius)
    ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(Center.X, Center.Y), RadiusPixels, IM_COL32(255, 0, 0, 255), 64, Thickness);
}

void Utils::DrawSnapLine(FVector TargetPos, float Thickness = 2.0f)
{
    FVector2D ScreenPos;

    if (!GVars.PlayerController->ProjectWorldLocationToScreen(TargetPos, &ScreenPos, true))
        return;

    ImVec2 Center(GVars.ScreenSize.x / 2.0f, GVars.ScreenSize.y / 2.0f);
    ImVec2 Target((float)ScreenPos.X, (float)ScreenPos.Y);
    
    // Draw the main line
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
    if (!GVars.World || !GVars.World->VTable) return true;
    
    if (!GVars.Level) return true;

    if (!GVars.GameState) return true;

    if (GVars.Level->Actors.Num() < 1) return true; 

    return false;
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
    if (!GVars.Level || !GVars.Character) return nullptr;
    
    ACharacter* NearestCharacter = nullptr;
    float NearestDistance = FLT_MAX;

    for (ACharacter* TargetChar : GVars.UnitCache)
    {
        if (!TargetChar || !Utils::IsValidActor(TargetChar))
            continue;

        if (TargetChar == GVars.Character)
            continue;

        FVector PlayerLocation = GVars.Character->K2_GetActorLocation();
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
	if (!GVars.Character || !Target) return ETeamAttitude::Neutral;
	return UGbxTeamFunctionLibrary::GetAttitudeTowards(GVars.Character, Target);
}
 
float Utils::GetHealthPercent(AActor* Actor)
{
	if (!Actor) return 0.0f;
	return UDamageStatics::GetHealthPoolPercent(Actor, 0); // layer 0 usually is health
}
