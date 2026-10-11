#pragma once

#include <imgui.h>
#include <functional>

namespace vgmstudio {
namespace icons {

// Drawing primitives for procedural vector geometry (strictly zero emojis)
void DrawPlay(ImDrawList* dl, ImVec2 pos, float size, ImU32 col);
void DrawPause(ImDrawList* dl, ImVec2 pos, float size, ImU32 col);
void DrawStop(ImDrawList* dl, ImVec2 pos, float size, ImU32 col);
void DrawRewind(ImDrawList* dl, ImVec2 pos, float size, ImU32 col);
void DrawLoop(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, bool active = true);
void DrawVolume(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, float vol = 1.0f);
void DrawFolder(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col);
void DrawFile(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col);
void DrawEye(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, bool visible = true);
void DrawSolo(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, bool active = false);
void DrawMute(ImDrawList* dl, ImVec2 pos, float size, ImU32 col, bool active = false);

// Custom styled vector icon button
bool IconButton(const char* str_id, std::function<void(ImDrawList*, ImVec2, float, ImU32)> drawIcon, 
                float iconSize = 14.0f, ImVec2 buttonPadding = ImVec2(7.0f, 5.0f), 
                bool active = false, ImU32 activeBgColor = 0);

} // namespace icons
} // namespace vgmstudio
