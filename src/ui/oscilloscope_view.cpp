#include "oscilloscope_view.hpp"
#include "icons.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace vgmstudio {

OscilloscopeView::OscilloscopeView() {
    m_waveBuffer.resize(1024, 0.0f);
    m_polyPoints.resize(1024);
}

OscilloscopeView::~OscilloscopeView() = default;

void OscilloscopeView::Render(AudioEngine& audioEngine, bool* pOpen) {
    if (m_focusRequested) {
        ImGui::SetNextWindowFocus();
        m_focusRequested = false;
    }

    ImGui::SetNextWindowSize(ImVec2(880, 520), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Oscilloscope Visualizer", pOpen)) {
        ImGui::End();
        return;
    }

    auto& noteTracker = audioEngine.GetNoteTracker();
    auto& channels = noteTracker.GetChannels();

    if (channels.empty() || !audioEngine.IsLoaded()) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(20.0f, avail.y * 0.35f));

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.65f, 0.90f, 1.0f));
        const char* title = "No VGM Song Loaded";
        float titleW = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX(std::max(0.0f, (avail.x - titleW) * 0.5f));
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        const char* desc = "Load a VGM or VGZ file to monitor real-time channel oscilloscope waveforms.";
        float descW = ImGui::CalcTextSize(desc).x;
        ImGui::SetCursorPosX(std::max(0.0f, (avail.x - descW) * 0.5f));
        ImGui::TextDisabled("%s", desc);

        ImGui::End();
        return;
    }

    // Top Controls Toolbar
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

    // Columns selector
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Cols:");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(78.0f);
    const char* colModes[] = { "Auto", "1 Col", "2 Cols", "3 Cols", "4 Cols", "6 Cols" };
    ImGui::Combo("##ColMode", &m_columnMode, colModes, IM_ARRAYSIZE(colModes));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Grid column count layout");

    // Style selector
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Style:");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(100.0f);
    int styleIdx = (int)m_style;
    const char* styleNames[] = { "Neon Glow", "Crisp Line", "Filled Area" };
    if (ImGui::Combo("##OscStyle", &styleIdx, styleNames, IM_ARRAYSIZE(styleNames))) {
        m_style = (OscWaveStyle)styleIdx;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Waveform drawing style");

    // Gain slider
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Gain:");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(82.0f);
    ImGui::SliderFloat("##Gain", &m_gain, 0.5f, 4.0f, "%.1fx");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Vertical gain multiplier (Double-click to reset to 1.0x)");
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_gain = 1.0f;
        }
    }

    // Window Length / Timebase
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Zoom:");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(90.0f);
    int lengthIdx = (m_sampleCount == 256 ? 0 : (m_sampleCount == 1024 ? 2 : 1));
    const char* lengthNames[] = { "256 smp", "512 smp", "1024 smp" };
    if (ImGui::Combo("##SampleCount", &lengthIdx, lengthNames, IM_ARRAYSIZE(lengthNames))) {
        m_sampleCount = (lengthIdx == 0 ? 256 : (lengthIdx == 2 ? 1024 : 512));
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Window timebase (samples per frame)");

    // Grid toggle
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &m_showGrid);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show oscilloscope reticle and center baseline");

    // Hide Muted toggle
    ImGui::SameLine();
    ImGui::Checkbox("Hide Muted", &m_hideMuted);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide channels that are currently muted");

    // Right-aligned bulk actions: Mute All, Unmute, Clear Solo
    float btnW_MuteAll = ImGui::CalcTextSize("Mute All").x + 12.0f;
    float btnW_Unmute = ImGui::CalcTextSize("Unmute").x + 12.0f;
    float btnW_ClearSolo = ImGui::CalcTextSize("Clear Solo").x + 12.0f;
    float rightBtnsW = btnW_MuteAll + btnW_Unmute + btnW_ClearSolo + (2.0f * 8.0f);
    float availW = ImGui::GetContentRegionAvail().x;
    if (availW > rightBtnsW + 10.0f) {
        ImGui::SameLine(0, availW - rightBtnsW - 4.0f);
    } else {
        ImGui::SameLine();
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

    ImGui::PopStyleVar(2);
    ImGui::Separator();
    ImGui::Spacing();

    // Filter visible channels
    std::vector<size_t> visibleIndices;
    visibleIndices.reserve(channels.size());
    for (size_t i = 0; i < channels.size(); ++i) {
        if (m_hideMuted && channels[i].muted) continue;
        visibleIndices.push_back(i);
    }

    if (visibleIndices.empty()) {
        ImGui::TextDisabled("All channels are currently muted and hidden.");
        ImGui::End();
        return;
    }

    // Determine column count
    int cols = 1;
    if (m_columnMode == 0) {
        float totalAvailW = ImGui::GetContentRegionAvail().x;
        cols = std::clamp((int)(totalAvailW / 310.0f), 1, 6);
    } else {
        const int presetCols[] = { 1, 1, 2, 3, 4, 6 };
        int idx = std::clamp(m_columnMode, 1, 5);
        cols = presetCols[idx];
    }

    ImGuiTableFlags tblFlags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("OscGridTable", cols, tblFlags)) {
        for (int c = 0; c < cols; ++c) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        }

        const float cardH = 126.0f;
        const float canvasH = 92.0f;

        for (size_t vi = 0; vi < visibleIndices.size(); ++vi) {
            size_t i = visibleIndices[vi];
            auto& ch = channels[i];

            ImGui::TableNextColumn();
            ImGui::PushID((int)i);

            ImVec2 cellP0 = ImGui::GetCursorScreenPos();
            float cellW = ImGui::GetContentRegionAvail().x;
            ImVec2 cardP0 = cellP0;
            ImVec2 cardP1 = ImVec2(cardP0.x + cellW, cardP0.y + cardH);

            bool isVisible = ImGui::IsRectVisible(ImVec2(cellW, cardH));
            if (!isVisible) {
                ImGui::Dummy(ImVec2(cellW, cardH + 6.0f));
                ImGui::PopID();
                continue;
            }

            ImDrawList* dl = ImGui::GetWindowDrawList();
            bool isHovered = ImGui::IsMouseHoveringRect(cardP0, cardP1);

            // Card background and border
            ImU32 cardBg = IM_COL32(16, 21, 29, 240);
            ImU32 cardBorder = isHovered ? IM_COL32(65, 105, 160, 255) :
                               (ch.active ? IM_COL32(45, 68, 98, 255) : IM_COL32(30, 39, 54, 255));
            dl->AddRectFilled(cardP0, cardP1, cardBg, 5.0f);
            dl->AddRect(cardP0, cardP1, cardBorder, 5.0f, 0, 1.0f);

            // Double click anywhere on card toggles solo
            if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                audioEngine.ToggleChannelSolo((int)i);
            }

            // --- Header: Badge, Names, Note Pill, Mute/Solo Buttons ---
            // 1. Channel Index Pill
            ImU32 badgeCol = ch.color ? ch.color : IM_COL32(0, 220, 240, 255);
            float badgeW = 22.0f;
            float badgeH = 16.0f;
            ImVec2 bp0 = ImVec2(cardP0.x + 6.0f, cardP0.y + 5.0f);
            ImVec2 bp1 = ImVec2(bp0.x + badgeW, bp0.y + badgeH);
            dl->AddRectFilled(bp0, bp1, badgeCol, 3.0f);
            char idBuf[16];
            std::snprintf(idBuf, sizeof(idBuf), "%d", ch.id + 1);
            ImVec2 idTextSz = ImGui::CalcTextSize(idBuf);
            dl->AddText(ImVec2(bp0.x + (badgeW - idTextSz.x) * 0.5f, bp0.y + (badgeH - idTextSz.y) * 0.5f),
                        IM_COL32(10, 15, 20, 255), idBuf);

            // 2. Chip & Channel Name
            char titleBuf[64];
            std::snprintf(titleBuf, sizeof(titleBuf), "%s  %s", ch.chipName.c_str(), ch.name.c_str());
            dl->AddText(ImVec2(bp1.x + 6.0f, cardP0.y + 5.0f), IM_COL32(230, 238, 248, 255), titleBuf);

            // 3. Right side header items: Note badge pill, Mute, Solo
            float rightItemsW = 76.0f;
            float rightStartX = cardP1.x - 6.0f - rightItemsW;

            // Note pill
            ImVec2 np0 = ImVec2(rightStartX, cardP0.y + 4.0f);
            ImVec2 np1 = ImVec2(np0.x + 34.0f, np0.y + 17.0f);
            if (ch.active && ch.curNoteName[0] != '-' && ch.curNoteName[0] != '\0') {
                dl->AddRectFilled(np0, np1, IM_COL32(24, 90, 48, 220), 3.0f);
                dl->AddRect(np0, np1, IM_COL32(40, 160, 80, 220), 3.0f, 0, 1.0f);
                ImVec2 nSz = ImGui::CalcTextSize(ch.curNoteName);
                dl->AddText(ImVec2(np0.x + (34.0f - nSz.x) * 0.5f, np0.y + (17.0f - nSz.y) * 0.5f),
                            IM_COL32(165, 255, 185, 255), ch.curNoteName);
            } else {
                dl->AddRectFilled(np0, np1, IM_COL32(20, 26, 36, 180), 3.0f);
                ImVec2 nSz = ImGui::CalcTextSize("--");
                dl->AddText(ImVec2(np0.x + (34.0f - nSz.x) * 0.5f, np0.y + (17.0f - nSz.y) * 0.5f),
                            IM_COL32(85, 100, 120, 200), "--");
            }

            // Interactive Mute & Solo buttons
            ImGui::SetCursorScreenPos(ImVec2(np1.x + 4.0f, cardP0.y + 3.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 0.0f));

            auto drawMuteIcon = [ch](ImDrawList* d, ImVec2 pos, float size, ImU32 col) {
                icons::DrawMute(d, pos, size, col, ch.muted);
            };
            if (icons::IconButton("##Mute", drawMuteIcon, 10.0f, ImVec2(3.0f, 2.0f), ch.muted, IM_COL32(185, 45, 45, 255))) {
                audioEngine.ToggleChannelMute((int)i);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(ch.muted ? "Unmute Channel" : "Mute Channel");

            ImGui::SameLine();

            auto drawSoloIcon = [ch](ImDrawList* d, ImVec2 pos, float size, ImU32 col) {
                icons::DrawSolo(d, pos, size, col, ch.solo);
            };
            if (icons::IconButton("##Solo", drawSoloIcon, 10.0f, ImVec2(3.0f, 2.0f), ch.solo, IM_COL32(35, 165, 70, 255))) {
                audioEngine.ToggleChannelSolo((int)i);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(ch.solo ? "Unsolo Channel" : "Solo Channel");

            ImGui::PopStyleVar();

            // --- Oscilloscope Display Canvas ---
            ImVec2 canvasP0 = ImVec2(cardP0.x + 6.0f, cardP0.y + 25.0f);
            ImVec2 canvasP1 = ImVec2(cardP1.x - 6.0f, canvasP0.y + canvasH);
            float cW = canvasP1.x - canvasP0.x;
            float cH = canvasP1.y - canvasP0.y;

            // CRT Screen background
            dl->AddRectFilled(canvasP0, canvasP1, IM_COL32(8, 12, 17, 255), 4.0f);
            dl->AddRect(canvasP0, canvasP1, IM_COL32(26, 36, 48, 255), 4.0f, 0, 1.0f);

            dl->PushClipRect(canvasP0, canvasP1, true);

            float midY = canvasP0.y + cH * 0.5f;

            // Scope reticle grid lines
            if (m_showGrid) {
                // Baseline (Center zero)
                dl->AddLine(ImVec2(canvasP0.x, midY), ImVec2(canvasP1.x, midY), IM_COL32(35, 50, 68, 180), 1.0f);

                // Quarter amplitude lines
                dl->AddLine(ImVec2(canvasP0.x, canvasP0.y + cH * 0.25f), ImVec2(canvasP1.x, canvasP0.y + cH * 0.25f), IM_COL32(20, 30, 42, 100), 1.0f);
                dl->AddLine(ImVec2(canvasP0.x, canvasP0.y + cH * 0.75f), ImVec2(canvasP1.x, canvasP0.y + cH * 0.75f), IM_COL32(20, 30, 42, 100), 1.0f);

                // Vertical time divisions
                for (int div = 1; div < 4; ++div) {
                    float vx = canvasP0.x + (cW * (float)div) / 4.0f;
                    dl->AddLine(ImVec2(vx, canvasP0.y), ImVec2(vx, canvasP1.y), IM_COL32(20, 30, 42, 100), 1.0f);
                }
            }

            // Fetch real-time waveform from AudioEngine
            if ((int)m_waveBuffer.size() < m_sampleCount) {
                m_waveBuffer.resize(m_sampleCount, 0.0f);
            }
            if ((int)m_polyPoints.size() < m_sampleCount) {
                m_polyPoints.resize(m_sampleCount);
            }

            audioEngine.GetChannelWaveform(i, m_waveBuffer.data(), m_sampleCount);

            for (int s = 0; s < m_sampleCount; ++s) {
                float normVal = std::clamp(m_waveBuffer[s] * m_gain, -1.0f, 1.0f);
                float px = canvasP0.x + ((float)s / (float)(m_sampleCount - 1)) * cW;
                float py = midY - normVal * (cH * 0.44f);
                m_polyPoints[s] = ImVec2(px, py);
            }

            // Color selection
            ImU32 baseCol = ch.color ? ch.color : IM_COL32(0, 220, 240, 255);
            if (ch.muted) {
                baseCol = IM_COL32(75, 80, 90, 110);
            }
            int r = (baseCol) & 0xFF;
            int g = (baseCol >> 8) & 0xFF;
            int b = (baseCol >> 16) & 0xFF;

            if (m_style == OscWaveStyle::Filled && !ch.muted) {
                // Filled area from baseline to waveform slices
                for (int s = 0; s < m_sampleCount - 1; ++s) {
                    ImVec2 p0(m_polyPoints[s].x, midY);
                    ImVec2 p1(m_polyPoints[s].x, m_polyPoints[s].y);
                    ImVec2 p2(m_polyPoints[s+1].x, m_polyPoints[s+1].y);
                    ImVec2 p3(m_polyPoints[s+1].x, midY);
                    dl->AddQuadFilled(p0, p1, p2, p3, IM_COL32(r, g, b, 45));
                }
                // Crisp line stroke on top
                dl->AddPolyline(m_polyPoints.data(), m_sampleCount, IM_COL32(r, g, b, 235), 0, 1.5f);
            } else if (m_style == OscWaveStyle::Glow && !ch.muted) {
                // Multi-pass phosphor glow
                dl->AddPolyline(m_polyPoints.data(), m_sampleCount, IM_COL32(r, g, b, 35), 0, 5.0f);
                dl->AddPolyline(m_polyPoints.data(), m_sampleCount, IM_COL32(r, g, b, 95), 0, 2.6f);
                int cr = std::min(255, r + 55);
                int cg = std::min(255, g + 55);
                int cb = std::min(255, b + 55);
                dl->AddPolyline(m_polyPoints.data(), m_sampleCount, IM_COL32(cr, cg, cb, 255), 0, 1.2f);
            } else {
                // Crisp Line
                dl->AddPolyline(m_polyPoints.data(), m_sampleCount, IM_COL32(r, g, b, ch.muted ? 120 : 240), 0, 1.6f);
            }

            // Status overlays inside scope
            if (ch.muted) {
                const char* mTxt = "MUTED";
                ImVec2 mSz = ImGui::CalcTextSize(mTxt);
                ImVec2 mp0 = ImVec2(canvasP1.x - mSz.x - 10.0f, canvasP0.y + 3.0f);
                ImVec2 mp1 = ImVec2(canvasP1.x - 3.0f, canvasP0.y + 3.0f + mSz.y + 2.0f);
                dl->AddRectFilled(mp0, mp1, IM_COL32(180, 40, 40, 210), 3.0f);
                dl->AddText(ImVec2(mp0.x + 3.0f, mp0.y + 1.0f), IM_COL32(255, 255, 255, 240), mTxt);
            } else if (ch.solo) {
                const char* sTxt = "SOLO";
                ImVec2 sSz = ImGui::CalcTextSize(sTxt);
                ImVec2 sp0 = ImVec2(canvasP1.x - sSz.x - 10.0f, canvasP0.y + 3.0f);
                ImVec2 sp1 = ImVec2(canvasP1.x - 3.0f, canvasP0.y + 3.0f + sSz.y + 2.0f);
                dl->AddRectFilled(sp0, sp1, IM_COL32(35, 150, 65, 210), 3.0f);
                dl->AddText(ImVec2(sp0.x + 3.0f, sp0.y + 1.0f), IM_COL32(255, 255, 255, 240), sTxt);
            }

            // Channel type badge in bottom-left
            if (!ch.type.empty()) {
                dl->AddText(ImVec2(canvasP0.x + 5.0f, canvasP1.y - 15.0f),
                            IM_COL32(95, 120, 150, 150), ch.type.c_str());
            }

            dl->PopClipRect();

            // Set cursor for table spacing
            ImGui::SetCursorScreenPos(ImVec2(cellP0.x, cellP0.y + cardH + 5.0f));
            ImGui::Dummy(ImVec2(cellW, 0.0f));

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace vgmstudio
