#include "channel_rack.hpp"
#include "icons.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vgmstudio {

ChannelRack::ChannelRack() {
    for (int i = 0; i < 64; ++i) {
        m_channelDecay[i] = 0.0f;
    }
}

ChannelRack::~ChannelRack() = default;

bool ChannelRack::DrawKnob(const char* label, float* pValue, float vMin, float vMax, 
                           const char* tooltipFmt, float radius, ImU32 activeColor, bool* pDoubleClicked) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 pos = ImGui::GetCursorScreenPos();
    float size = radius * 2.0f;
    ImVec2 center = ImVec2(pos.x + radius, pos.y + radius);

    ImGui::InvisibleButton(label, ImVec2(size, size));
    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();

    bool valueChanged = false;

    // Drag interaction (vertical drag)
    if (isActive && io.MouseDelta.y != 0.0f) {
        float step = (vMax - vMin) / 120.0f;
        *pValue -= io.MouseDelta.y * step;
        *pValue = std::clamp(*pValue, vMin, vMax);
        valueChanged = true;
    }

    // Double-click interaction: trigger custom percentage dialog or fallback reset
    if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (pDoubleClicked) {
            *pDoubleClicked = true;
        } else {
            *pValue = (vMin < 0.0f) ? 0.0f : 1.0f;
            valueChanged = true;
        }
    }

    // Tooltip readout
    if (isHovered || isActive) {
        if (vMin < 0.0f) {
            char panBuf[32];
            int p = (int)(*pValue * 100.0f);
            if (p < -2) std::snprintf(panBuf, sizeof(panBuf), "L %d%%", -p);
            else if (p > 2) std::snprintf(panBuf, sizeof(panBuf), "R %d%%", p);
            else std::snprintf(panBuf, sizeof(panBuf), "Center");
            ImGui::SetTooltip(tooltipFmt, panBuf);
        } else {
            ImGui::SetTooltip(tooltipFmt, *pValue * 100.0f);
        }
    }

    // Outer rim
    dl->AddCircleFilled(center, radius, IM_COL32(18, 22, 30, 255), 16);
    dl->AddCircle(center, radius, isHovered ? IM_COL32(70, 88, 115, 255) : IM_COL32(36, 46, 62, 255), 16, 1.2f);

    // Inner cap
    dl->AddCircleFilled(center, radius - 2.5f, isActive ? IM_COL32(34, 42, 56, 255) : IM_COL32(24, 30, 42, 255), 14);

    // Active arc
    float norm = (*pValue - vMin) / (vMax - vMin);
    norm = std::clamp(norm, 0.0f, 1.0f);

    float angleMin = -2.356194f; // -135 deg
    float angleMax = 2.356194f;  // +135 deg
    float curAngle = angleMin + norm * (angleMax - angleMin);

    if (vMin < 0.0f) {
        // Pan arc from center
        float a1 = std::min(0.0f, curAngle);
        float a2 = std::max(0.0f, curAngle);
        dl->PathArcTo(center, radius + 1.8f, a1 - 1.570796f, a2 - 1.570796f, 10);
        dl->PathStroke(activeColor, false, 2.0f);
    } else {
        // Volume arc from minimum
        dl->PathArcTo(center, radius + 1.8f, angleMin - 1.570796f, curAngle - 1.570796f, 12);
        dl->PathStroke(activeColor, false, 2.0f);
    }

    // Pointer needle
    float cosA = std::cos(curAngle - 1.570796f);
    float sinA = std::sin(curAngle - 1.570796f);
    ImVec2 ptrStart = ImVec2(center.x + cosA * (radius * 0.28f), center.y + sinA * (radius * 0.28f));
    ImVec2 ptrEnd = ImVec2(center.x + cosA * (radius * 0.86f), center.y + sinA * (radius * 0.86f));
    dl->AddLine(ptrStart, ptrEnd, IM_COL32(245, 250, 255, 240), 1.5f);

    return valueChanged;
}

void ChannelRack::Render(AudioEngine& audioEngine, bool* pOpen) {
    ImGui::SetNextWindowSize(ImVec2(800, 320), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin("Channels / Mixer", pOpen, flags)) {
        ImGui::End();
        return;
    }

    auto& channels = audioEngine.GetNoteTracker().GetChannels();

    float totalW = ImGui::GetContentRegionAvail().x;
    float totalH = ImGui::GetContentRegionAvail().y;

    float masterStripW = 80.0f;
    float channelsW = std::max(200.0f, totalW - masterStripW - 8.0f);

    // Left Pane: Sound Chip Channels
    ImGui::BeginChild("ChannelsMainPane", ImVec2(channelsW, totalH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // Header Title & Bulk Action Buttons
    float headerY = ImGui::GetCursorPosY() + 6.0f;
    ImGui::SetCursorPosY(headerY);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 1.0f), "Sound Chip Channels");

    if (!channels.empty()) {
        ImGui::SameLine();
        
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 4.0f));

        float btnW_MuteAll   = ImGui::CalcTextSize("Mute All").x + 12.0f;
        float btnW_Unmute    = ImGui::CalcTextSize("Unmute").x + 12.0f;
        float btnW_ClearSolo = ImGui::CalcTextSize("Clear Solo").x + 12.0f;
        float btnW_ResetVol  = ImGui::CalcTextSize("Reset Vol").x + 12.0f;
        float btnW_ResetPan  = ImGui::CalcTextSize("Reset Pan").x + 12.0f;
        float btnW_AllVis    = ImGui::CalcTextSize("All Vis").x + 12.0f;
        float totalBtnsW     = btnW_MuteAll + btnW_Unmute + btnW_ClearSolo + 
                               btnW_ResetVol + btnW_ResetPan + btnW_AllVis + (5.0f * 5.0f);

        float availW = ImGui::GetContentRegionAvail().x;
        const float rightMargin = 14.0f;
        if (availW > totalBtnsW + rightMargin + 5.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availW - totalBtnsW - rightMargin);
        }

        if (ImGui::Button("Mute All")) {
            audioEngine.MuteAllChannels(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Unmute")) {
            audioEngine.MuteAllChannels(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Solo")) {
            audioEngine.ClearAllSolo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Vol")) {
            for (size_t i = 0; i < channels.size(); ++i) {
                channels[i].volume = 1.0f;
                audioEngine.SetChannelVolume(i, 1.0f);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset all channel volumes to 100%%");
        ImGui::SameLine();
        if (ImGui::Button("Reset Pan")) {
            for (size_t i = 0; i < channels.size(); ++i) {
                channels[i].pan = 0.0f;
                audioEngine.SetChannelPan(i, 0.0f);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset all channel panning to Center");
        ImGui::SameLine();
        if (ImGui::Button("All Vis")) {
            for (auto& ch : channels) ch.visible = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make all channels visible in Piano Roll");

        ImGui::PopStyleVar(2);

        ImGui::Spacing();

        float availTableH = ImGui::GetContentRegionAvail().y;
        float neededH = (float)channels.size() * 25.0f + 26.0f; // rows + header
        bool needsScroll = (neededH > availTableH);

        ImGuiTableFlags tblFlags = ImGuiTableFlags_RowBg | 
                                   ImGuiTableFlags_SizingFixedFit | 
                                   ImGuiTableFlags_Borders;
        if (needsScroll) {
            tblFlags |= ImGuiTableFlags_ScrollY;
        }

        if (ImGui::BeginTable("ChannelsTable", 7, tblFlags, ImVec2(0, needsScroll ? availTableH : 0.0f))) {
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 78.0f);
            ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 42.0f);
            ImGui::TableSetupColumn("Chip", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn("Channel Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthFixed, 46.0f);
            ImGui::TableSetupColumn("Vol", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("Pan", ImGuiTableColumnFlags_WidthFixed, 48.0f);

            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            ImDrawList* dl = ImGui::GetWindowDrawList();

            for (size_t i = 0; i < channels.size(); ++i) {
                TrackedChannel& ch = channels[i];
                ImGui::PushID((int)i);
                ImGui::TableNextRow(0, 24.0f);

                // --- Col 0: Actions (Mute, Solo, Eye) - Centered ---
                ImGui::TableSetColumnIndex(0);
                float cellAvailW = ImGui::GetContentRegionAvail().x;
                // Buttons: Mute(17) + Solo(17) + Eye(18) + 2*spacing(4) = 60px
                float btnsTotalW = 17.0f + 17.0f + 18.0f + 8.0f;
                float startOffX = std::max(0.0f, (cellAvailW - btnsTotalW) * 0.5f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startOffX);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));

                // Mute
                auto drawMuteIcon = [ch](ImDrawList* d, ImVec2 pos, float size, ImU32 col) {
                    icons::DrawMute(d, pos, size, col, ch.muted);
                };
                if (icons::IconButton("##Mute", drawMuteIcon, 11.0f, ImVec2(3.0f, 2.0f), ch.muted, IM_COL32(185, 45, 45, 255))) {
                    audioEngine.ToggleChannelMute((int)i);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(ch.muted ? "Unmute Channel" : "Mute Channel");

                ImGui::SameLine();

                // Solo
                auto drawSoloIcon = [ch](ImDrawList* d, ImVec2 pos, float size, ImU32 col) {
                    icons::DrawSolo(d, pos, size, col, ch.solo);
                };
                if (icons::IconButton("##Solo", drawSoloIcon, 11.0f, ImVec2(3.0f, 2.0f), ch.solo, IM_COL32(35, 165, 70, 255))) {
                    audioEngine.ToggleChannelSolo((int)i);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(ch.solo ? "Unsolo Channel" : "Solo Channel");

                ImGui::SameLine();

                // Eye (Piano Roll Visibility)
                auto drawEyeIcon = [ch](ImDrawList* d, ImVec2 pos, float size, ImU32 col) {
                    icons::DrawEye(d, pos, size, col, ch.visible);
                };
                if (icons::IconButton("##Eye", drawEyeIcon, 12.0f, ImVec2(3.0f, 2.0f), ch.visible, 0)) {
                    ch.visible = !ch.visible;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(ch.visible ? "Hide from Piano Roll" : "Show on Piano Roll");

                ImGui::PopStyleVar();

                // --- Col 1: Color Picker Swatch ---
                ImGui::TableSetColumnIndex(1);
                float colCellAvailW = ImGui::GetContentRegionAvail().x;
                float pickerW = 18.0f;
                float colOffX = std::max(0.0f, (colCellAvailW - pickerW) * 0.5f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + colOffX);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);

                ImVec4 colVec = ImColor(ch.color);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
                if (ImGui::ColorEdit4("##Color", &colVec.x, 
                                      ImGuiColorEditFlags_NoInputs | 
                                      ImGuiColorEditFlags_NoLabel | 
                                      ImGuiColorEditFlags_NoAlpha)) {
                    ch.color = ImColor(colVec);
                }
                ImGui::PopStyleVar();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to change channel color");

                // --- Col 2: Sound Chip Badge ---
                ImGui::TableSetColumnIndex(2);
                ImVec2 badgeP0 = ImGui::GetCursorScreenPos();
                badgeP0.y += 2.0f;
                float cellW = ImGui::GetContentRegionAvail().x;
                float badgeW = std::max(50.0f, cellW);
                float badgeH = 18.0f;
                ImVec2 badgeP1 = ImVec2(badgeP0.x + badgeW, badgeP0.y + badgeH);
                dl->AddRectFilled(badgeP0, badgeP1, IM_COL32(22, 27, 36, 255), 2.0f);
                dl->AddRect(badgeP0, badgeP1, IM_COL32(42, 52, 70, 255), 2.0f);
                ImVec2 cTxtSz = ImGui::CalcTextSize(ch.chipName.c_str());
                float startTx = (badgeW > cTxtSz.x) ? (badgeP0.x + (badgeW - cTxtSz.x) * 0.5f) : (badgeP0.x + 3.0f);
                dl->PushClipRect(badgeP0, badgeP1, true);
                dl->AddText(ImVec2(startTx, badgeP0.y + (badgeH - cTxtSz.y) * 0.5f), 
                            IM_COL32(165, 185, 210, 255), ch.chipName.c_str());
                dl->PopClipRect();
                ImGui::Dummy(ImVec2(badgeW, badgeH));

                // --- Col 3: Channel Name ---
                ImGui::TableSetColumnIndex(3);
                ImVec4 nameCol = ch.muted ? ImVec4(0.45f, 0.50f, 0.60f, 0.6f) : ImVec4(0.92f, 0.94f, 0.98f, 1.0f);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(nameCol, "%s", ch.name.c_str());

                // --- Col 4: Note ---
                ImGui::TableSetColumnIndex(4);
                ImVec2 noteP0 = ImGui::GetCursorScreenPos();
                noteP0.y += 3.0f;
                if (ch.active) {
                    dl->AddText(noteP0, ch.color, ch.curNoteName);
                } else {
                    dl->AddText(noteP0, IM_COL32(85, 100, 120, 180), "--");
                }

                // --- Col 5: Vol ---
                ImGui::TableSetColumnIndex(5);
                ImVec2 volP = ImGui::GetCursorScreenPos();
                volP.x += 12.0f;
                volP.y += 2.0f;
                ImGui::SetCursorScreenPos(volP);
                bool dblClickVol = false;
                if (DrawKnob("##VolKnob", &ch.volume, 0.0f, 1.0f, "Vol: %.0f%% (Double-click to set %%)", 9.0f, IM_COL32(0, 220, 235, 255), &dblClickVol)) {
                    audioEngine.SetChannelVolume(i, ch.volume);
                }
                if (dblClickVol) {
                    m_editChannelIndex = i;
                    m_isEditingPan = false;
                    m_inputPercent = ch.volume * 100.0f;
                    m_openKnobInputModal = true;
                }

                // --- Col 6: Pan ---
                ImGui::TableSetColumnIndex(6);
                ImVec2 panP = ImGui::GetCursorScreenPos();
                panP.x += 12.0f;
                panP.y += 2.0f;
                ImGui::SetCursorScreenPos(panP);
                bool dblClickPan = false;
                if (DrawKnob("##PanKnob", &ch.pan, -1.0f, 1.0f, "Pan: %s (Double-click to set %%)", 9.0f, IM_COL32(245, 175, 45, 255), &dblClickPan)) {
                    audioEngine.SetChannelPan(i, ch.pan);
                }
                if (dblClickPan) {
                    m_editChannelIndex = i;
                    m_isEditingPan = true;
                    m_inputPercent = ch.pan * 100.0f;
                    m_openKnobInputModal = true;
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("Load a VGM file to inspect and mix sound channels.");
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // Right Pane: Hardware Mixer Style Master Strip
    RenderVerticalMasterStrip(audioEngine, masterStripW, totalH);

    // Percentage Input Modal Dialog (Triggered on Double-Clicking Vol or Pan)
    if (m_openKnobInputModal) {
        ImGui::OpenPopup("Set Channel Percentage##Modal");
        m_openKnobInputModal = false;
    }

    ImVec2 modalCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(modalCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 200), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Set Channel Percentage##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        if (m_editChannelIndex >= 0 && m_editChannelIndex < (int)channels.size()) {
            TrackedChannel& ch = channels[m_editChannelIndex];
            if (m_isEditingPan) {
                ImGui::TextColored(ImVec4(0.0f, 0.88f, 0.95f, 1.0f), "Set Pan Percentage (Channel %d: %s)", m_editChannelIndex + 1, ch.name.c_str());
                ImGui::TextDisabled("Enter pan from -100%% (Left) to +100%% (Right), or 0%% (Center):");
                ImGui::Spacing();
                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputFloat("##PanInputVal", &m_inputPercent, 1.0f, 5.0f, "%.0f%%");
                m_inputPercent = std::clamp(m_inputPercent, -100.0f, 100.0f);

                ImGui::Spacing();
                if (ImGui::SmallButton("100% L")) m_inputPercent = -100.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("50% L")) m_inputPercent = -50.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("Center (0%)")) m_inputPercent = 0.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("50% R")) m_inputPercent = 50.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("100% R")) m_inputPercent = 100.0f;
            } else {
                ImGui::TextColored(ImVec4(0.0f, 0.88f, 0.95f, 1.0f), "Set Volume Percentage (Channel %d: %s)", m_editChannelIndex + 1, ch.name.c_str());
                ImGui::TextDisabled("Enter volume from 0%% to 100%% (Maximum 100%%):");
                ImGui::Spacing();
                ImGui::SetNextItemWidth(220.0f);
                ImGui::InputFloat("##VolInputVal", &m_inputPercent, 1.0f, 5.0f, "%.0f%%");
                m_inputPercent = std::clamp(m_inputPercent, 0.0f, 100.0f);

                ImGui::Spacing();
                if (ImGui::SmallButton("0% (Mute)")) m_inputPercent = 0.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("25%")) m_inputPercent = 25.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("50%")) m_inputPercent = 50.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("75%")) m_inputPercent = 75.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("100%")) m_inputPercent = 100.0f;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button(" OK ", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
                if (m_isEditingPan) {
                    m_inputPercent = std::clamp(m_inputPercent, -100.0f, 100.0f);
                    ch.pan = m_inputPercent / 100.0f;
                    audioEngine.SetChannelPan(m_editChannelIndex, ch.pan);
                } else {
                    m_inputPercent = std::clamp(m_inputPercent, 0.0f, 100.0f);
                    ch.volume = m_inputPercent / 100.0f;
                    audioEngine.SetChannelVolume(m_editChannelIndex, ch.volume);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.0f, 12.0f);
            if (ImGui::Button("Cancel", ImVec2(80, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void ChannelRack::RenderTableHeader(float) {
    // Deprecated: Now cleanly handled by ImGui::TableHeadersRow()
}

void ChannelRack::RenderChannelStrip(int, TrackedChannel&, AudioEngine&) {
    // Deprecated: Now cleanly handled row by row inside Render()
}

void ChannelRack::RenderVerticalMasterStrip(AudioEngine& audioEngine, float availW, float availH) {
    ImGui::BeginChild("MasterStripPane", ImVec2(availW, availH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + availW, p0.y + availH);

    // Background Mixer Strip Chassis
    dl->AddRectFilled(p0, p1, IM_COL32(15, 18, 25, 255), 4.0f);
    dl->AddRect(p0, p1, IM_COL32(32, 40, 54, 255), 4.0f);

    // Header Pill
    float hdrH = 22.0f;
    ImVec2 hdrP1 = ImVec2(p1.x, p0.y + hdrH);
    dl->AddRectFilled(p0, hdrP1, IM_COL32(20, 25, 35, 255), 4.0f, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(p0.x, hdrP1.y), hdrP1, IM_COL32(36, 46, 62, 255), 1.0f);
    ImVec2 tSz = ImGui::CalcTextSize("Master");
    dl->AddText(ImVec2(p0.x + (availW - tSz.x) * 0.5f, p0.y + 4.0f), IM_COL32(0, 210, 225, 255), "Master");

    float topY = p0.y + hdrH + 6.0f;
    float botY = p1.y - 44.0f;
    float stripH = botY - topY;
    if (stripH < 40.0f) stripH = 40.0f;

    // --- Meters geometry: Left on far left, Right on far right ---
    float barW = 6.0f;
    float meterLX = p0.x + 8.0f;
    float meterRX = p1.x - 8.0f - barW;

    float peakL = 0.0f, peakR = 0.0f;
    audioEngine.GetPeakLevels(peakL, peakR);

    auto drawOneBar = [&](float bx, float level, const char* label) {
        ImVec2 bp0 = ImVec2(bx, topY);
        ImVec2 bp1 = ImVec2(bx + barW, botY);

        dl->AddRectFilled(bp0, bp1, IM_COL32(10, 13, 18, 255), 1.5f);
        dl->AddRect(bp0, bp1, IM_COL32(28, 36, 48, 255), 1.5f);

        float fillH = std::clamp(level, 0.0f, 1.0f) * (stripH - 2.0f);
        if (fillH > 0.0f) {
            ImU32 barCol = IM_COL32(0, 205, 120, 255);
            if (level > 0.88f) barCol = IM_COL32(235, 50, 50, 255);
            else if (level > 0.70f) barCol = IM_COL32(235, 185, 30, 255);

            dl->AddRectFilled(ImVec2(bp0.x + 1.0f, botY - 1.0f - fillH), 
                              ImVec2(bp1.x - 1.0f, botY - 1.0f), barCol, 1.0f);
        }

        // dB tick lines
        dl->AddLine(ImVec2(bp0.x, botY - 0.50f * stripH), ImVec2(bp1.x, botY - 0.50f * stripH), IM_COL32(60, 75, 95, 180));
        dl->AddLine(ImVec2(bp0.x, botY - 0.89f * stripH), ImVec2(bp1.x, botY - 0.89f * stripH), IM_COL32(90, 45, 45, 180));

        ImVec2 lblSz = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(bx + (barW - lblSz.x) * 0.5f, botY + 2.0f), IM_COL32(140, 155, 175, 255), label);
    };

    // --- 1. Left Meter (L) ---
    drawOneBar(meterLX, peakL, "L");

    // --- 2. Right Meter (R) ---
    drawOneBar(meterRX, peakR, "R");

    // --- 3. Middle: Vertical Mixer Fader Slider ---
    float railCenterX = p0.x + availW * 0.5f;
    float faderAreaX = meterLX + barW + 2.0f;
    float faderAreaW = (meterRX - 2.0f) - faderAreaX;

    // Fader rail
    float railW = 4.0f;
    dl->AddRectFilled(ImVec2(railCenterX - railW * 0.5f, topY + 4.0f), 
                      ImVec2(railCenterX + railW * 0.5f, botY - 4.0f), IM_COL32(10, 12, 16, 255), 2.0f);
    dl->AddRect(ImVec2(railCenterX - railW * 0.5f, topY + 4.0f), 
                ImVec2(railCenterX + railW * 0.5f, botY - 4.0f), IM_COL32(30, 38, 50, 255), 2.0f);

    // Fader Cap Handle
    float curVol = audioEngine.GetVolume();
    float maxVol = 1.20f;
    float normVol = std::clamp(curVol / maxVol, 0.0f, 1.0f);
    float capY = botY - normVol * stripH;

    float capW = 24.0f;
    float capH = 14.0f;
    ImVec2 capMin = ImVec2(railCenterX - capW * 0.5f, capY - capH * 0.5f);
    ImVec2 capMax = ImVec2(railCenterX + capW * 0.5f, capY + capH * 0.5f);

    ImGui::SetCursorScreenPos(ImVec2(faderAreaX, topY));
    ImGui::InvisibleButton("##MasterFaderTrack", ImVec2(faderAreaW, stripH));
    bool isFaderHovered = ImGui::IsItemHovered();
    bool isFaderActive = ImGui::IsItemActive();

    ImGuiIO& io = ImGui::GetIO();
    if (isFaderActive) {
        float dragRatio = (botY - io.MousePos.y) / stripH;
        dragRatio = std::clamp(dragRatio, 0.0f, 1.0f);
        float newVol = dragRatio * maxVol;
        audioEngine.SetVolume(newVol);
        if (newVol > 0.01f) m_masterMuted = false;
        else m_masterMuted = true;
    }
    if (isFaderHovered || isFaderActive) {
        ImGui::SetTooltip("Master Volume: %.0f%%", audioEngine.GetVolume() * 100.0f);
    }

    // Metallic cap with notch
    ImU32 capBg = isFaderActive ? IM_COL32(45, 55, 72, 255) : (isFaderHovered ? IM_COL32(40, 48, 62, 255) : IM_COL32(30, 36, 48, 255));
    dl->AddRectFilled(capMin, capMax, capBg, 2.5f);
    dl->AddRect(capMin, capMax, isFaderActive ? IM_COL32(0, 210, 225, 255) : IM_COL32(50, 62, 80, 255), 2.5f, 0, 1.2f);
    dl->AddLine(ImVec2(capMin.x + 3.0f, capY), ImVec2(capMax.x - 3.0f, capY), IM_COL32(0, 210, 225, 255), 1.5f);

    // --- 4. Bottom: Master Mute Button (volume text above removed) ---
    ImVec2 muteBtnP0 = ImVec2(p0.x + 8.0f, botY + 18.0f);
    ImGui::SetCursorScreenPos(muteBtnP0);
    ImGui::PushStyleColor(ImGuiCol_Button, m_masterMuted ? IM_COL32(180, 45, 45, 255) : IM_COL32(28, 35, 46, 255));
    if (ImGui::Button(m_masterMuted ? "Unmute" : "Mute", ImVec2(availW - 16.0f, 20.0f))) {
        if (m_masterMuted) {
            float restored = (m_savedMasterVol > 0.05f) ? m_savedMasterVol : 0.8f;
            audioEngine.SetVolume(restored);
            m_masterMuted = false;
        } else {
            m_savedMasterVol = (curVol > 0.05f) ? curVol : 0.8f;
            audioEngine.SetVolume(0.0f);
            m_masterMuted = true;
        }
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_masterMuted ? "Unmute Master" : "Mute Master");

    ImGui::EndChild();
}

} // namespace vgmstudio
