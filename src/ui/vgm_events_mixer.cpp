#include "vgm_events_mixer.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vgmstudio {

static void ParsePan(const char* desc, float& outPan, std::string& outPanText) {
    if (std::strstr(desc, "Pan:") || std::strstr(desc, "Pan =") || std::strstr(desc, "Pan ")) {
        if (std::strstr(desc, "Center") || std::strstr(desc, "Left+Right") || std::strstr(desc, "Both")) {
            outPan = 0.0f;
            outPanText = "Center";
        } else if (std::strstr(desc, "Left") && !std::strstr(desc, "Right")) {
            outPan = -1.0f;
            outPanText = "Left";
        } else if (std::strstr(desc, "Right") && !std::strstr(desc, "Left")) {
            outPan = 1.0f;
            outPanText = "Right";
        } else if (std::strstr(desc, "None") || std::strstr(desc, "Mute")) {
            outPan = 0.0f;
            outPanText = "Mute";
        } else {
            const char* lp = std::strstr(desc, "L:");
            if (lp) {
                int l = 0, r = 0;
                if (std::sscanf(lp, "L:%d R:%d", &l, &r) == 2) {
                    if (l == 0 && r == 0) {
                        outPan = 0.0f;
                        outPanText = "Mute";
                    } else if (l == r) {
                        outPan = 0.0f;
                        outPanText = "Center";
                    } else {
                        outPan = std::clamp((float)(r - l) / (float)std::max(1, l + r), -1.0f, 1.0f);
                        char buf[32];
                        if (outPan < 0.0f) std::snprintf(buf, sizeof(buf), "L %d%%", (int)(-outPan * 100));
                        else std::snprintf(buf, sizeof(buf), "R %d%%", (int)(outPan * 100));
                        outPanText = buf;
                    }
                }
            }
        }
    }
}

VgmEventsMixer::VgmEventsMixer() = default;
VgmEventsMixer::~VgmEventsMixer() = default;

void VgmEventsMixer::Reset() {
    m_channelStates.clear();
    m_lastSample = 0;
    m_lastInstIdx = 0;
}

int VgmEventsMixer::MatchInstructionToChannel(const VgmInstruction& inst, const std::vector<TrackedChannel>& channels) {
    if (channels.empty()) return -1;

    // Filter candidate channels by chip name
    std::vector<int> candidates;
    for (size_t i = 0; i < channels.size(); ++i) {
        const auto& ch = channels[i];
        if (ch.chipName.empty()) continue;

        if (inst.chip[0] != '\0') {
            if (std::strstr(inst.chip, ch.chipName.c_str()) != nullptr ||
                std::strstr(ch.chipName.c_str(), inst.chip) != nullptr) {
                candidates.push_back((int)i);
                continue;
            }
        }
    }

    if (candidates.empty()) {
        for (size_t i = 0; i < channels.size(); ++i) {
            const auto& ch = channels[i];
            if (!ch.chipName.empty() && std::strstr(inst.desc, ch.chipName.c_str()) != nullptr) {
                candidates.push_back((int)i);
            }
        }
    }

    if (candidates.empty()) return -1;
    if (candidates.size() == 1) return candidates[0];

    // Among candidates, match channel number
    int chNum = -1;
    if (std::sscanf(inst.channel, "Ch %d", &chNum) == 1 ||
        std::sscanf(inst.desc, "Ch %d", &chNum) == 1) {
        int targetIdxInChip = chNum - 1;
        for (int idx : candidates) {
            if (channels[idx].channelInChip == targetIdxInChip) {
                return idx;
            }
        }
        if (targetIdxInChip >= 0 && targetIdxInChip < (int)candidates.size()) {
            return candidates[targetIdxInChip];
        }
    }

    // Named channel types
    const char* targets[] = { "Pulse 1", "Pulse 2", "Triangle", "Noise", "DAC", "PCM", "DMC", "SSG 1", "SSG 2", "SSG 3" };
    for (const char* t : targets) {
        if (std::strstr(inst.channel, t) || std::strstr(inst.desc, t)) {
            for (int idx : candidates) {
                if (std::strstr(channels[idx].name.c_str(), t) != nullptr ||
                    std::strstr(channels[idx].type.c_str(), t) != nullptr) {
                    return idx;
                }
            }
        }
    }

    return candidates[0];
}

void VgmEventsMixer::UpdateTelemetry(AudioEngine& audioEngine, const std::vector<TrackedChannel>& channels) {
    if (channels.empty()) {
        m_channelStates.clear();
        return;
    }

    if (m_channelStates.size() != channels.size()) {
        m_channelStates.resize(channels.size());
        m_lastSample = 0;
        m_lastInstIdx = 0;
    }

    const auto& disassembly = audioEngine.GetDisassembly();
    uint32_t curSample = audioEngine.GetCurSample();
    float dt = ImGui::GetIO().DeltaTime;

    // Detect backward seek or song loop
    if (curSample < m_lastSample || m_lastInstIdx == 0) {
        for (auto& s : m_channelStates) {
            s.lastDesc = "Idle";
            s.lastHex.clear();
            s.vgmPanText = "Center";
            s.vgmPan = 0.0f;
            s.eventFlash = 0.0f;
        }

        if (!disassembly.IsEmpty()) {
            int endIdx = disassembly.FindInstructionIndexAtSample(curSample);
            if (endIdx >= 0) {
                std::vector<bool> found(channels.size(), false);
                size_t remaining = channels.size();
                int lookbackLimit = std::max(0, endIdx - 2000);

                for (int i = endIdx; i >= lookbackLimit && remaining > 0; --i) {
                    const auto& inst = disassembly.GetInstructions()[i];
                    int chIdx = MatchInstructionToChannel(inst, channels);
                    if (chIdx >= 0 && chIdx < (int)m_channelStates.size()) {
                        if (!found[chIdx]) {
                            found[chIdx] = true;
                            remaining--;
                            m_channelStates[chIdx].lastDesc = inst.desc;
                            m_channelStates[chIdx].lastHex = inst.hexBytes;
                            m_channelStates[chIdx].lastSample = inst.samplePos;
                            ParsePan(inst.desc, m_channelStates[chIdx].vgmPan, m_channelStates[chIdx].vgmPanText);
                        }
                    }
                }
                m_lastInstIdx = endIdx + 1;
            }
        }
    } else {
        if (!disassembly.IsEmpty()) {
            int endIdx = disassembly.FindInstructionIndexAtSample(curSample);
            if (endIdx >= m_lastInstIdx) {
                size_t totalInst = disassembly.GetCount();
                int limitEnd = std::min(endIdx, (int)totalInst - 1);

                for (int i = m_lastInstIdx; i <= limitEnd; ++i) {
                    const auto& inst = disassembly.GetInstructions()[i];
                    int chIdx = MatchInstructionToChannel(inst, channels);
                    if (chIdx >= 0 && chIdx < (int)m_channelStates.size()) {
                        m_channelStates[chIdx].lastDesc = inst.desc;
                        m_channelStates[chIdx].lastHex = inst.hexBytes;
                        m_channelStates[chIdx].lastSample = inst.samplePos;
                        m_channelStates[chIdx].eventFlash = 1.0f;
                        ParsePan(inst.desc, m_channelStates[chIdx].vgmPan, m_channelStates[chIdx].vgmPanText);
                    }
                }
                m_lastInstIdx = limitEnd + 1;
            }
        }
    }

    m_lastSample = curSample;

    // Smooth meter decay & event flash fade
    for (size_t i = 0; i < channels.size(); ++i) {
        auto& s = m_channelStates[i];
        const auto& ch = channels[i];

        if (s.eventFlash > 0.0f) {
            s.eventFlash = std::max(0.0f, s.eventFlash - dt * 3.5f);
        }

        float target = ch.curVolume;
        if (target >= s.meterLevel) {
            s.meterLevel = target;
        } else {
            s.meterLevel = std::max(0.0f, s.meterLevel - dt * 1.8f);
        }

        if (target >= s.peakHold) {
            s.peakHold = target;
            s.peakTimer = 0.5f;
        } else {
            s.peakTimer -= dt;
            if (s.peakTimer <= 0.0f) {
                s.peakHold = std::max(0.0f, s.peakHold - dt * 1.2f);
            }
        }
    }
}

void VgmEventsMixer::RenderChannelStrip(int chIdx, const TrackedChannel& ch, ChannelLiveEventState& state, float stripW, float stripH) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + stripW, p0.y + stripH);

    bool isHovered = ImGui::IsMouseHoveringRect(p0, p1);

    // Strip background
    ImU32 stripBg = IM_COL32(18, 23, 31, 245);
    ImU32 stripBorder = IM_COL32(32, 42, 58, 255);

    if (state.eventFlash > 0.05f) {
        int flashAlpha = (int)(state.eventFlash * 140.0f);
        stripBorder = IM_COL32(0, 210, 240, flashAlpha);
    } else if (ch.active) {
        stripBorder = IM_COL32(40, 75, 105, 255);
    }

    dl->AddRectFilled(p0, p1, stripBg, 5.0f);
    dl->AddRect(p0, p1, stripBorder, 5.0f, 0, state.eventFlash > 0.05f ? 1.5f : 1.0f);

    float curY = p0.y + 6.0f;

    // 1. Channel Header: Badge, Chip, Channel Name, Type
    {
        ImU32 badgeCol = ch.color ? ch.color : IM_COL32(0, 220, 240, 255);
        float badgeW = 20.0f;
        float badgeH = 16.0f;
        ImVec2 bp0(p0.x + 6.0f, curY);
        ImVec2 bp1(bp0.x + badgeW, bp0.y + badgeH);
        dl->AddRectFilled(bp0, bp1, badgeCol, 3.0f);

        char idBuf[16];
        std::snprintf(idBuf, sizeof(idBuf), "%d", ch.id + 1);
        ImVec2 idSz = ImGui::CalcTextSize(idBuf);
        dl->AddText(ImVec2(bp0.x + (badgeW - idSz.x) * 0.5f, bp0.y + (badgeH - idSz.y) * 0.5f),
                    IM_COL32(10, 15, 20, 255), idBuf);

        // Chip Name
        dl->PushClipRect(ImVec2(bp1.x + 4.0f, curY - 2.0f), ImVec2(p1.x - 4.0f, curY + 16.0f), true);
        dl->AddText(ImVec2(bp1.x + 4.0f, curY), IM_COL32(160, 195, 235, 255), ch.chipName.c_str());
        dl->PopClipRect();

        curY += 18.0f;

        // Channel Name
        dl->PushClipRect(ImVec2(p0.x + 6.0f, curY), ImVec2(p1.x - 6.0f, curY + 16.0f), true);
        dl->AddText(ImVec2(p0.x + 6.0f, curY), IM_COL32(235, 242, 250, 255), ch.name.c_str());
        dl->PopClipRect();

        curY += 17.0f;

        // Type Tag
        if (!ch.type.empty()) {
            ImVec2 tp0(p0.x + 6.0f, curY);
            ImVec2 tp1(tp0.x + stripW - 12.0f, tp0.y + 14.0f);
            dl->AddRectFilled(tp0, tp1, IM_COL32(25, 33, 46, 200), 2.0f);
            ImVec2 tSz = ImGui::CalcTextSize(ch.type.c_str());
            dl->AddText(ImVec2(tp0.x + (tp1.x - tp0.x - tSz.x) * 0.5f, tp0.y + (14.0f - tSz.y) * 0.5f),
                        IM_COL32(120, 150, 185, 220), ch.type.c_str());
        }
        curY += 18.0f;
    }

    // 2. Live Note & Pitch LCD Display
    {
        ImVec2 np0(p0.x + 8.0f, curY);
        ImVec2 np1(p1.x - 8.0f, curY + 34.0f);
        float nW = np1.x - np0.x;

        dl->AddRectFilled(np0, np1, IM_COL32(10, 14, 20, 255), 3.0f);
        dl->AddRect(np0, np1, IM_COL32(28, 38, 52, 255), 3.0f, 0, 1.0f);

        if (ch.active && ch.curNoteName[0] != '-' && ch.curNoteName[0] != '\0') {
            ImVec2 nSz = ImGui::CalcTextSize(ch.curNoteName);
            dl->AddText(ImVec2(np0.x + (nW - nSz.x) * 0.5f, np0.y + 2.0f),
                        IM_COL32(0, 230, 250, 255), ch.curNoteName);

            // Frequency
            float freq = 440.0f * std::pow(2.0f, (ch.curMidiNote - 69.0f) / 12.0f);
            char fBuf[24];
            if (freq >= 1000.0f) std::snprintf(fBuf, sizeof(fBuf), "%.1f kHz", freq / 1000.0f);
            else std::snprintf(fBuf, sizeof(fBuf), "%.1f Hz", freq);

            ImVec2 fSz = ImGui::CalcTextSize(fBuf);
            dl->AddText(ImVec2(np0.x + (nW - fSz.x) * 0.5f, np0.y + 17.0f),
                        IM_COL32(130, 160, 195, 220), fBuf);
        } else {
            const char* dash = "---";
            ImVec2 dSz = ImGui::CalcTextSize(dash);
            dl->AddText(ImVec2(np0.x + (nW - dSz.x) * 0.5f, np0.y + 3.0f),
                        IM_COL32(70, 85, 105, 180), dash);

            const char* hzOff = "-- Hz";
            ImVec2 hSz = ImGui::CalcTextSize(hzOff);
            dl->AddText(ImVec2(np0.x + (nW - hSz.x) * 0.5f, np0.y + 17.0f),
                        IM_COL32(50, 65, 82, 160), hzOff);
        }

        curY += 38.0f;
    }

    // 4. Live VGM Panning Gauge
    {
        const char* panLabel = "PAN";
        ImVec2 plSz = ImGui::CalcTextSize(panLabel);
        dl->AddText(ImVec2(p0.x + (stripW - plSz.x) * 0.5f, curY), IM_COL32(95, 120, 150, 180), panLabel);
        curY += 13.0f;

        // Pan track
        ImVec2 pp0(p0.x + 10.0f, curY);
        ImVec2 pp1(p1.x - 10.0f, curY + 10.0f);
        float pW = pp1.x - pp0.x;
        dl->AddRectFilled(pp0, pp1, IM_COL32(12, 16, 23, 255), 2.0f);
        dl->AddRect(pp0, pp1, IM_COL32(35, 46, 62, 255), 2.0f, 0, 1.0f);

        // Center tick line
        float midX = pp0.x + pW * 0.5f;
        dl->AddLine(ImVec2(midX, pp0.y), ImVec2(midX, pp1.y), IM_COL32(55, 75, 100, 200), 1.0f);

        // Needle indicator
        float needleX = midX + state.vgmPan * (pW * 0.44f);
        ImVec2 np0(needleX - 2.5f, pp0.y - 1.0f);
        ImVec2 np1(needleX + 2.5f, pp1.y + 1.0f);
        dl->AddRectFilled(np0, np1, IM_COL32(0, 220, 240, 255), 1.5f);

        curY += 12.0f;

        // Pan text readout
        ImVec2 ptSz = ImGui::CalcTextSize(state.vgmPanText.c_str());
        dl->AddText(ImVec2(p0.x + (stripW - ptSz.x) * 0.5f, curY),
                    IM_COL32(175, 205, 235, 240), state.vgmPanText.c_str());

        curY += 16.0f;
    }

    // Top section: Header, Note LCD, Pan Gauge (~105px)
    // Bottom section: Numeric volume readout + Last Event Box (~56px)
    float nonFaderH = 162.0f;
    float faderH = std::clamp(stripH - nonFaderH, 45.0f, 160.0f);
    float faderTopY = curY;
    float faderBotY = curY + faderH;

    // 5. Live VGM Volume Fader & Peak LED Meter (Read-Only)
    {
        // Left: Multi-segment LED meter (width 11px)
        float meterX0 = p0.x + 10.0f;
        float meterX1 = meterX0 + 11.0f;
        dl->AddRectFilled(ImVec2(meterX0, faderTopY), ImVec2(meterX1, faderBotY), IM_COL32(10, 14, 20, 255), 2.0f);
        dl->AddRect(ImVec2(meterX0, faderTopY), ImVec2(meterX1, faderBotY), IM_COL32(28, 38, 50, 255), 2.0f, 0, 1.0f);

        // Dynamically scaled LED segments
        const int numSegments = std::clamp((int)(faderH / 8.0f), 6, 16);
        float segSpacing = 1.5f;
        float segH = (faderH - (numSegments + 1) * segSpacing) / numSegments;

        for (int s = 0; s < numSegments; ++s) {
            float segBot = faderBotY - segSpacing - s * (segH + segSpacing);
            float segTop = segBot - segH;
            float segFrac = (float)(s + 1) / (float)numSegments;

            ImU32 litCol;
            ImU32 dimCol;
            if (segFrac > 0.85f) {
                litCol = IM_COL32(255, 50, 50, 255);   // Red
                dimCol = IM_COL32(65, 18, 18, 140);
            } else if (segFrac > 0.65f) {
                litCol = IM_COL32(255, 195, 25, 255);  // Yellow
                dimCol = IM_COL32(65, 50, 15, 140);
            } else {
                litCol = IM_COL32(35, 215, 75, 255);   // Green
                dimCol = IM_COL32(15, 55, 25, 140);
            }

            bool isLit = (state.meterLevel >= segFrac);
            dl->AddRectFilled(ImVec2(meterX0 + 1.5f, segTop), ImVec2(meterX1 - 1.5f, segBot),
                              isLit ? litCol : dimCol, 1.0f);
        }

        // Peak hold line
        if (state.peakHold > 0.02f) {
            float peakY = faderBotY - state.peakHold * faderH;
            dl->AddLine(ImVec2(meterX0 + 1.0f, peakY), ImVec2(meterX1 - 1.0f, peakY),
                        IM_COL32(255, 240, 200, 255), 1.5f);
        }

        // Center: dB scale ticks
        float tickX = meterX1 + 4.0f;
        const struct { float frac; const char* text; } ticks[] = {
            { 1.00f, "0" },
            { 0.75f, "-3" },
            { 0.50f, "-6" },
            { 0.25f, "-12" },
            { 0.00f, "-inf" }
        };
        for (const auto& tk : ticks) {
            float ty = faderBotY - tk.frac * faderH;
            dl->AddLine(ImVec2(tickX, ty), ImVec2(tickX + 4.0f, ty), IM_COL32(50, 65, 85, 180), 1.0f);
        }

        // Right: Fader Track & Knob
        float trackX = p0.x + stripW - 30.0f;
        float trackSlotW = 5.0f;
        ImVec2 slotP0(trackX - trackSlotW * 0.5f, faderTopY);
        ImVec2 slotP1(trackX + trackSlotW * 0.5f, faderBotY);

        dl->AddRectFilled(slotP0, slotP1, IM_COL32(8, 12, 18, 255), 2.0f);
        dl->AddRect(slotP0, slotP1, IM_COL32(28, 38, 52, 255), 2.0f, 0, 1.0f);
        dl->AddLine(ImVec2(trackX, faderTopY), ImVec2(trackX, faderBotY), IM_COL32(20, 28, 40, 255), 1.0f);

        // Fader handle position (driven directly by live VGM volume ch.curVolume)
        float faderVal = std::clamp(ch.curVolume, 0.0f, 1.0f);
        float handleY = faderBotY - faderVal * faderH;
        float capW = 26.0f;
        float capH = 12.0f;
        ImVec2 capP0(trackX - capW * 0.5f, handleY - capH * 0.5f);
        ImVec2 capP1(trackX + capW * 0.5f, handleY + capH * 0.5f);

        // Metallic fader cap styling
        dl->AddRectFilled(capP0, capP1, IM_COL32(55, 68, 85, 255), 2.5f);
        dl->AddRectFilled(ImVec2(capP0.x + 1.0f, capP0.y + 1.0f), ImVec2(capP1.x - 1.0f, capP0.y + capH * 0.5f),
                          IM_COL32(75, 92, 115, 255), 2.0f);
        dl->AddRect(capP0, capP1, IM_COL32(110, 135, 168, 255), 2.5f, 0, 1.0f);
        dl->AddLine(ImVec2(capP0.x + 2.0f, handleY), ImVec2(capP1.x - 2.0f, handleY),
                    IM_COL32(0, 220, 240, 255), 1.2f);

        if (isHovered && ImGui::IsMouseHoveringRect(ImVec2(p0.x + 6.0f, faderTopY), ImVec2(p1.x - 6.0f, faderBotY))) {
            float pct = ch.curVolume * 100.0f;
            float dB = (ch.curVolume > 0.0001f) ? (20.0f * std::log10(ch.curVolume)) : -96.0f;
            ImGui::SetTooltip("VGM Level: %.1f%% (%.1f dB)\nHardware stream telemetry (Read-only)", pct, dB);
        }

        curY = faderBotY + 4.0f;

        // Level numeric readout (% and dB)
        char volBuf[32];
        if (ch.curVolume <= 0.001f) {
            std::snprintf(volBuf, sizeof(volBuf), "0%% (-inf)");
        } else {
            float dB = 20.0f * std::log10(ch.curVolume);
            std::snprintf(volBuf, sizeof(volBuf), "%d%% (%.1fdB)", (int)(ch.curVolume * 100.0f), dB);
        }
        ImVec2 vbSz = ImGui::CalcTextSize(volBuf);
        dl->AddText(ImVec2(p0.x + (stripW - vbSz.x) * 0.5f, curY),
                    IM_COL32(190, 215, 240, 255), volBuf);

        curY += 15.0f;
    }

    // 6. Latest VGM Instruction / Event Box
    {
        ImVec2 ep0(p0.x + 5.0f, curY);
        ImVec2 ep1(p1.x - 5.0f, p1.y - 4.0f);

        ImU32 ebBg = IM_COL32(10, 14, 20, 255);
        ImU32 ebBorder = IM_COL32(30, 42, 58, 255);

        if (state.eventFlash > 0.05f) {
            int a = (int)(state.eventFlash * 220.0f);
            ebBorder = IM_COL32(0, 220, 240, a);
            ebBg = IM_COL32(12, 28, 42, 255);
        }

        dl->AddRectFilled(ep0, ep1, ebBg, 3.0f);
        dl->AddRect(ep0, ep1, ebBorder, 3.0f, 0, state.eventFlash > 0.05f ? 1.5f : 1.0f);

        // Event text
        float textH = ImGui::GetTextLineHeight();
        float boxH = ep1.y - ep0.y;
        float textY = ep0.y + std::max(2.0f, (boxH - textH) * 0.5f);
        dl->PushClipRect(ImVec2(ep0.x + 3.0f, ep0.y + 2.0f), ImVec2(ep1.x - 3.0f, ep1.y - 1.0f), true);
        dl->AddText(ImVec2(ep0.x + 4.0f, textY),
                    state.eventFlash > 0.05f ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 205, 230, 240),
                    state.lastDesc.c_str());
        dl->PopClipRect();

        if (ImGui::IsMouseHoveringRect(ep0, ep1)) {
            ImGui::SetTooltip("Channel %d (%s - %s)\nLast Event: %s\nSample Pos: %u\nHex: %s",
                              ch.id + 1, ch.chipName.c_str(), ch.name.c_str(),
                              state.lastDesc.c_str(), state.lastSample,
                              state.lastHex.empty() ? "--" : state.lastHex.c_str());
        }
    }

    // Set cursor for next item layout
    ImGui::SetCursorScreenPos(ImVec2(p0.x + stripW + 6.0f, p0.y));
}

void VgmEventsMixer::Render(AudioEngine& audioEngine, bool* pOpen) {
    if (m_focusRequested) {
        m_focusRequested = false;
        ImGui::SetNextWindowFocus();
        ImGuiWindow* win = ImGui::FindWindowByName("VGM Events Mixer");
        if (win) {
            ImGui::FocusWindow(win);
            if (win->DockNode) {
                win->DockNode->SelectedTabId = win->TabId;
                if (win->DockNode->TabBar) {
                    win->DockNode->TabBar->SelectedTabId = win->TabId;
                    win->DockNode->TabBar->NextSelectedTabId = win->TabId;
                }
            }
        }
    }

    ImGui::SetNextWindowSize(ImVec2(920, 480), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("VGM Events Mixer", pOpen)) {
        ImGui::End();
        return;
    }

    auto& channels = audioEngine.GetNoteTracker().GetChannels();

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
        const char* desc = "Load a VGM or VGZ file to monitor live hardware channel mixer events in real time.";
        float descW = ImGui::CalcTextSize(desc).x;
        ImGui::SetCursorPosX(std::max(0.0f, (avail.x - descW) * 0.5f));
        ImGui::TextDisabled("%s", desc);

        ImGui::End();
        return;
    }

    // Update live instruction and volume telemetry
    UpdateTelemetry(audioEngine, channels);

    // Top Controls Toolbar
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

    // Sound Chips selector filter (Dropdown with checkboxes)
    std::vector<std::string> uniqueChips;
    for (const auto& ch : channels) {
        if (!ch.chipName.empty() && 
            std::find(uniqueChips.begin(), uniqueChips.end(), ch.chipName) == uniqueChips.end()) {
            uniqueChips.push_back(ch.chipName);
            if (m_chipFilter.find(ch.chipName) == m_chipFilter.end()) {
                m_chipFilter[ch.chipName] = true;
            }
        }
    }

    size_t selectedCount = 0;
    std::string singleSelectedName;
    for (const auto& chip : uniqueChips) {
        if (m_chipFilter[chip]) {
            selectedCount++;
            singleSelectedName = chip;
        }
    }

    std::string chipPreview;
    if (uniqueChips.empty()) {
        chipPreview = "None";
    } else if (selectedCount == uniqueChips.size()) {
        chipPreview = "All Chips";
    } else if (selectedCount == 0) {
        chipPreview = "None";
    } else if (selectedCount == 1) {
        chipPreview = singleSelectedName;
    } else {
        chipPreview = std::to_string(selectedCount) + " of " + std::to_string(uniqueChips.size()) + " Chips";
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Chips:");
    ImGui::SameLine(0, 4);
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::BeginCombo("##MixerChipFilter", chipPreview.c_str())) {
        if (ImGui::Button("All", ImVec2(55, 0))) {
            for (const auto& chip : uniqueChips) {
                m_chipFilter[chip] = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("None", ImVec2(55, 0))) {
            for (const auto& chip : uniqueChips) {
                m_chipFilter[chip] = false;
            }
        }
        ImGui::Separator();
        for (const auto& chip : uniqueChips) {
            int chipChCount = 0;
            for (const auto& ch : channels) {
                if (ch.chipName == chip) chipChCount++;
            }
            char label[64];
            std::snprintf(label, sizeof(label), "%s (%d ch)", chip.c_str(), chipChCount);
            bool isChecked = m_chipFilter[chip];
            if (ImGui::Checkbox(label, &isChecked)) {
                m_chipFilter[chip] = isChecked;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Filter sound chips to display in the events mixer");

    // Active Only toggle
    ImGui::SameLine();
    ImGui::Checkbox("Active Only", &m_showActiveOnly);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only show channels currently playing active notes or events");

    // Telemetry description
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("|");

    ImGui::SameLine();
    int activeCount = 0;
    for (const auto& ch : channels) {
        if (ch.active) activeCount++;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 1.0f), "Channels: %d (%d active)", (int)channels.size(), activeCount);

    // Read-only indicator pill on right
    const char* roLabel = "LIVE TELEMETRY (READ-ONLY)";
    float roW = ImGui::CalcTextSize(roLabel).x + 16.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    if (availW > roW + 8.0f) {
        ImGui::SameLine(0, availW - roW - 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.22f, 0.32f, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.75f, 1.0f, 1.0f));
        ImGui::SmallButton(roLabel);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("This window visualizes live VGM hardware register events.\nFaders and pan gauges directly reflect song stream events.");
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::PopStyleVar(2);
    ImGui::Separator();
    ImGui::Spacing();

    // Filter visible channel indices
    std::vector<size_t> visibleIndices;
    visibleIndices.reserve(channels.size());
    for (size_t i = 0; i < channels.size(); ++i) {
        if (m_showActiveOnly && !channels[i].active) continue;
        auto it = m_chipFilter.find(channels[i].chipName);
        if (it != m_chipFilter.end() && !it->second) continue;
        visibleIndices.push_back(i);
    }

    if (visibleIndices.empty()) {
        ImGui::TextDisabled("No channels match the current filter. Enable chips in the Chips filter dropdown.");
        ImGui::End();
        return;
    }

    // Scrollable Console Strip Area
    ImGuiWindowFlags childFlags = ImGuiWindowFlags_HorizontalScrollbar;
    ImGui::BeginChild("MixerStripsScrollArea", ImVec2(0, 0), false, childFlags);

    const float stripW = 106.0f;
    float availH = ImGui::GetContentRegionAvail().y;
    const float stripH = std::max(235.0f, availH - 4.0f);

    ImVec2 startCursor = ImGui::GetCursorScreenPos();
    for (size_t vi = 0; vi < visibleIndices.size(); ++vi) {
        size_t chIdx = visibleIndices[vi];
        ImGui::PushID((int)chIdx);

        RenderChannelStrip((int)chIdx, channels[chIdx], m_channelStates[chIdx], stripW, stripH);

        ImGui::PopID();
    }

    // Allocate bounding dummy so horizontal scrollbar knows content width
    float totalContentW = visibleIndices.size() * (stripW + 6.0f);
    ImGui::SetCursorScreenPos(startCursor);
    ImGui::Dummy(ImVec2(totalContentW, stripH));

    ImGui::EndChild();
    ImGui::End();
}

} // namespace vgmstudio
