#include "pch.h"

namespace
{
    std::vector<ImVec2> BuildRoundedRectPath(const ImVec2& min, const ImVec2& max, float rounding)
    {
        std::vector<ImVec2> points;
        const float width = max.x - min.x;
        const float height = max.y - min.y;
        if (width <= 0.0f || height <= 0.0f)
            return points;

        const float radius = (std::max)(0.0f, (std::min)(rounding, ((std::min)(width, height) * 0.5f) - 1.0f));
        if (radius <= 0.5f)
        {
            points.push_back(min);
            points.push_back(ImVec2(max.x, min.y));
            points.push_back(max);
            points.push_back(ImVec2(min.x, max.y));
            return points;
        }

        auto appendArc = [&](const ImVec2& center, float startAngle, float endAngle)
        {
            const int segments = 10;
            for (int i = 0; i <= segments; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(segments);
                const float angle = startAngle + ((endAngle - startAngle) * t);
                points.emplace_back(
                    center.x + (std::cos(angle) * radius),
                    center.y + (std::sin(angle) * radius));
            }
        };

        appendArc(ImVec2(max.x - radius, min.y + radius), -IM_PI * 0.5f, 0.0f);
        appendArc(ImVec2(max.x - radius, max.y - radius), 0.0f, IM_PI * 0.5f);
        appendArc(ImVec2(min.x + radius, max.y - radius), IM_PI * 0.5f, IM_PI);
        appendArc(ImVec2(min.x + radius, min.y + radius), IM_PI, IM_PI * 1.5f);
        return points;
    }

    float ComputePolylineLength(const std::vector<ImVec2>& points, bool closed)
    {
        if (points.size() < 2)
            return 0.0f;

        float length = 0.0f;
        for (size_t i = 1; i < points.size(); ++i)
        {
            const float dx = points[i].x - points[i - 1].x;
            const float dy = points[i].y - points[i - 1].y;
            const float edgeLengthSqr = (dx * dx) + (dy * dy);
            length += edgeLengthSqr > 0.0f ? std::sqrt(edgeLengthSqr) : 0.0f;
        }
        if (closed)
        {
            const float dx = points.front().x - points.back().x;
            const float dy = points.front().y - points.back().y;
            const float edgeLengthSqr = (dx * dx) + (dy * dy);
            length += edgeLengthSqr > 0.0f ? std::sqrt(edgeLengthSqr) : 0.0f;
        }
        return length;
    }

    std::vector<ImVec2> ExtractPolylineSegment(const std::vector<ImVec2>& points, float startDistance, float segmentLength)
    {
        std::vector<ImVec2> out;
        if (points.size() < 2 || segmentLength <= 0.0f)
            return out;

        const float totalLength = ComputePolylineLength(points, true);
        if (totalLength <= 1.0f)
            return out;

        auto pointAtDistance = [&](float distance) -> ImVec2
        {
            float remaining = std::fmod(distance, totalLength);
            if (remaining < 0.0f)
                remaining += totalLength;

            for (size_t i = 0; i < points.size(); ++i)
            {
                const ImVec2 a = points[i];
                const ImVec2 b = points[(i + 1) % points.size()];
                const float deltaX = b.x - a.x;
                const float deltaY = b.y - a.y;
                const float edgeLength = std::sqrt((deltaX * deltaX) + (deltaY * deltaY));
                if (edgeLength <= 0.0001f)
                    continue;
                if (remaining <= edgeLength)
                {
                    const float t = remaining / edgeLength;
                    return ImVec2(a.x + (deltaX * t), a.y + (deltaY * t));
                }
                remaining -= edgeLength;
            }

            return points.front();
        };

        const int subdivisions = (std::max)(12, static_cast<int>(segmentLength / 10.0f));
        for (int i = 0; i <= subdivisions; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(subdivisions);
            out.push_back(pointAtDistance(startDistance + (segmentLength * t)));
        }
        return out;
    }

    void DrawOverlayHudChrome(const ImVec2& pos, const ImVec2& size, ImU32 accent)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float rounding = 20.0f;
        const ImVec2 max(pos.x + size.x, pos.y + size.y);

        drawList->AddRectFilled(
            ImVec2(pos.x + 10.0f, pos.y + 14.0f),
            ImVec2(max.x + 10.0f, max.y + 14.0f),
            IM_COL32(0, 0, 0, 24),
            rounding + 4.0f);

        drawList->AddRectFilledMultiColor(
            pos,
            max,
            IM_COL32(255, 255, 255, 16),
            IM_COL32(255, 255, 255, 10),
            IM_COL32(255, 255, 255, 6),
            IM_COL32(255, 255, 255, 12));

        drawList->AddRect(pos, max, IM_COL32(255, 255, 255, 24), rounding, 0, 1.0f);
        drawList->AddRect(
            ImVec2(pos.x + 1.0f, pos.y + 1.0f),
            ImVec2(max.x - 1.0f, max.y - 1.0f),
            IM_COL32(255, 255, 255, 10),
            rounding - 1.0f,
            0,
            1.0f);

        drawList->AddLine(
            ImVec2(pos.x + 18.0f, pos.y + 48.0f),
            ImVec2(pos.x + size.x - 18.0f, pos.y + 48.0f),
            IM_COL32(255, 255, 255, 14),
            1.0f);

        const ImVec2 marqueeMin(pos.x + 12.0f, pos.y + 12.0f);
        const ImVec2 marqueeMax(max.x - 12.0f, max.y - 12.0f);
        const std::vector<ImVec2> marqueePath = BuildRoundedRectPath(marqueeMin, marqueeMax, rounding - 8.0f);
        const float perimeter = ComputePolylineLength(marqueePath, true);
        if (perimeter <= 1.0f)
            return;

        const float segmentLength = (std::min)(perimeter * 0.22f, 120.0f);
        const float travel = std::fmod(static_cast<float>(ImGui::GetTime()) * 120.0f, perimeter);
        const std::vector<ImVec2> marqueeSegment = ExtractPolylineSegment(marqueePath, travel, segmentLength);
        const ImU32 marqueeColor = IM_COL32(
            (accent >> IM_COL32_R_SHIFT) & 0xFF,
            (accent >> IM_COL32_G_SHIFT) & 0xFF,
            (accent >> IM_COL32_B_SHIFT) & 0xFF,
            188);
        if (marqueeSegment.size() >= 2)
            drawList->AddPolyline(marqueeSegment.data(), static_cast<int>(marqueeSegment.size()), marqueeColor, 0, 2.0f);
    }
}

void Cheats::RenderEnabledOptions()
{
	if (!ConfigManager::B("Misc.RenderOptions")) return;

    std::vector<const char*> activeKeys;
    std::vector<const char*> activeLabels;
    auto addEntry = [&](bool enabled, const char* key)
    {
        if (enabled)
            activeKeys.push_back(key);
    };

    addEntry(ConfigManager::B("Player.GodMode"), "GODMODE");
    addEntry(ConfigManager::B("Player.InfAmmo"), "INF_AMMO");
    addEntry(ConfigManager::B("Player.InfGrenades"), "INF_GRENADES");
    addEntry(ConfigManager::B("Player.InfVehicleBoost"), "INF_VEHICLE_BOOST");
    addEntry(ConfigManager::B("Player.InfGlideStamina"), "INF_GLIDE_STAMINA");
    addEntry(ConfigManager::B("Aimbot.Enabled"), "AIMBOT");
    addEntry(ConfigManager::B("Player.ESP"), "ESP");

    if (activeKeys.empty())
        return;

    activeLabels.reserve(activeKeys.size());
    float maxLabelWidth = 0.0f;
    for (const char* key : activeKeys)
    {
        const char* label = Localization::T(key);
        activeLabels.push_back(label);
        maxLabelWidth = (std::max)(maxLabelWidth, ImGui::CalcTextSize(label).x);
    }

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float titleWidth = ImGui::CalcTextSize(Localization::T("ACTIVE_FEATURES")).x;
    const float horizontalPadding = 14.0f;
    const float maxAllowedWidth = (std::max)(210.0f, displaySize.x - 32.0f);
    const float desiredWidth = (std::max)(titleWidth, maxLabelWidth) + (horizontalPadding * 2.0f) + 8.0f;
    const float windowWidth = (std::clamp)(desiredWidth, 210.0f, maxAllowedWidth);
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 windowSize(windowWidth, 56.0f + (static_cast<float>(activeLabels.size()) * rowHeight));
    const float windowX = (std::max)(16.0f, displaySize.x - windowSize.x - 16.0f);
    ImGui::SetNextWindowPos(ImVec2(windowX, 30.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::Begin(
        Localization::T("ACTIVE_FEATURES_LIST"),
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove);

    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    DrawOverlayHudChrome(pos, size, IM_COL32(102, 214, 213, 220));

    ImGui::SetCursorPos(ImVec2(16.0f, 12.0f));
    ImGui::TextColored(ImVec4(0.86f, 0.92f, 0.95f, 0.94f), "%s", Localization::T("ACTIVE_FEATURES"));
    ImGui::SetCursorPos(ImVec2(16.0f, 38.0f));

    for (const char* label : activeLabels)
    {
        ImGui::SetCursorPosX(16.0f);
        ImGui::TextUnformatted(label);
    }
	ImGui::End();
}

void Cheats::KillEnemies() {
    if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
        static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Kill);
}

void Cheats::GiveLevels() {
    if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
        static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Levels);
}

void Cheats::SpawnItems() {
    if (GVars.PlayerController && GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass()))
        static_cast<SDK::AOakPlayerController*>(GVars.PlayerController)->ServerActivateDevPerk(SDK::EDevPerk::Items);
}

void Cheats::TogglePlayersOnly() {
    SDK::UWorld* World = Utils::GetWorldSafe();
    if (World && World->GameState && World->GameState->IsA(SDK::AGbxGameState::StaticClass())) {
        static_cast<SDK::AGbxGameState*>(World->GameState)->bRepPlayersOnly = ConfigManager::B("Player.PlayersOnly");
    }
}

void Cheats::AddCurrency(const std::string& type, int amount)
{
    if (!GVars.PlayerController || !GVars.PlayerController->IsA(SDK::AOakPlayerController::StaticClass())) return;
    SDK::AOakPlayerController* OakPC = static_cast<SDK::AOakPlayerController*>(GVars.PlayerController);
    if (!OakPC->CurrencyManager) return;

    for (int i = 0; i < OakPC->CurrencyManager->currencies.Num(); i++) {
        auto& cur = OakPC->CurrencyManager->currencies[i];
        if (cur.type.Name.ToString().find(type) != std::string::npos) {
            OakPC->Server_AddCurrency(cur.type, amount);
            break;
        }
    }
}

void Cheats::ClearGroundItems()
{
    if (!GVars.World || !GVars.Level) return;
    SDK::FVector farLoc = { 100000.f, 100000.f, -100000.f };

    Utils::ForEachLevelActor(GVars.Level, [&](SDK::AActor* Actor)
    {
        if (Actor && Actor->IsA(SDK::AInventoryPickup::StaticClass()))
            Actor->K2_SetActorLocation(farLoc, false, nullptr, false);
        return true;
    });
}

void Cheats::SetExperienceLevel(int32 xpAmount)
{
    SDK::UWorld* World = Utils::GetWorldSafe();
    if (!World || !World->OwningGameInstance || World->OwningGameInstance->LocalPlayers.Num() == 0) return;

    SDK::APlayerController* PC = World->OwningGameInstance->LocalPlayers[0]->PlayerController;
    if (!PC || !PC->IsA(SDK::AOakPlayerController::StaticClass())) return;

    SDK::AOakPlayerController* OakController = static_cast<SDK::AOakPlayerController*>(PC);
    SDK::AOakPlayerState* PS = OakController->GetOakPlayerState();

    if (PS)
    {
        if (PS->ExperienceState.Num() > 0)
        {
            PS->ExperienceState[0].ExperienceLevel = xpAmount;
        }
    }

    SDK::AOakCharacter* localChar = static_cast<SDK::AOakCharacter*>(OakController->Character);
    if (localChar && localChar->IsA(SDK::AOakCharacter::StaticClass()))
    {
        localChar->BroadcastLevelUp(xpAmount);
    }
}
