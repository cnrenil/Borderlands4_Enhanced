#pragma once

namespace GUI::Draw
{
    enum class Backend
    {
        Auto,
        UCanvas,
        ImGui
    };

    void SetPreferredBackend(Backend backend);
    Backend GetPreferredBackend();
    Backend ResolveBackend(class UCanvas* canvas = nullptr);

    void Line(const ImVec2& a, const ImVec2& b, ImU32 color, float thickness, class UCanvas* canvas = nullptr);
    void Rect(const ImVec2& min, const ImVec2& max, ImU32 color, float thickness, class UCanvas* canvas = nullptr);
    void RectFilled(const ImVec2& min, const ImVec2& max, ImU32 color, class UCanvas* canvas = nullptr);
    void Circle(const ImVec2& center, float radius, ImU32 color, int sides, float thickness, class UCanvas* canvas = nullptr);
    void CircleFilled(const ImVec2& center, float radius, ImU32 color, class UCanvas* canvas = nullptr);
    void TriangleOutline(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 color, float thickness, class UCanvas* canvas = nullptr);
    void TriangleFilled(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 color, class UCanvas* canvas = nullptr);
    void Text(const std::string& text, const ImVec2& pos, ImU32 color, const FVector2D& scale = FVector2D(1.0f, 1.0f), bool centerX = false, bool centerY = false, bool outlined = true, class UCanvas* canvas = nullptr);
    void Text(const class FString& text, const ImVec2& pos, ImU32 color, const FVector2D& scale = FVector2D(1.0f, 1.0f), bool centerX = false, bool centerY = false, bool outlined = true, class UCanvas* canvas = nullptr);
}
