#include "theme.hpp"
#include "../third_party/imgui/imgui.h"

namespace vgmstudio {
namespace theme {

void ApplyFurnaceTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Palette:
    // Deep Charcoal Background: #14161B
    // Card / Frame Background:  #1C1E26
    // Frame Hovered:            #242833
    // Accent Cyan / Teal:       #00ADB5 / #38B2AC
    // Accent Hover:             #4FD1C5
    // Accent Active:            #237A74
    // Text Primary:             #EDF2F7
    // Text Disabled:            #718096
    // Border Subtle:            #2B2F3D

    colors[ImGuiCol_Text]                  = ImVec4(0.93f, 0.95f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.44f, 0.50f, 0.59f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.11f, 0.12f, 0.16f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.17f, 0.19f, 0.24f, 1.00f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.16f, 0.18f, 0.23f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.07f, 0.08f, 0.10f, 0.75f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.08f, 0.09f, 0.11f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.22f, 0.25f, 0.32f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.28f, 0.32f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.00f, 0.68f, 0.71f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.00f, 0.82f, 0.85f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.00f, 0.68f, 0.71f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.31f, 0.82f, 0.77f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.21f, 0.24f, 0.31f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.00f, 0.55f, 0.58f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.16f, 0.18f, 0.23f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.22f, 0.25f, 0.32f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.00f, 0.60f, 0.63f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.17f, 0.19f, 0.24f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.00f, 0.68f, 0.71f, 0.78f);
    colors[ImGuiCol_SeparatorActive]       = ImVec4(0.00f, 0.68f, 0.71f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.20f, 0.23f, 0.30f, 0.50f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.00f, 0.68f, 0.71f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.00f, 0.68f, 0.71f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.12f, 0.13f, 0.17f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.16f, 0.19f, 0.25f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
    colors[ImGuiCol_DockingPreview]        = ImVec4(0.00f, 0.68f, 0.71f, 0.35f);
    colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
    colors[ImGuiCol_PlotLines]             = ImVec4(0.00f, 0.82f, 0.85f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram]         = ImVec4(0.00f, 0.68f, 0.71f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.31f, 0.82f, 0.77f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.19f, 0.21f, 0.27f, 1.00f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.00f, 0.68f, 0.71f, 0.35f);
    colors[ImGuiCol_DragDropTarget]        = ImVec4(0.00f, 0.82f, 0.85f, 0.90f);
    colors[ImGuiCol_NavHighlight]          = ImVec4(0.00f, 0.68f, 0.71f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.10f, 0.10f, 0.10f, 0.60f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.05f, 0.05f, 0.07f, 0.75f);

    // Style adjustments
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(7.0f, 4.0f);
    style.CellPadding       = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 10.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 1.0f;

    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;

    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
}

void ApplyClassicTrackerTheme() {
    ApplyFurnaceTheme();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Classic FastTracker / MilkyTracker deep navy with cyan accent
    colors[ImGuiCol_WindowBg]             = ImVec4(0.05f, 0.08f, 0.14f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.07f, 0.10f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.09f, 0.13f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.14f, 0.20f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.18f, 0.25f, 0.40f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.06f, 0.09f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.10f, 0.15f, 0.26f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.11f, 0.17f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.16f, 0.24f, 0.38f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.00f, 0.55f, 0.75f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.12f, 0.18f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.18f, 0.26f, 0.42f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.00f, 0.60f, 0.80f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.00f, 0.85f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.00f, 0.70f, 0.85f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.00f, 0.90f, 1.00f, 1.00f);
}

void ApplyCyberpunkTheme() {
    ApplyFurnaceTheme();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Cyberpunk dark indigo with neon magenta and neon cyan
    colors[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.06f, 0.12f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.11f, 0.08f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.15f, 0.10f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.14f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.30f, 0.18f, 0.42f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.07f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.18f, 0.10f, 0.26f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.20f, 0.12f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.32f, 0.16f, 0.42f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.85f, 0.10f, 0.50f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(1.00f, 0.20f, 0.65f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.85f, 0.15f, 0.55f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(1.00f, 0.30f, 0.70f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.24f, 0.12f, 0.34f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.35f, 0.18f, 0.48f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.80f, 0.15f, 0.50f, 1.00f);
}

void ApplyMonokaiTheme() {
    ApplyFurnaceTheme();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Monokai Pro charcoal slate with amber/warm accents
    colors[ImGuiCol_WindowBg]             = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.85f, 0.65f, 0.15f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.95f, 0.75f, 0.20f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.85f, 0.65f, 0.15f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.95f, 0.75f, 0.20f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.75f, 0.60f, 0.15f, 1.00f);
}

void ApplyTheme(int presetIndex) {
    switch (presetIndex) {
        case 1: ApplyClassicTrackerTheme(); break;
        case 2: ApplyCyberpunkTheme(); break;
        case 3: ApplyMonokaiTheme(); break;
        case 0:
        default:
            ApplyFurnaceTheme();
            break;
    }
}

} // namespace theme
} // namespace vgmstudio
