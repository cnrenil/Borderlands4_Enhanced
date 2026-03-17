#include "pch.h"

namespace GUI::Draw
{
    namespace
    {
        std::atomic<Backend> g_PreferredBackend{ Backend::Auto };

        ImVec2 GetViewportOffsetForImGui()
        {
            if (!GVars.PlayerController || !GVars.PlayerController->PlayerCameraManager)
                return ImVec2(0.0f, 0.0f);

            const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
            const FMinimalViewInfo& cameraPOV = GVars.PlayerController->PlayerCameraManager->CameraCachePrivate.POV;
            const FVector camLoc = cameraPOV.Location;
            const FVector camFwd = Utils::FRotatorToVector(cameraPOV.Rotation);
            const FVector aimPoint = camLoc + (camFwd * 50000.0f);

            FVector2D projectedCenter{};
            if (Utils::ProjectWorldLocationToScreen(aimPoint, projectedCenter, true))
            {
                return ImVec2(
                    (displaySize.x * 0.5f) - static_cast<float>(projectedCenter.X),
                    (displaySize.y * 0.5f) - static_cast<float>(projectedCenter.Y));
            }

            return ImVec2(0.0f, 0.0f);
        }

        ImVec2 ToImGuiDrawPos(const ImVec2& pos)
        {
            const ImVec2 offset = GetViewportOffsetForImGui();
            return ImVec2(pos.x + offset.x, pos.y + offset.y);
        }

        UFont* ResolveHudFont()
        {
            static UFont* cachedFont = nullptr;
            if (cachedFont)
                return cachedFont;

            if (UEngine* engine = UEngine::GetEngine())
            {
                if (engine->SmallFont)
                {
                    cachedFont = engine->SmallFont;
                    return cachedFont;
                }
                if (engine->TinyFont)
                {
                    cachedFont = engine->TinyFont;
                    return cachedFont;
                }
            }

            cachedFont = UObject::FindObject<UFont>("Font Engine.Default__Font");
            return cachedFont;
        }

        UCanvas* ResolveCanvas(UCanvas* canvas)
        {
            return canvas ? canvas : Utils::GetCurrentCanvas();
        }

        ImDrawList* GetDrawList()
        {
            return ImGui::GetBackgroundDrawList();
        }

        void DrawCanvasLineImpl(UCanvas* canvas, const FVector2D& a, const FVector2D& b, float thickness, const FLinearColor& color)
        {
            if (!canvas) return;
            canvas->K2_DrawLine(a, b, thickness, color);
        }

        void DrawCanvasBoxImpl(UCanvas* canvas, const FVector2D& position, const FVector2D& size, float thickness, const FLinearColor& color)
        {
            if (!canvas) return;
            canvas->K2_DrawBox(position, size, thickness, color);
        }

        void DrawCanvasFilledRectImpl(UCanvas* canvas, const FVector2D& position, const FVector2D& size, const FLinearColor& color)
        {
            if (!canvas || size.X <= 0.0f || size.Y <= 0.0f) return;

            UTexture2D* fillTexture = canvas->DefaultTexture ? canvas->DefaultTexture : nullptr;
            if (!fillTexture)
            {
                UEngine* engine = UEngine::GetEngine();
                fillTexture = engine ? engine->DefaultTexture : nullptr;
            }
            if (!fillTexture) return;

            canvas->K2_DrawTexture(
                fillTexture,
                position,
                size,
                FVector2D(0.0, 0.0),
                FVector2D(1.0, 1.0),
                color,
                EBlendMode::BLEND_Translucent,
                0.0f,
                FVector2D(0.0, 0.0));
        }

        void DrawCanvasCircleImpl(UCanvas* canvas, const FVector2D& center, float radius, int32 sides, float thickness, const FLinearColor& color)
        {
            if (!canvas || radius <= 0.0f || sides < 3) return;

            const float step = (2.0f * static_cast<float>(std::numbers::pi)) / static_cast<float>(sides);
            FVector2D prev(center.X + radius, center.Y);
            for (int32 i = 1; i <= sides; ++i)
            {
                const float angle = step * static_cast<float>(i);
                const FVector2D next(
                    center.X + std::cos(angle) * radius,
                    center.Y + std::sin(angle) * radius);
                DrawCanvasLineImpl(canvas, prev, next, thickness, color);
                prev = next;
            }
        }

        void DrawCanvasTextImpl(UCanvas* canvas, const FString& text, const FVector2D& position, const FLinearColor& color, const FVector2D& scale, bool centerX, bool centerY, bool outlined)
        {
            if (!canvas || !text) return;

            UFont* font = ResolveHudFont();
            if (!font)
            {
                UEngine* engine = UEngine::GetEngine();
                if (engine)
                {
                    if (engine->SmallFont) font = engine->SmallFont;
                    else if (engine->TinyFont) font = engine->TinyFont;
                }
            }

            canvas->K2_DrawText(
                font,
                text,
                position,
                scale,
                color,
                0.0f,
                FLinearColor(0.0f, 0.0f, 0.0f, color.A),
                FVector2D(1.0f, 1.0f),
                centerX,
                centerY,
                outlined,
                FLinearColor(0.0f, 0.0f, 0.0f, color.A));
        }

        void DrawCanvasTextImpl(UCanvas* canvas, const std::string& text, const FVector2D& position, const FLinearColor& color, const FVector2D& scale, bool centerX, bool centerY, bool outlined)
        {
            if (!canvas || text.empty()) return;
            std::wstring wideText = UtfN::StringToWString(text);
            const int32 textLen = static_cast<int32>(wideText.length() + 1);
            const FString renderText(const_cast<wchar_t*>(wideText.c_str()), textLen, textLen);
            DrawCanvasTextImpl(canvas, renderText, position, color, scale, centerX, centerY, outlined);
        }
    }

    void SetPreferredBackend(Backend backend)
    {
        g_PreferredBackend.store(backend);
    }

    Backend GetPreferredBackend()
    {
        return g_PreferredBackend.load();
    }

    Backend ResolveBackend(UCanvas* canvas)
    {
        (void)canvas;
        return Backend::ImGui;
    }

    void Line(const ImVec2& a, const ImVec2& b, ImU32 color, float thickness, UCanvas* canvas)
    {
        if (ResolveBackend(canvas) == Backend::UCanvas)
        {
            DrawCanvasLineImpl(ResolveCanvas(canvas), FVector2D(a.x, a.y), FVector2D(b.x, b.y), thickness, Utils::U32ToLinearColor(color));
            return;
        }
        GetDrawList()->AddLine(ToImGuiDrawPos(a), ToImGuiDrawPos(b), color, thickness);
    }

    void Rect(const ImVec2& min, const ImVec2& max, ImU32 color, float thickness, UCanvas* canvas)
    {
        if (ResolveBackend(canvas) == Backend::UCanvas)
        {
            DrawCanvasBoxImpl(ResolveCanvas(canvas), FVector2D(min.x, min.y), FVector2D(max.x - min.x, max.y - min.y), thickness, Utils::U32ToLinearColor(color));
            return;
        }
        GetDrawList()->AddRect(ToImGuiDrawPos(min), ToImGuiDrawPos(max), color, 0.0f, 0, thickness);
    }

    void RectFilled(const ImVec2& min, const ImVec2& max, ImU32 color, UCanvas* canvas)
    {
        if (ResolveBackend(canvas) == Backend::UCanvas)
        {
            DrawCanvasFilledRectImpl(ResolveCanvas(canvas), FVector2D(min.x, min.y), FVector2D(max.x - min.x, max.y - min.y), Utils::U32ToLinearColor(color));
            return;
        }
        GetDrawList()->AddRectFilled(ToImGuiDrawPos(min), ToImGuiDrawPos(max), color);
    }

    void Circle(const ImVec2& center, float radius, ImU32 color, int sides, float thickness, UCanvas* canvas)
    {
        if (ResolveBackend(canvas) == Backend::UCanvas)
        {
            DrawCanvasCircleImpl(ResolveCanvas(canvas), FVector2D(center.x, center.y), radius, sides, thickness, Utils::U32ToLinearColor(color));
            return;
        }
        GetDrawList()->AddCircle(ToImGuiDrawPos(center), radius, color, sides, thickness);
    }

    void CircleFilled(const ImVec2& center, float radius, ImU32 color, UCanvas* canvas)
    {
        if (ResolveBackend(canvas) == Backend::UCanvas)
        {
            DrawCanvasCircleImpl(ResolveCanvas(canvas), FVector2D(center.x, center.y), radius, 16, radius, Utils::U32ToLinearColor(color));
            return;
        }
        GetDrawList()->AddCircleFilled(ToImGuiDrawPos(center), radius, color);
    }

    void TriangleOutline(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 color, float thickness, UCanvas* canvas)
    {
        Line(p1, p2, color, thickness, canvas);
        Line(p2, p3, color, thickness, canvas);
        Line(p3, p1, color, thickness, canvas);
    }

    void TriangleFilled(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, ImU32 color, UCanvas* canvas)
    {
        if (ResolveBackend(canvas) == Backend::UCanvas)
        {
            TriangleOutline(p1, p2, p3, color, 2.0f, canvas);
            return;
        }
        GetDrawList()->AddTriangleFilled(ToImGuiDrawPos(p1), ToImGuiDrawPos(p2), ToImGuiDrawPos(p3), color);
    }

    void Text(const std::string& text, const ImVec2& pos, ImU32 color, const FVector2D& scale, bool centerX, bool centerY, bool outlined, UCanvas* canvas)
    {
        if (text.empty())
            return;

        if (ResolveBackend(canvas) == Backend::UCanvas)
        {
            DrawCanvasTextImpl(ResolveCanvas(canvas), text, FVector2D(pos.x, pos.y), Utils::U32ToLinearColor(color), scale, centerX, centerY, outlined);
            return;
        }
        GetDrawList()->AddText(ToImGuiDrawPos(pos), color, text.c_str());
    }

    void Text(const FString& text, const ImVec2& pos, ImU32 color, const FVector2D& scale, bool centerX, bool centerY, bool outlined, UCanvas* canvas)
    {
        if (!text)
            return;

        if (ResolveBackend(canvas) == Backend::UCanvas)
        {
            DrawCanvasTextImpl(ResolveCanvas(canvas), text, FVector2D(pos.x, pos.y), Utils::U32ToLinearColor(color), scale, centerX, centerY, outlined);
            return;
        }
        GetDrawList()->AddText(ToImGuiDrawPos(pos), color, text.ToString().c_str());
    }
}
