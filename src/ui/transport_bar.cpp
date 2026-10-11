#include "transport_bar.hpp"
#include "icons.hpp"
#include "piano_roll.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace vgmstudio {

static std::string FormatDigitalTime(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    int totalCenti = (int)(seconds * 100.0);
    int centi = totalCenti % 100;
    int totalSec = totalCenti / 100;
    int sec = totalSec % 60;
    int min = totalSec / 60;

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d.%02d", min, sec, centi);
    return std::string(buf);
}

TransportBar::TransportBar() {
}

TransportBar::~TransportBar() {
}

void TransportBar::Render(AudioEngine& audioEngine, FileDialog& fileDialog, PianoRoll* pianoRoll, bool* pOpen, bool* pPlayToggled, bool* pShowSpeed) {
    ImGui::SetNextWindowSize(ImVec2(800, 75), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Player Control", pOpen, flags)) {
        ImGui::End();
        return;
    }

    bool isPlaying = audioEngine.IsPlaying();
    bool isLoaded = audioEngine.IsLoaded();
    const auto& info = audioEngine.GetCurrentInfo();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

    // 1. Open Button (Vector Folder Icon)
    if (icons::IconButton("##OpenBtn", [](ImDrawList* dl, ImVec2 pos, float size, ImU32 col) {
        float half = size * 0.5f;
        icons::DrawFolder(dl, ImVec2(pos.x - half, pos.y - half * 0.8f), ImVec2(pos.x + half, pos.y + half * 0.8f), col);
    }, 15.0f, ImVec2(9.0f, 5.0f))) {
        fileDialog.Open("samples");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open VGM / VGZ File (Ctrl+O)");

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // 2. Transport Buttons: Rewind, Play/Pause, Stop
    // Rewind
    if (icons::IconButton("##RewindBtn", icons::DrawRewind, 14.0f, ImVec2(8.0f, 5.0f))) {
        audioEngine.Rewind();
        if (pianoRoll) pianoRoll->RewindToStart();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rewind to Beginning (Home)");

    ImGui::SameLine();

    // Play / Pause
    ImU32 playActiveColor = isPlaying ? IM_COL32(0, 155, 170, 255) : 0;
    bool playPressed = false;
    if (isPlaying) {
        playPressed = icons::IconButton("##PlayPauseBtn", icons::DrawPause, 14.0f, ImVec2(10.0f, 5.0f), true, playActiveColor);
    } else {
        playPressed = icons::IconButton("##PlayPauseBtn", icons::DrawPlay, 14.0f, ImVec2(10.0f, 5.0f), false, 0);
    }
    if (playPressed) {
        audioEngine.TogglePlayPause();
        if (pPlayToggled) *pPlayToggled = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(isPlaying ? "Pause (Space)" : "Play (Space)");

    ImGui::SameLine();

    // Stop
    if (icons::IconButton("##StopBtn", icons::DrawStop, 13.0f, ImVec2(8.0f, 5.0f))) {
        audioEngine.Stop();
        if (pianoRoll) pianoRoll->RewindToStart();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop Playback");

    ImGui::SameLine();

    // Loop Toggle
    bool loop = audioEngine.GetLoop();
    ImU32 loopActiveColor = loop ? IM_COL32(28, 155, 95, 255) : 0;
    if (icons::IconButton("##LoopBtn", [](ImDrawList* dl, ImVec2 pos, float size, ImU32 col) {
        icons::DrawLoop(dl, pos, size, col, true);
    }, 14.0f, ImVec2(8.0f, 5.0f), loop, loopActiveColor)) {
        audioEngine.SetLoop(!loop);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(loop ? "Looping Enabled (Click to Toggle)" : "Looping Disabled (Click to Toggle)");

    ImGui::SameLine();

    // Speed badge button (toggles View -> Speed)
    float curSpd = audioEngine.GetPlaybackSpeed();
    float curPitch = audioEngine.GetPitchSemitones();
    char spdBuf[32];
    if (std::abs(curPitch) > 0.05f) {
        std::snprintf(spdBuf, sizeof(spdBuf), "%.2fx (%+.1fst)##SpdBtn", curSpd, curPitch);
    } else {
        std::snprintf(spdBuf, sizeof(spdBuf), "%.2fx##SpdBtn", curSpd);
    }
    bool speedModified = (std::abs(curSpd - 1.0f) > 0.01f || std::abs(curPitch) > 0.05f);
    if (speedModified) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.25f, 1.0f));
    }
    float origY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(origY + 2.0f); // Nudge down slightly so it aligns nicely with adjacent controls
    if (ImGui::Button(spdBuf)) {
        if (pShowSpeed) *pShowSpeed = !(*pShowSpeed);
    }
    if (speedModified) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Playback Speed: %.2fx | Pitch: %+.1f st\nClick to toggle Speed window", curSpd, curPitch);
    }

    ImGui::SameLine();
    ImGui::SetCursorPosY(origY + 2.5f);
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::SetCursorPosY(origY + 2.5f);

    // 3. Digital Clock & Samples Display
    double curSec = audioEngine.GetCurSeconds();
    double totSec = audioEngine.GetTotalSeconds();
    std::string curTimeStr = FormatDigitalTime(curSec);
    std::string totTimeStr = FormatDigitalTime(totSec);

    // Neon cyan digital time
    ImGui::TextColored(ImVec4(0.00f, 0.90f, 0.95f, 1.0f), "%s", curTimeStr.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosY(origY + 2.5f);
    ImGui::TextDisabled("/ %s", totTimeStr.c_str());

    uint32_t totSmpl = audioEngine.GetTotalSamples();

    // 4. High-Precision Seek Bar (Scrubber)
    ImGui::SetCursorPosY(origY + 35.0f);

    ImVec2 barP0 = ImGui::GetCursorScreenPos();
    float availW = ImGui::GetContentRegionAvail().x;
    float barH = 14.0f;
    ImVec2 barP1 = ImVec2(barP0.x + availW, barP0.y + barH);

    // Invisible button for robust mouse interaction and zero click-through
    ImGui::InvisibleButton("##SeekBarArea", ImVec2(availW, barH));
    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();

    static bool s_isScrubbing = false;
    static float s_scrubFraction = 0.0f;

    // 1. Initial click (press down): seek once to the clicked position
    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float frac = (ImGui::GetIO().MousePos.x - barP0.x) / availW;
        frac = std::clamp(frac, 0.0f, 1.0f);
        s_scrubFraction = frac;
        audioEngine.SeekToFraction(frac);
    }

    // 2. Dragging (Scrubbing): only when mouse has actually moved/dragged significantly
    if (isActive) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
            s_isScrubbing = true;
            float frac = (ImGui::GetIO().MousePos.x - barP0.x) / availW;
            frac = std::clamp(frac, 0.0f, 1.0f);
            s_scrubFraction = frac;
            audioEngine.SeekToFraction(frac);
        }
    } else {
        s_isScrubbing = false;
    }

    float progress = s_isScrubbing ? s_scrubFraction : ((totSec > 0.0) ? (float)(curSec / totSec) : 0.0f);
    progress = std::clamp(progress, 0.0f, 1.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background track
    dl->AddRectFilled(barP0, barP1, IM_COL32(20, 24, 32, 255), 3.0f);
    dl->AddRect(barP0, barP1, IM_COL32(40, 48, 62, 255), 3.0f);

    // Loop Region Highlight (if song has loop)
    if (info.hasLoop && totSmpl > 0 && info.loopSamples > 0) {
        uint32_t loopStart = totSmpl - info.loopSamples;
        float loopStartFrac = (float)loopStart / (float)totSmpl;
        float lx0 = barP0.x + loopStartFrac * availW;
        float lx1 = barP1.x;

        // Loop zone tint
        dl->AddRectFilled(ImVec2(lx0, barP0.y + 1), ImVec2(lx1, barP1.y - 1), IM_COL32(0, 173, 181, 40));
        // Loop start indicator line
        dl->AddLine(ImVec2(lx0, barP0.y), ImVec2(lx0, barP1.y), IM_COL32(0, 220, 230, 200), 1.5f);
        // Loop marker label
        dl->AddText(ImVec2(lx0 + 3.0f, barP0.y + 1.0f), IM_COL32(0, 220, 230, 180), "|L");
    }

    // Filled progress bar
    float fillW = progress * availW;
    if (fillW > 0.0f) {
        dl->AddRectFilled(
            ImVec2(barP0.x + 1, barP0.y + 1),
            ImVec2(barP0.x + fillW, barP1.y - 1),
            IM_COL32(0, 173, 181, 230),
            2.0f
        );
        // Gloss highlight line on top edge
        dl->AddLine(
            ImVec2(barP0.x + 2, barP0.y + 2),
            ImVec2(barP0.x + fillW - 1, barP0.y + 2),
            IM_COL32(255, 255, 255, 120),
            1.0f
        );
    }

    // Playhead Scrubber Handle
    float handleX = barP0.x + fillW;
    dl->AddCircleFilled(ImVec2(handleX, barP0.y + barH * 0.5f), 5.5f, IM_COL32(255, 255, 255, 255));
    dl->AddCircle(ImVec2(handleX, barP0.y + barH * 0.5f), 5.5f, IM_COL32(0, 173, 181, 255), 12, 1.5f);

    if (isHovered || isActive) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        float hoverFrac = s_isScrubbing ? s_scrubFraction : ((ImGui::GetIO().MousePos.x - barP0.x) / availW);
        hoverFrac = std::clamp(hoverFrac, 0.0f, 1.0f);
        double hoverSec = hoverFrac * totSec;
        ImGui::SetTooltip("Seek to %s", FormatDigitalTime(hoverSec).c_str());
    }

    ImGui::PopStyleVar();
    ImGui::End();
}

} // namespace vgmstudio
