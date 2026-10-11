#include "icons.hpp"
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace vgmstudio {
namespace icons {

void DrawPlay(ImDrawList* dl, ImVec2 pos, float size, ImU32 col) {
    float half = size * 0.5f;
    ImVec2 p1 = ImVec2(pos.x - half * 0.7f, pos.y - half);
    ImVec2 p2 = ImVec2(pos.x + half * 0.9f, pos.y);
    ImVec2 p3 = ImVec2(pos.x - half * 0.7f, pos.y + half);
    dl->AddTriangleFilled(p1, p2, p3, col);
}

void DrawPause(ImDrawList* dl, ImVec2 pos, float size, ImU32 col) {
    float barW = size * 0.28f;
    float barH = size;
    float gap = size * 0.22f;

    float x0 = pos.x - gap * 0.5f - barW;
    float x1 = pos.x + gap * 0.5f;
    float y0 = pos.y - barH * 0.5f;
    float y1 = pos.y + barH * 0.5f;

    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + barW, y1), col, 1.5f);
    dl->AddRectFilled(ImVec2(x1, y0), ImVec2(x1 + barW, y1), col, 1.5f);
}

void DrawStop(ImDrawList* dl, ImVec2 pos, float size, ImU32 col) {
    float half = size * 0.44f;
    dl->AddRectFilled(ImVec2(pos.x - half, pos.y - half), ImVec2(pos.x + half, pos.y + half), col, 2.0f);
}

void DrawRewind(ImDrawList* dl, ImVec2 pos, float size, ImU32 col) {
    float half = size * 0.5f;
    float barW = size * 0.18f;

    // Vertical bar on left
    dl->AddRectFilled(ImVec2(pos.x - half, pos.y - half), ImVec2(pos.x - half + barW, pos.y + half), col, 1.0f);

    // Left-pointing triangle
    ImVec2 p1 = ImVec2(pos.x + half, pos.y - half);
    ImVec2 p2 = ImVec2(pos.x - half + barW * 1.5f, pos.y);
    ImVec2 p3 = ImVec2(pos.x + half, pos.y + half);
    dl->AddTriangleFilled(p1, p2, p3, col);
}

void DrawLoop(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, bool active) {
    float radius = size * 0.42f;
    float thickness = 1.8f;

    // Two circular arcs
    dl->PathArcTo(pos, radius, -2.4f, 0.4f, 12);
    dl->PathStroke(col, false, thickness);

    dl->PathArcTo(pos, radius, 0.74f, 3.54f, 12);
    dl->PathStroke(col, false, thickness);

    // Arrow heads
    float a1 = 0.4f;
    ImVec2 head1 = ImVec2(pos.x + std::cos(a1) * radius, pos.y + std::sin(a1) * radius);
    dl->AddTriangleFilled(
        head1,
        ImVec2(head1.x - 3.5f, head1.y - 4.5f),
        ImVec2(head1.x + 3.0f, head1.y - 3.0f),
        col
    );

    float a2 = 3.54f;
    ImVec2 head2 = ImVec2(pos.x + std::cos(a2) * radius, pos.y + std::sin(a2) * radius);
    dl->AddTriangleFilled(
        head2,
        ImVec2(head2.x + 3.5f, head2.y + 4.5f),
        ImVec2(head2.x - 3.0f, head2.y + 3.0f),
        col
    );
}

void DrawVolume(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, float vol) {
    float half = size * 0.5f;

    // Speaker body
    float spkW = size * 0.35f;
    float spkH = size * 0.45f;
    float spkBackX = pos.x - half * 0.6f;
    float spkFrontX = spkBackX + spkW;
    float spkMidY = pos.y;

    // Small back rect
    dl->AddRectFilled(ImVec2(spkBackX, spkMidY - spkH * 0.35f), ImVec2(spkBackX + spkW * 0.45f, spkMidY + spkH * 0.35f), col, 1.0f);

    // Cone polygon
    ImVec2 conePts[4] = {
        ImVec2(spkBackX + spkW * 0.4f, spkMidY - spkH * 0.35f),
        ImVec2(spkFrontX, spkMidY - half * 0.85f),
        ImVec2(spkFrontX, spkMidY + half * 0.85f),
        ImVec2(spkBackX + spkW * 0.4f, spkMidY + spkH * 0.35f)
    };
    dl->AddConvexPolyFilled(conePts, 4, col);

    if (vol <= 0.001f) {
        // Mute 'X'
        float crossX = spkFrontX + 5.0f;
        float crossHalf = 3.0f;
        dl->AddLine(ImVec2(crossX - crossHalf, spkMidY - crossHalf), ImVec2(crossX + crossHalf, spkMidY + crossHalf), col, 1.5f);
        dl->AddLine(ImVec2(crossX - crossHalf, spkMidY + crossHalf), ImVec2(crossX + crossHalf, spkMidY - crossHalf), col, 1.5f);
    } else {
        // Wave arc 1
        float waveCenter = spkFrontX - 2.0f;
        dl->PathArcTo(ImVec2(waveCenter, spkMidY), half * 0.6f, -0.7f, 0.7f, 6);
        dl->PathStroke(col, false, 1.5f);

        // Wave arc 2 (if vol > 0.4)
        if (vol > 0.4f) {
            dl->PathArcTo(ImVec2(waveCenter, spkMidY), half * 0.95f, -0.7f, 0.7f, 8);
            dl->PathStroke(col, false, 1.5f);
        }
    }
}

void DrawFolder(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float w = p1.x - p0.x;
    float h = p1.y - p0.y;

    // Folder Tab
    float tabW = w * 0.45f;
    float tabH = h * 0.28f;
    dl->AddRectFilled(p0, ImVec2(p0.x + tabW, p0.y + tabH), col, 2.0f);

    // Folder Body
    float bodyY0 = p0.y + tabH * 0.7f;
    dl->AddRectFilled(ImVec2(p0.x, bodyY0), p1, col, 3.0f);

    // Subtle highlight edge on top of body
    dl->AddLine(ImVec2(p0.x + 2, bodyY0 + 1), ImVec2(p1.x - 2, bodyY0 + 1), IM_COL32(255, 255, 255, 70), 1.0f);
}

void DrawFile(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float w = p1.x - p0.x;
    float h = p1.y - p0.y;
    float fold = w * 0.32f;

    // Document polygon with corner fold
    ImVec2 pts[5] = {
        p0,
        ImVec2(p1.x - fold, p0.y),
        ImVec2(p1.x, p0.y + fold),
        p1,
        ImVec2(p0.x, p1.y)
    };
    dl->AddConvexPolyFilled(pts, 5, col);

    // Folded corner triangle
    dl->AddTriangleFilled(
        ImVec2(p1.x - fold, p0.y),
        ImVec2(p1.x, p0.y + fold),
        ImVec2(p1.x - fold, p0.y + fold),
        IM_COL32(0, 0, 0, 60)
    );
}

void DrawEye(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, bool visible) {
    float w = size * 0.5f;
    float h = size * 0.32f;

    if (visible) {
        // Eye outline
        dl->AddEllipse(pos, ImVec2(w, h), col, 0.0f, 12, 1.5f);
        // Pupil
        dl->AddCircleFilled(pos, h * 0.55f, col);
    } else {
        // Closed eye / slash
        dl->AddEllipse(pos, ImVec2(w, h * 0.6f), IM_COL32(110, 120, 140, 180), 0.0f, 12, 1.2f);
        dl->AddLine(ImVec2(pos.x - w * 1.1f, pos.y + h * 0.9f), ImVec2(pos.x + w * 1.1f, pos.y - h * 0.9f), IM_COL32(230, 80, 80, 220), 1.8f);
    }
}

void DrawSolo(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, bool active) {
    (void)size;
    (void)active;
    ImVec2 tSz = ImGui::CalcTextSize("S");
    dl->AddText(ImVec2(pos.x - tSz.x * 0.5f, pos.y - tSz.y * 0.5f), col, "S");
}

void DrawMute(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, bool active) {
    (void)size;
    (void)active;
    ImVec2 tSz = ImGui::CalcTextSize("M");
    dl->AddText(ImVec2(pos.x - tSz.x * 0.5f, pos.y - tSz.y * 0.5f), col, "M");
}

bool IconButton(const char* str_id, std::function<void(ImDrawList*, ImVec2, float, ImU32)> drawIcon, 
                float iconSize, ImVec2 buttonPadding, bool active, ImU32 activeBgColor) {
    ImVec2 bSize = ImVec2(iconSize + buttonPadding.x * 2.0f, iconSize + buttonPadding.y * 2.0f);
    
    bool pushedColors = false;
    if (active && activeBgColor != 0) {
        // Extract RGBA components
        uint32_t r = (activeBgColor >> 0) & 0xFF;
        uint32_t g = (activeBgColor >> 8) & 0xFF;
        uint32_t b = (activeBgColor >> 16) & 0xFF;
        uint32_t a = (activeBgColor >> 24) & 0xFF;

        // Hovered: lighter/brighter tint so color remains vibrant and visible
        uint32_t hr = std::min(255u, (uint32_t)(r * 1.25f + 28.0f));
        uint32_t hg = std::min(255u, (uint32_t)(g * 1.25f + 28.0f));
        uint32_t hb = std::min(255u, (uint32_t)(b * 1.25f + 28.0f));
        ImU32 hoveredBg = IM_COL32(hr, hg, hb, a);

        // Pressed / Active: deeper shade of the same active color
        uint32_t pr = (uint32_t)(r * 0.72f);
        uint32_t pg = (uint32_t)(g * 0.72f);
        uint32_t pb = (uint32_t)(b * 0.72f);
        ImU32 pressedBg = IM_COL32(pr, pg, pb, a);

        ImGui::PushStyleColor(ImGuiCol_Button, activeBgColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoveredBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, pressedBg);
        pushedColors = true;
    }

    bool pressed = ImGui::Button(str_id, bSize);

    if (pushedColors) {
        ImGui::PopStyleColor(3);
    }

    ImVec2 bMin = ImGui::GetItemRectMin();
    ImVec2 bMax = ImGui::GetItemRectMax();
    ImVec2 center = ImVec2((bMin.x + bMax.x) * 0.5f, (bMin.y + bMax.y) * 0.5f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 iconColor = active ? IM_COL32(255, 255, 255, 255) : IM_COL32(210, 220, 235, 240);
    if (ImGui::IsItemHovered()) {
        iconColor = IM_COL32(255, 255, 255, 255);
    }

    drawIcon(dl, center, iconSize, iconColor);
    return pressed;
}

} // namespace icons
} // namespace vgmstudio
