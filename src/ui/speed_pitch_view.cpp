#include "speed_pitch_view.hpp"
#include "../audio/audio_engine.hpp"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace vgmstudio {

SpeedPitchView::SpeedPitchView() = default;
SpeedPitchView::~SpeedPitchView() = default;

void SpeedPitchView::Render(AudioEngine& audioEngine, bool* pOpen) {
    ImGui::SetNextWindowSize(ImVec2(340, 240), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Speed", pOpen)) {
        ImGui::End();
        return;
    }

    // 1. Linking option (Follow Pitch)
    bool link = audioEngine.GetLinkSpeedPitch();
    if (ImGui::Checkbox("Follow Pitch (Link Speed & Pitch)", &link)) {
        audioEngine.SetLinkSpeedPitch(link);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When checked, changing speed alters pitch (tape style), and vice versa.");
    }

    ImGui::TextDisabled(link ? "Speed and pitch scale together proportionally." : "Speed and pitch are controlled independently.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Playback Speed
    float curSpeed = audioEngine.GetPlaybackSpeed();
    ImGui::TextColored(ImVec4(0.0f, 0.88f, 0.95f, 1.0f), "Playback Speed: %.2fx", curSpeed);

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##SpeedSlider", &curSpeed, 0.25f, 2.50f, "%.2fx")) {
        audioEngine.SetPlaybackSpeed(curSpeed);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 3. Pitch Shift
    float curPitch = audioEngine.GetPitchSemitones();
    float curRatio = std::pow(2.0f, curPitch / 12.0f);
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Pitch Shift: %+.1f st (%.2fx)", curPitch, curRatio);

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##PitchSlider", &curPitch, -12.0f, 12.0f, "%+.1f st")) {
        audioEngine.SetPitchSemitones(curPitch);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 4. Quick Reset
    if (ImGui::Button("Reset to Default", ImVec2(-1.0f, 0))) {
        audioEngine.ResetSpeedAndPitch();
    }

    ImGui::End();
}

} // namespace vgmstudio
