#include "vgm_patch_viewer.hpp"
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <sstream>
#include <filesystem>

namespace vgmstudio {

static const char* NOTE_NAMES[12] = {
    "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
};

static bool IsOpCarrier(int alg, int opIdx) {
    switch (alg) {
        case 0: return (opIdx == 3);
        case 1: return (opIdx == 3);
        case 2: return (opIdx == 3);
        case 3: return (opIdx == 3);
        case 4: return (opIdx == 1 || opIdx == 3);
        case 5: return (opIdx == 1 || opIdx == 2 || opIdx == 3);
        case 6: return (opIdx == 1 || opIdx == 2 || opIdx == 3);
        case 7: return true;
        default: return (opIdx == 3);
    }
}

VgmPatchViewer::VgmPatchViewer() {
    Reset();
}

VgmPatchViewer::~VgmPatchViewer() = default;

void VgmPatchViewer::Reset() {
    m_extractor.Clear();
    m_hasScanned = false;
    m_selectedPresetIdx = 0;
    m_chipFilterIdx = 0;
    m_channelFilterIdx = 0;
    m_typeFilterIdx = 0;
    m_sortModeIdx = 0;
    m_searchFilter[0] = '\0';
    m_activeTestKey = -1;
    m_mouseTesting = false;
    m_lastTestedMidi = -1;
    m_exportStatusMessage.clear();
    m_exportStatusTimer = 0.0f;
}

void VgmPatchViewer::ScanSong(AudioEngine& audioEngine) {
    if (audioEngine.IsLoaded() && audioEngine.GetVgmData() && audioEngine.GetVgmDataSize() > 0) {
        m_extractor.ExtractFromVgm(audioEngine.GetVgmData(), audioEngine.GetVgmDataSize());
        m_hasScanned = true;
        m_selectedPresetIdx = -1;
        m_channelFilterIdx = 0;
    } else {
        Reset();
    }
}

void VgmPatchViewer::SetChannelFilter(const std::string& chName) {
    const auto& allChs = m_extractor.GetAllChannels();
    for (size_t i = 0; i < allChs.size(); ++i) {
        if (allChs[i] == chName) {
            m_channelFilterIdx = (int)i + 1;
            return;
        }
    }
}

void VgmPatchViewer::TriggerAuditionNote(AudioEngine& audioEngine, const VgmPreset& preset, float freq) {
    if (preset.type == PresetType::FM_4OP || preset.type == PresetType::FM_2OP) {
        audioEngine.SetPreviewFmPatch(preset.fm, preset.chipName);
        audioEngine.StartPreview(freq, 0, 0.0f, m_testVolume);
    } else if (preset.type == PresetType::SQUARE) {
        audioEngine.StartPreview(freq, 1, preset.square.dutyCycle, m_testVolume);
    } else if (preset.type == PresetType::TRIANGLE) {
        audioEngine.StartPreview(freq, 2, 0.0f, m_testVolume);
    } else if (preset.type == PresetType::WAVETABLE) {
        audioEngine.SetPreviewWavetable(preset.wavetable.samples);
        audioEngine.StartPreview(freq, 3, 0.0f, m_testVolume);
    } else if (preset.type == PresetType::NOISE) {
        audioEngine.StartPreview(freq, 4, (float)preset.noise.mode, m_testVolume);
    }
}

void VgmPatchViewer::StopAuditionNote(AudioEngine& audioEngine) {
    audioEngine.StopPreview();
}

void VgmPatchViewer::RenderAlgorithmDiagram(int algorithm, int feedback, const FmOperatorPreset ops[4]) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float totalW = ImGui::GetContentRegionAvail().x;
    float totalH = 110.0f;
    ImVec2 p1(p0.x + totalW, p0.y + totalH);

    dl->AddRectFilled(p0, p1, IM_COL32(14, 18, 26, 255), 5.0f);
    dl->AddRect(p0, p1, IM_COL32(32, 44, 60, 255), 5.0f, 0, 1.0f);

    char algTitle[64];
    std::snprintf(algTitle, sizeof(algTitle), "ALGORITHM %d  [Feedback: %d]", algorithm, feedback);
    dl->AddText(ImVec2(p0.x + 10.0f, p0.y + 6.0f), IM_COL32(0, 220, 240, 255), algTitle);

    float boxW = 50.0f;
    float boxH = 36.0f;
    ImVec2 opPos[4];
    float midX = p0.x + totalW * 0.42f;

    switch (algorithm) {
        case 0:
            opPos[0] = ImVec2(midX - 150.0f, p0.y + 40.0f);
            opPos[1] = ImVec2(midX - 65.0f,  p0.y + 40.0f);
            opPos[2] = ImVec2(midX + 20.0f,  p0.y + 40.0f);
            opPos[3] = ImVec2(midX + 105.0f, p0.y + 40.0f);
            break;
        case 1:
            opPos[0] = ImVec2(midX - 130.0f, p0.y + 24.0f);
            opPos[1] = ImVec2(midX - 130.0f, p0.y + 66.0f);
            opPos[2] = ImVec2(midX - 20.0f,  p0.y + 45.0f);
            opPos[3] = ImVec2(midX + 85.0f,  p0.y + 45.0f);
            break;
        case 2:
            opPos[0] = ImVec2(midX - 130.0f, p0.y + 24.0f);
            opPos[1] = ImVec2(midX - 30.0f,  p0.y + 24.0f);
            opPos[2] = ImVec2(midX - 30.0f,  p0.y + 66.0f);
            opPos[3] = ImVec2(midX + 75.0f,  p0.y + 45.0f);
            break;
        case 3:
            opPos[0] = ImVec2(midX - 130.0f, p0.y + 45.0f);
            opPos[1] = ImVec2(midX - 30.0f,  p0.y + 24.0f);
            opPos[2] = ImVec2(midX - 30.0f,  p0.y + 66.0f);
            opPos[3] = ImVec2(midX + 75.0f,  p0.y + 45.0f);
            break;
        case 4:
            opPos[0] = ImVec2(midX - 90.0f, p0.y + 24.0f);
            opPos[1] = ImVec2(midX + 15.0f, p0.y + 24.0f);
            opPos[2] = ImVec2(midX - 90.0f, p0.y + 66.0f);
            opPos[3] = ImVec2(midX + 15.0f, p0.y + 66.0f);
            break;
        case 5:
            opPos[0] = ImVec2(midX - 110.0f, p0.y + 45.0f);
            opPos[1] = ImVec2(midX + 15.0f,  p0.y + 18.0f);
            opPos[2] = ImVec2(midX + 15.0f,  p0.y + 48.0f);
            opPos[3] = ImVec2(midX + 15.0f,  p0.y + 78.0f);
            break;
        case 6:
            opPos[0] = ImVec2(midX - 90.0f, p0.y + 24.0f);
            opPos[1] = ImVec2(midX + 0.0f,  p0.y + 24.0f);
            opPos[2] = ImVec2(midX + 75.0f, p0.y + 24.0f);
            opPos[3] = ImVec2(midX + 75.0f, p0.y + 68.0f);
            break;
        case 7:
        default:
            opPos[0] = ImVec2(midX - 120.0f, p0.y + 45.0f);
            opPos[1] = ImVec2(midX - 40.0f,  p0.y + 45.0f);
            opPos[2] = ImVec2(midX + 40.0f,  p0.y + 45.0f);
            opPos[3] = ImVec2(midX + 120.0f, p0.y + 45.0f);
            break;
    }

    ImVec2 outNodePos(p1.x - 44.0f, p0.y + 45.0f);

    auto DrawWire = [&](ImVec2 from, ImVec2 to, ImU32 col) {
        dl->AddLine(from, to, col, 2.0f);
        ImVec2 dir(to.x - from.x, to.y - from.y);
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 8.0f) {
            dir.x /= len; dir.y /= len;
            ImVec2 n(-dir.y, dir.x);
            ImVec2 tip = to;
            ImVec2 b1(tip.x - dir.x * 5.0f + n.x * 3.5f, tip.y - dir.y * 5.0f + n.y * 3.5f);
            ImVec2 b2(tip.x - dir.x * 5.0f - n.x * 3.5f, tip.y - dir.y * 5.0f - n.y * 3.5f);
            dl->AddTriangleFilled(tip, b1, b2, col);
        }
    };

    ImU32 wireCol = IM_COL32(70, 95, 125, 220);
    ImU32 carrierCol = IM_COL32(230, 160, 40, 230);

    for (int k = 0; k < 4; ++k) {
        if (IsOpCarrier(algorithm, k)) {
            DrawWire(ImVec2(opPos[k].x + boxW, opPos[k].y + boxH * 0.5f),
                     ImVec2(outNodePos.x, opPos[k].y + boxH * 0.5f), carrierCol);
        }
    }

    // Connect modulation wires based on algorithm
    if (algorithm == 0) {
        DrawWire(ImVec2(opPos[0].x + boxW, opPos[0].y + boxH * 0.5f), ImVec2(opPos[1].x, opPos[1].y + boxH * 0.5f), wireCol);
        DrawWire(ImVec2(opPos[1].x + boxW, opPos[1].y + boxH * 0.5f), ImVec2(opPos[2].x, opPos[2].y + boxH * 0.5f), wireCol);
        DrawWire(ImVec2(opPos[2].x + boxW, opPos[2].y + boxH * 0.5f), ImVec2(opPos[3].x, opPos[3].y + boxH * 0.5f), wireCol);
    } else if (algorithm == 1) {
        DrawWire(ImVec2(opPos[0].x + boxW, opPos[0].y + boxH * 0.5f), ImVec2(opPos[2].x, opPos[2].y + 10.0f), wireCol);
        DrawWire(ImVec2(opPos[1].x + boxW, opPos[1].y + boxH * 0.5f), ImVec2(opPos[2].x, opPos[2].y + boxH - 10.0f), wireCol);
        DrawWire(ImVec2(opPos[2].x + boxW, opPos[2].y + boxH * 0.5f), ImVec2(opPos[3].x, opPos[3].y + boxH * 0.5f), wireCol);
    } else if (algorithm == 2) {
        DrawWire(ImVec2(opPos[0].x + boxW, opPos[0].y + boxH * 0.5f), ImVec2(opPos[1].x, opPos[1].y + boxH * 0.5f), wireCol);
        DrawWire(ImVec2(opPos[1].x + boxW, opPos[1].y + boxH * 0.5f), ImVec2(opPos[3].x, opPos[3].y + 10.0f), wireCol);
        DrawWire(ImVec2(opPos[2].x + boxW, opPos[2].y + boxH * 0.5f), ImVec2(opPos[3].x, opPos[3].y + boxH - 10.0f), wireCol);
    } else if (algorithm == 3) {
        DrawWire(ImVec2(opPos[0].x + boxW, opPos[0].y + boxH * 0.5f), ImVec2(opPos[1].x, opPos[1].y + boxH * 0.5f), wireCol);
        DrawWire(ImVec2(opPos[0].x + boxW, opPos[0].y + boxH * 0.5f), ImVec2(opPos[2].x, opPos[2].y + boxH * 0.5f), wireCol);
        DrawWire(ImVec2(opPos[1].x + boxW, opPos[1].y + boxH * 0.5f), ImVec2(opPos[3].x, opPos[3].y + 10.0f), wireCol);
        DrawWire(ImVec2(opPos[2].x + boxW, opPos[2].y + boxH * 0.5f), ImVec2(opPos[3].x, opPos[3].y + boxH - 10.0f), wireCol);
    } else if (algorithm == 4) {
        DrawWire(ImVec2(opPos[0].x + boxW, opPos[0].y + boxH * 0.5f), ImVec2(opPos[1].x, opPos[1].y + boxH * 0.5f), wireCol);
        DrawWire(ImVec2(opPos[2].x + boxW, opPos[2].y + boxH * 0.5f), ImVec2(opPos[3].x, opPos[3].y + boxH * 0.5f), wireCol);
    } else if (algorithm == 5) {
        DrawWire(ImVec2(opPos[0].x + boxW, opPos[0].y + boxH * 0.5f), ImVec2(opPos[1].x, opPos[1].y + boxH * 0.5f), wireCol);
        DrawWire(ImVec2(opPos[0].x + boxW, opPos[0].y + boxH * 0.5f), ImVec2(opPos[2].x, opPos[2].y + boxH * 0.5f), wireCol);
        DrawWire(ImVec2(opPos[0].x + boxW, opPos[0].y + boxH * 0.5f), ImVec2(opPos[3].x, opPos[3].y + boxH * 0.5f), wireCol);
    } else if (algorithm == 6) {
        DrawWire(ImVec2(opPos[0].x + boxW, opPos[0].y + boxH * 0.5f), ImVec2(opPos[1].x, opPos[1].y + boxH * 0.5f), wireCol);
    }

    // Feedback loop on OP 1
    if (feedback > 0) {
        ImVec2 fbStart(opPos[0].x + boxW * 0.5f, opPos[0].y);
        ImVec2 fbUp(fbStart.x, fbStart.y - 10.0f);
        ImVec2 fbLeft(opPos[0].x - 10.0f, fbUp.y);
        ImVec2 fbTarget(opPos[0].x, opPos[0].y + boxH * 0.5f);
        dl->AddLine(fbStart, fbUp, IM_COL32(0, 240, 200, 220), 1.5f);
        dl->AddLine(fbUp, fbLeft, IM_COL32(0, 240, 200, 220), 1.5f);
        dl->AddLine(fbLeft, ImVec2(fbLeft.x, fbTarget.y), IM_COL32(0, 240, 200, 220), 1.5f);
        DrawWire(ImVec2(fbLeft.x, fbTarget.y), fbTarget, IM_COL32(0, 240, 200, 220));
    }

    // Out node
    ImVec2 on0(outNodePos.x - 4.0f, p0.y + 26.0f);
    ImVec2 on1(p1.x - 8.0f, p0.y + 92.0f);
    dl->AddRectFilled(on0, on1, IM_COL32(20, 28, 40, 255), 4.0f);
    dl->AddRect(on0, on1, IM_COL32(245, 175, 40, 200), 4.0f, 0, 1.5f);
    const char* outTxt = "OUT";
    ImVec2 oSz = ImGui::CalcTextSize(outTxt);
    dl->AddText(ImVec2(on0.x + (on1.x - on0.x - oSz.x) * 0.5f, on0.y + (on1.y - on0.y - oSz.y) * 0.5f),
                IM_COL32(255, 200, 80, 255), outTxt);

    // Operator Boxes
    for (int k = 0; k < 4; ++k) {
        ImVec2 bp0 = opPos[k];
        ImVec2 bp1(bp0.x + boxW, bp0.y + boxH);
        bool isCarrier = IsOpCarrier(algorithm, k);

        ImU32 boxBg = isCarrier ? IM_COL32(38, 30, 16, 255) : IM_COL32(16, 28, 42, 255);
        ImU32 boxBorder = isCarrier ? IM_COL32(235, 170, 45, 255) : IM_COL32(0, 195, 235, 255);

        dl->AddRectFilled(bp0, bp1, boxBg, 4.0f);
        dl->AddRect(bp0, bp1, boxBorder, 4.0f, 0, 1.5f);

        char opName[16];
        std::snprintf(opName, sizeof(opName), "OP %d", k + 1);
        ImVec2 onSz = ImGui::CalcTextSize(opName);
        dl->AddText(ImVec2(bp0.x + (boxW - onSz.x) * 0.5f, bp0.y + 2.0f),
                    isCarrier ? IM_COL32(255, 215, 100, 255) : IM_COL32(160, 235, 255, 255), opName);

        char sub[24];
        std::snprintf(sub, sizeof(sub), "%dx|%d", ops[k].mult, ops[k].tl);
        ImVec2 sbSz = ImGui::CalcTextSize(sub);
        dl->AddText(ImVec2(bp0.x + (boxW - sbSz.x) * 0.5f, bp0.y + 14.0f),
                    IM_COL32(180, 200, 220, 230), sub);

        const char* role = isCarrier ? "CARR" : "MOD";
        ImVec2 rSz = ImGui::CalcTextSize(role);
        dl->AddText(ImVec2(bp0.x + (boxW - rSz.x) * 0.5f, bp0.y + 24.0f),
                    isCarrier ? IM_COL32(240, 140, 30, 220) : IM_COL32(30, 160, 220, 220), role);
    }

    ImGui::Dummy(ImVec2(totalW, totalH + 4.0f));
}

void VgmPatchViewer::RenderEnvelopeCurve(const FmOperatorPreset& op, ImVec2 size, bool isCarrier) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size.x, p0.y + size.y);

    dl->AddRectFilled(p0, p1, IM_COL32(10, 14, 20, 255), 3.0f);
    dl->AddRect(p0, p1, IM_COL32(28, 38, 52, 255), 3.0f, 0, 1.0f);

    float peakNorm = std::clamp(1.0f - ((float)op.tl / 127.0f), 0.05f, 1.0f);
    float susNorm = peakNorm * std::clamp(1.0f - ((float)op.sl / 15.0f), 0.0f, 1.0f);

    float padX = 4.0f, padY = 4.0f;
    float graphW = size.x - padX * 2.0f;
    float graphH = size.y - padY * 2.0f;
    float baseY = p1.y - padY;

    float aFrac = std::clamp((32.0f - (float)op.ar) / 32.0f * 0.35f + 0.05f, 0.04f, 0.35f);
    float dFrac = std::clamp((32.0f - (float)op.dr) / 32.0f * 0.35f + 0.05f, 0.05f, 0.35f);
    float rFrac = std::clamp((16.0f - (float)op.rr) / 16.0f * 0.35f + 0.05f, 0.05f, 0.35f);
    float sFrac = std::max(0.05f, 1.0f - (aFrac + dFrac + rFrac));

    float x0 = p0.x + padX;
    float x1 = x0 + graphW * aFrac;
    float x2 = x1 + graphW * dFrac;
    float x3 = x2 + graphW * sFrac;
    float x4 = p1.x - padX;

    float y0 = baseY;
    float y1 = baseY - graphH * peakNorm;
    float y2 = baseY - graphH * susNorm;
    float y3 = y2;
    float y4 = baseY;

    ImVec2 poly[6] = {
        ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x2, y2), ImVec2(x3, y3), ImVec2(x4, y4), ImVec2(x0, y0)
    };

    ImU32 fillCol = isCarrier ? IM_COL32(230, 150, 30, 50) : IM_COL32(0, 180, 220, 50);
    ImU32 lineCol = isCarrier ? IM_COL32(250, 185, 45, 255) : IM_COL32(0, 220, 245, 255);

    dl->AddConvexPolyFilled(poly, 5, fillCol);
    dl->AddLine(poly[0], poly[1], lineCol, 2.0f);
    dl->AddLine(poly[1], poly[2], lineCol, 2.0f);
    dl->AddLine(poly[2], poly[3], lineCol, 2.0f);
    dl->AddLine(poly[3], poly[4], lineCol, 2.0f);

    dl->AddCircleFilled(poly[1], 2.5f, IM_COL32(255, 255, 255, 255));
    dl->AddCircleFilled(poly[2], 2.5f, IM_COL32(255, 255, 255, 255));
    dl->AddCircleFilled(poly[3], 2.5f, IM_COL32(255, 255, 255, 255));

    ImGui::Dummy(size);
}

void VgmPatchViewer::RenderOperatorCard(int opIdx, const FmOperatorPreset& op, bool isCarrier, float cardW) {
    ImGui::PushID(opIdx);
    ImGui::BeginChild("OpCard", ImVec2(cardW, 250.0f), true, ImGuiWindowFlags_NoScrollbar);

    ImVec4 badgeCol = isCarrier ? ImVec4(1.0f, 0.75f, 0.2f, 1.0f) : ImVec4(0.0f, 0.85f, 1.0f, 1.0f);
    ImGui::TextColored(badgeCol, "OP %d (%s)", opIdx + 1, isCarrier ? "Carrier" : "Modulator");
    ImGui::Separator();

    RenderEnvelopeCurve(op, ImVec2(cardW - 16.0f, 40.0f), isCarrier);
    ImGui::Spacing();

    ImGui::Text("TL (Level):   %d", op.tl);
    ImGui::Text("MULT:         %dx", op.mult);
    ImGui::Text("AR (Attack):  %d", op.ar);
    ImGui::Text("DR (Decay 1): %d", op.dr);
    ImGui::Text("SR (Decay 2): %d", op.sr);
    ImGui::Text("SL (Sustain): %d", op.sl);
    ImGui::Text("RR (Release): %d", op.rr);
    ImGui::Text("DT (Detune):  %+d", op.dt);
    ImGui::Text("RS / SSG:     %d / %d", op.rs, op.ssg);
    if (op.am) {
        ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.9f, 1.0f), "AM Tremolo: ON");
    }

    ImGui::EndChild();
    ImGui::PopID();
}

void VgmPatchViewer::RenderFmDetails(const FmPresetData& fm) {
    RenderAlgorithmDiagram(fm.algorithm, fm.feedback, fm.ops);
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.95f, 1.0f), "Global Modulation: PMS=%d, AMS=%d, Feedback=%d",
                       fm.pms, fm.ams, fm.feedback);
    ImGui::Spacing();

    float availW = ImGui::GetContentRegionAvail().x;
    float cardW = std::max(130.0f, (availW - 30.0f) / 4.0f);
    for (int opIdx = 0; opIdx < 4; ++opIdx) {
        if (opIdx > 0) ImGui::SameLine(0.0f, 8.0f);
        bool isCarrier = IsOpCarrier(fm.algorithm, opIdx);
        RenderOperatorCard(opIdx, fm.ops[opIdx], isCarrier, cardW);
    }
}

void VgmPatchViewer::RenderSquareDetails(const SquarePresetData& sq) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float totalW = ImGui::GetContentRegionAvail().x;
    float totalH = 140.0f;
    ImVec2 p1(p0.x + totalW, p0.y + totalH);

    dl->AddRectFilled(p0, p1, IM_COL32(14, 18, 26, 255), 5.0f);
    dl->AddRect(p0, p1, IM_COL32(32, 44, 60, 255), 5.0f, 0, 1.0f);

    char title[64];
    std::snprintf(title, sizeof(title), "SQUARE / PULSE WAVE (Duty: %.1f%%)", sq.dutyCycle * 100.0f);
    dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 8.0f), IM_COL32(0, 220, 240, 255), title);

    // Draw Pulse Waveform representation
    float graphW = totalW - 40.0f;
    float graphH = 60.0f;
    float gY = p0.y + 40.0f;
    float gX = p0.x + 20.0f;

    // Draw center baseline
    dl->AddLine(ImVec2(gX, gY + graphH * 0.5f), ImVec2(gX + graphW, gY + graphH * 0.5f), IM_COL32(40, 50, 70, 200), 1.0f);

    // Draw 3 cycles of pulse wave
    float cycleW = graphW / 3.0f;
    for (int c = 0; c < 3; ++c) {
        float cX = gX + c * cycleW;
        float highW = cycleW * sq.dutyCycle;
        float lowW = cycleW - highW;

        ImVec2 ptHigh0(cX, gY);
        ImVec2 ptHigh1(cX + highW, gY);
        ImVec2 ptLow0(cX + highW, gY + graphH);
        ImVec2 ptLow1(cX + cycleW, gY + graphH);

        // Lines
        dl->AddLine(ptHigh0, ptHigh1, IM_COL32(245, 175, 45, 255), 2.5f);
        dl->AddLine(ptHigh1, ptLow0, IM_COL32(245, 175, 45, 255), 2.5f);
        dl->AddLine(ptLow0, ptLow1, IM_COL32(245, 175, 45, 255), 2.5f);
        if (c < 2) {
            dl->AddLine(ptLow1, ImVec2(cX + cycleW, gY), IM_COL32(245, 175, 45, 255), 2.5f);
        }
    }

    ImGui::Dummy(ImVec2(totalW, totalH + 4.0f));
    ImGui::Spacing();

    ImGui::BeginChild("SquareParams", ImVec2(totalW, 110.0f), true);
    ImGui::Columns(3, "sq_cols", false);
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.95f, 1.0f), "Duty & Waveform");
    ImGui::Text("Duty Ratio: %.1f%%", sq.dutyCycle * 100.0f);
    ImGui::Text("Duty Mode:  %d/3", sq.dutyIndex);

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.95f, 1.0f), "Envelope / Volume");
    ImGui::Text("Volume:     %d / 15", sq.volume);
    ImGui::Text("Mode:       %s", sq.constantVol ? "Constant Vol" : "Decay Envelope");
    ImGui::Text("Env Loop:   %s", sq.envLoop ? "Looping" : "One-Shot");

    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.95f, 1.0f), "Hardware Sweep");
    ImGui::Text("Sweep:      %s", sq.sweepEnabled ? "ENABLED" : "Disabled");
    if (sq.sweepEnabled) {
        ImGui::Text("Period:     %d", sq.sweepPeriod);
        ImGui::Text("Direction:  %s", sq.sweepNegate ? "Sweeping DOWN" : "Sweeping UP");
        ImGui::Text("Shift:      %d", sq.sweepShift);
    }
    ImGui::Columns(1);
    ImGui::EndChild();
}

void VgmPatchViewer::RenderTriangleDetails(const TrianglePresetData& tr) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float totalW = ImGui::GetContentRegionAvail().x;
    float totalH = 140.0f;
    ImVec2 p1(p0.x + totalW, p0.y + totalH);

    dl->AddRectFilled(p0, p1, IM_COL32(14, 18, 26, 255), 5.0f);
    dl->AddRect(p0, p1, IM_COL32(32, 44, 60, 255), 5.0f, 0, 1.0f);

    dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 8.0f), IM_COL32(0, 220, 240, 255), "TRIANGLE WAVE GENERATOR");

    float graphW = totalW - 40.0f;
    float graphH = 60.0f;
    float gY = p0.y + 40.0f;
    float gX = p0.x + 20.0f;

    // Draw 3 cycles of triangle wave
    float cycleW = graphW / 3.0f;
    for (int c = 0; c < 3; ++c) {
        float cX = gX + c * cycleW;
        ImVec2 pA(cX, gY + graphH * 0.5f);
        ImVec2 pB(cX + cycleW * 0.25f, gY);
        ImVec2 pC(cX + cycleW * 0.75f, gY + graphH);
        ImVec2 pD(cX + cycleW, gY + graphH * 0.5f);

        dl->AddLine(pA, pB, IM_COL32(40, 220, 120, 255), 2.5f);
        dl->AddLine(pB, pC, IM_COL32(40, 220, 120, 255), 2.5f);
        dl->AddLine(pC, pD, IM_COL32(40, 220, 120, 255), 2.5f);
    }

    ImGui::Dummy(ImVec2(totalW, totalH + 4.0f));
    ImGui::Spacing();

    ImGui::BeginChild("TriParams", ImVec2(totalW, 80.0f), true);
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.95f, 1.0f), "Linear Counter & Length Parameters");
    ImGui::Text("Linear Counter Reload: %d / 127", tr.linearCounter);
    ImGui::Text("Length Counter Halt:   %s", tr.lengthHalt ? "YES (Continuous)" : "No");
    ImGui::EndChild();
}

void VgmPatchViewer::RenderNoiseDetails(const NoisePresetData& ns) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float totalW = ImGui::GetContentRegionAvail().x;
    float totalH = 140.0f;
    ImVec2 p1(p0.x + totalW, p0.y + totalH);

    dl->AddRectFilled(p0, p1, IM_COL32(14, 18, 26, 255), 5.0f);
    dl->AddRect(p0, p1, IM_COL32(32, 44, 60, 255), 5.0f, 0, 1.0f);

    const char* nModeStr = ns.mode ? "METALLIC NOISE (93-bit Loop LFSR)" : "WHITE NOISE (32767-bit LFSR)";
    dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 8.0f), IM_COL32(0, 220, 240, 255), nModeStr);

    float graphW = totalW - 40.0f;
    float graphH = 50.0f;
    float gY = p0.y + 45.0f;
    float gX = p0.x + 20.0f;

    // Simulate noise pattern
    uint16_t lfsr = 0x7FFF;
    int steps = (int)(graphW / 4.0f);
    float lastX = gX;
    float lastY = gY + graphH * 0.5f;

    for (int s = 0; s < steps; ++s) {
        lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & (ns.mode ? 0x6000 : 0x4000));
        float nVal = (lfsr & 1) ? 0.0f : 1.0f;
        float curX = gX + (float)s * 4.0f;
        float curY = gY + nVal * graphH;
        dl->AddLine(ImVec2(lastX, lastY), ImVec2(curX, curY), IM_COL32(230, 80, 160, 220), 1.5f);
        lastX = curX;
        lastY = curY;
    }

    ImGui::Dummy(ImVec2(totalW, totalH + 4.0f));
    ImGui::Spacing();

    ImGui::BeginChild("NoiseParams", ImVec2(totalW, 80.0f), true);
    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.95f, 1.0f), "LFSR Generator Mode & Attenuation");
    ImGui::Text("Mode:     %s", ns.mode ? "Short Periodic (Tonal / Metallic)" : "Long Pseudo-Random (White Noise)");
    ImGui::Text("Volume:   %d / 15", ns.volume);
    ImGui::Text("Period:   %d", ns.period);
    ImGui::EndChild();
}

void VgmPatchViewer::RenderWavetableDetails(const WavetablePresetData& wt) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float totalW = ImGui::GetContentRegionAvail().x;
    float totalH = 150.0f;
    ImVec2 p1(p0.x + totalW, p0.y + totalH);

    dl->AddRectFilled(p0, p1, IM_COL32(14, 18, 26, 255), 5.0f);
    dl->AddRect(p0, p1, IM_COL32(32, 44, 60, 255), 5.0f, 0, 1.0f);

    dl->AddText(ImVec2(p0.x + 12.0f, p0.y + 8.0f), IM_COL32(0, 220, 240, 255), "32-STEP CUSTOM WAVETABLE WAVEFORM");

    if (!wt.samples.empty()) {
        float graphW = totalW - 40.0f;
        float graphH = 70.0f;
        float gY = p0.y + 40.0f;
        float gX = p0.x + 20.0f;
        float stepW = graphW / (float)wt.samples.size();

        dl->AddLine(ImVec2(gX, gY + graphH * 0.5f), ImVec2(gX + graphW, gY + graphH * 0.5f), IM_COL32(50, 65, 85, 200), 1.0f);

        for (size_t s = 0; s < wt.samples.size(); ++s) {
            float val = std::clamp(wt.samples[s], -1.0f, 1.0f);
            float bH = val * (graphH * 0.5f);
            ImVec2 bar0(gX + s * stepW + 1.0f, gY + graphH * 0.5f);
            ImVec2 bar1(gX + (s + 1) * stepW - 1.0f, gY + graphH * 0.5f - bH);
            dl->AddRectFilled(bar0, bar1, IM_COL32(0, 220, 240, 200), 1.5f);
        }
    }

    ImGui::Dummy(ImVec2(totalW, totalH + 4.0f));
}

void VgmPatchViewer::RenderAuditionKeyboard(AudioEngine& audioEngine, const VgmPreset& preset) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float totalW = ImGui::GetContentRegionAvail().x;

    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.95f, 1.0f), "Key Note Testing & Audition Piano");
    ImGui::SameLine();
    ImGui::TextDisabled("| Click keys or use computer keyboard [Z-M] to audition this patch");

    ImGui::BeginGroup();
    ImGui::Text("Octave: %d", m_testOctave);
    ImGui::SameLine();
    if (ImGui::Button("< Oct-") && m_testOctave > 1) m_testOctave--;
    ImGui::SameLine();
    if (ImGui::Button("Oct+ >") && m_testOctave < 6) m_testOctave++;
    ImGui::SameLine(0.0f, 16.0f);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Audition Vol", &m_testVolume, 0.1f, 1.2f, "%.2f");
    ImGui::EndGroup();

    // 2-Octave interactive Piano Keyboard (14 white keys, 10 black keys)
    const int numWhiteKeys = 14;
    float whiteKeyW = std::clamp((totalW - 20.0f) / (float)numWhiteKeys, 22.0f, 40.0f);
    float whiteKeyH = 75.0f;
    float blackKeyW = whiteKeyW * 0.62f;
    float blackKeyH = whiteKeyH * 0.62f;

    ImVec2 kbStart = ImGui::GetCursorScreenPos();
    kbStart.x += 6.0f;
    kbStart.y += 4.0f;

    static const int whiteKeySemitones[14] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23 };
    static const char* whiteKeyLabels[14] = { "C", "D", "E", "F", "G", "A", "B", "C", "D", "E", "F", "G", "A", "B" };
    static const int blackKeyAnchors[10] = { 0, 1, 3, 4, 5, 7, 8, 10, 11, 12 };
    static const int blackKeySemitones[10] = { 1, 3, 6, 8, 10, 13, 15, 18, 20, 22 };
    static const char* blackKeyLabels[10] = { "C#", "D#", "F#", "G#", "A#", "C#", "D#", "F#", "G#", "A#" };

    int hoveredKey = -1;
    int baseMidi = (m_testOctave + 1) * 12;

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool mouseDown = ImGui::GetIO().MouseDown[0];

    // Check black keys hover
    for (int b = 0; b < 10; ++b) {
        int wAnchor = blackKeyAnchors[b];
        float bkX = kbStart.x + (wAnchor + 1) * whiteKeyW - blackKeyW * 0.5f;
        ImVec2 bk0(bkX, kbStart.y);
        ImVec2 bk1(bkX + blackKeyW, kbStart.y + blackKeyH);
        if (mousePos.x >= bk0.x && mousePos.x <= bk1.x && mousePos.y >= bk0.y && mousePos.y <= bk1.y) {
            hoveredKey = blackKeySemitones[b];
            break;
        }
    }

    // Check white keys hover
    if (hoveredKey == -1) {
        for (int w = 0; w < 14; ++w) {
            float wkX = kbStart.x + w * whiteKeyW;
            ImVec2 wk0(wkX, kbStart.y);
            ImVec2 wk1(wkX + whiteKeyW, kbStart.y + whiteKeyH);
            if (mousePos.x >= wk0.x && mousePos.x <= wk1.x && mousePos.y >= wk0.y && mousePos.y <= wk1.y) {
                hoveredKey = whiteKeySemitones[w];
                break;
            }
        }
    }

    // Computer keyboard tracker keys
    int kbKey = -1;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyDown(ImGuiKey_Z)) kbKey = 0;           // C
        else if (ImGui::IsKeyDown(ImGuiKey_S)) kbKey = 1;      // C#
        else if (ImGui::IsKeyDown(ImGuiKey_X)) kbKey = 2;      // D
        else if (ImGui::IsKeyDown(ImGuiKey_D)) kbKey = 3;      // D#
        else if (ImGui::IsKeyDown(ImGuiKey_C)) kbKey = 4;      // E
        else if (ImGui::IsKeyDown(ImGuiKey_V)) kbKey = 5;      // F
        else if (ImGui::IsKeyDown(ImGuiKey_G)) kbKey = 6;      // F#
        else if (ImGui::IsKeyDown(ImGuiKey_B)) kbKey = 7;      // G
        else if (ImGui::IsKeyDown(ImGuiKey_H)) kbKey = 8;      // G#
        else if (ImGui::IsKeyDown(ImGuiKey_N)) kbKey = 9;      // A
        else if (ImGui::IsKeyDown(ImGuiKey_J)) kbKey = 10;     // A#
        else if (ImGui::IsKeyDown(ImGuiKey_M)) kbKey = 11;     // B
        else if (ImGui::IsKeyDown(ImGuiKey_Comma)) kbKey = 12; // C+1
        else if (ImGui::IsKeyDown(ImGuiKey_L)) kbKey = 13;     // C#+1
        else if (ImGui::IsKeyDown(ImGuiKey_Period)) kbKey = 14;// D+1
    }

    int activeKey = -1;
    if (mouseDown && hoveredKey != -1) {
        activeKey = hoveredKey;
    } else if (kbKey != -1) {
        activeKey = kbKey;
    }

    if (activeKey != -1) {
        if (!m_mouseTesting || m_activeTestKey != activeKey) {
            m_mouseTesting = true;
            m_activeTestKey = activeKey;
            int testMidi = baseMidi + activeKey;
            m_lastTestedMidi = testMidi;
            float freq = 440.0f * std::pow(2.0f, (testMidi - 69.0f) / 12.0f);
            TriggerAuditionNote(audioEngine, preset, freq);
        }
    } else {
        if (m_mouseTesting) {
            m_mouseTesting = false;
            m_activeTestKey = -1;
            StopAuditionNote(audioEngine);
        }
    }

    // 1. White keys
    for (int w = 0; w < 14; ++w) {
        int semi = whiteKeySemitones[w];
        float wkX = kbStart.x + w * whiteKeyW;
        ImVec2 wk0(wkX, kbStart.y);
        ImVec2 wk1(wkX + whiteKeyW, kbStart.y + whiteKeyH);

        bool isActive = (m_activeTestKey == semi);
        bool isHovered = (hoveredKey == semi && !m_mouseTesting);

        ImU32 wCol = IM_COL32(235, 240, 248, 255);
        if (isActive) wCol = IM_COL32(0, 220, 240, 255);
        else if (isHovered) wCol = IM_COL32(200, 225, 245, 255);

        dl->AddRectFilled(wk0, wk1, wCol, 3.0f);
        dl->AddRect(wk0, wk1, IM_COL32(40, 50, 65, 255), 3.0f, 0, 1.0f);

        char kLbl[16];
        int oct = m_testOctave + (w >= 7 ? 1 : 0);
        std::snprintf(kLbl, sizeof(kLbl), "%s%d", whiteKeyLabels[w], oct);
        ImVec2 lSz = ImGui::CalcTextSize(kLbl);
        dl->AddText(ImVec2(wk0.x + (whiteKeyW - lSz.x) * 0.5f, wk1.y - 18.0f),
                    isActive ? IM_COL32(10, 20, 30, 255) : IM_COL32(70, 85, 105, 220), kLbl);
    }

    // 2. Black keys
    for (int b = 0; b < 10; ++b) {
        int semi = blackKeySemitones[b];
        int wAnchor = blackKeyAnchors[b];
        float bkX = kbStart.x + (wAnchor + 1) * whiteKeyW - blackKeyW * 0.5f;
        ImVec2 bk0(bkX, kbStart.y);
        ImVec2 bk1(bkX + blackKeyW, kbStart.y + blackKeyH);

        bool isActive = (m_activeTestKey == semi);
        bool isHovered = (hoveredKey == semi && !m_mouseTesting);

        ImU32 bCol = IM_COL32(24, 30, 42, 255);
        if (isActive) bCol = IM_COL32(20, 160, 240, 255);
        else if (isHovered) bCol = IM_COL32(45, 60, 85, 255);

        dl->AddRectFilled(bk0, bk1, bCol, 2.5f);
        dl->AddRect(bk0, bk1, IM_COL32(10, 14, 20, 255), 2.5f, 0, 1.0f);

        const char* bLbl = blackKeyLabels[b];
        ImVec2 bSz = ImGui::CalcTextSize(bLbl);
        dl->AddText(ImVec2(bk0.x + (blackKeyW - bSz.x) * 0.5f, bk1.y - 16.0f),
                    isActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 175, 205, 200), bLbl);
    }

    ImGui::Dummy(ImVec2(totalW, whiteKeyH + 10.0f));

    if (m_lastTestedMidi >= 0) {
        float f = 440.0f * std::pow(2.0f, (m_lastTestedMidi - 69.0f) / 12.0f);
        int semi = m_lastTestedMidi % 12;
        int oct = (m_lastTestedMidi / 12) - 1;
        if (m_activeTestKey >= 0) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.85f, 1.0f), "● Auditioning: %s%d (MIDI %d) -> %.2f Hz",
                               NOTE_NAMES[semi], oct, m_lastTestedMidi, f);
        } else {
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.95f, 1.0f), "Last Auditioned: %s%d (MIDI %d) -> %.2f Hz",
                               NOTE_NAMES[semi], oct, m_lastTestedMidi, f);
        }
    } else {
        ImGui::TextDisabled("Audition: Click/drag keys above, or use computer keyboard [Z,S,X,D,C,V,G,B,H,N,J,M] to play");
    }
}

void VgmPatchViewer::RenderPresetList(AudioEngine& audioEngine, float width) {
    ImGui::BeginChild("PresetListPanel", ImVec2(width, 0.0f), true);

    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.95f, 1.0f), "Presets & Patches (%d)", (int)m_extractor.GetPresetCount());
    ImGui::Separator();

    // Quick Channel Selector Bar
    const auto& allChannels = m_extractor.GetAllChannels();
    if (!allChannels.empty()) {
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 0.95f, 1.0f), "Channels:");
        ImGui::SameLine();
        
        bool isAll = (m_channelFilterIdx == 0);
        if (isAll) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.50f, 0.70f, 1.0f));
        if (ImGui::SmallButton("All")) {
            m_channelFilterIdx = 0;
        }
        if (isAll) ImGui::PopStyleColor();

        for (int c = 0; c < (int)allChannels.size(); ++c) {
            ImGui::SameLine();
            bool isChSel = (m_channelFilterIdx == c + 1);
            if (isChSel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.50f, 0.70f, 1.0f));
            if (ImGui::SmallButton(allChannels[c].c_str())) {
                m_channelFilterIdx = c + 1;
            }
            if (isChSel) ImGui::PopStyleColor();
        }
        ImGui::Spacing();
    }

    // Filters
    ImGui::SetNextItemWidth(width - 16.0f);
    ImGui::InputTextWithHint("##PreSearch", "Search presets, channels, chips...", m_searchFilter, sizeof(m_searchFilter));

    float filterColW = (width - 24.0f) * 0.5f;

    static const char* chipFilterOptions[] = { "All Chips", "YM2612", "YM2151", "YM2203", "NES APU", "HuC6280", "SN76496" };
    ImGui::SetNextItemWidth(filterColW);
    ImGui::Combo("##ChipFlt", &m_chipFilterIdx, chipFilterOptions, IM_ARRAYSIZE(chipFilterOptions));

    ImGui::SameLine();
    // Dynamic channel options
    std::string chPreview = "All Channels";
    if (m_channelFilterIdx > 0 && m_channelFilterIdx <= (int)allChannels.size()) {
        chPreview = allChannels[m_channelFilterIdx - 1];
    }
    ImGui::SetNextItemWidth(filterColW);
    if (ImGui::BeginCombo("##ChFlt", chPreview.c_str())) {
        if (ImGui::Selectable("All Channels", m_channelFilterIdx == 0)) {
            m_channelFilterIdx = 0;
        }
        for (int c = 0; c < (int)allChannels.size(); ++c) {
            bool isSel = (m_channelFilterIdx == c + 1);
            if (ImGui::Selectable(allChannels[c].c_str(), isSel)) {
                m_channelFilterIdx = c + 1;
            }
        }
        ImGui::EndCombo();
    }

    static const char* typeFilterOptions[] = { "All Types", "FM 4-Op", "Square", "Triangle", "Noise", "Wavetable" };
    ImGui::SetNextItemWidth(filterColW);
    ImGui::Combo("##TypeFlt", &m_typeFilterIdx, typeFilterOptions, IM_ARRAYSIZE(typeFilterOptions));

    ImGui::SameLine();
    static const char* sortOptions[] = { "Most Used", "Song Order", "By Channel", "By Name" };
    ImGui::SetNextItemWidth(filterColW);
    ImGui::Combo("##SortMode", &m_sortModeIdx, sortOptions, IM_ARRAYSIZE(sortOptions));

    ImGui::Spacing();

    // Preset items list
    const auto& presets = m_extractor.GetPresets();
    float listHeight = ImGui::GetContentRegionAvail().y - 75.0f;
    ImGui::BeginChild("PresetItems", ImVec2(0.0f, listHeight), true);

    std::string searchLower = m_searchFilter;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    std::string selectedChannelName = "";
    if (m_channelFilterIdx > 0 && m_channelFilterIdx <= (int)allChannels.size()) {
        selectedChannelName = allChannels[m_channelFilterIdx - 1];
    }

    std::vector<int> filteredIndices;
    for (int i = 0; i < (int)presets.size(); ++i) {
        const auto& p = presets[i];

        // Chip filter
        if (m_chipFilterIdx > 0) {
            std::string selectedChip = chipFilterOptions[m_chipFilterIdx];
            if (p.chipName.find(selectedChip) == std::string::npos) continue;
        }

        // Channel filter
        if (!selectedChannelName.empty()) {
            if (!p.IsUsedByChannel(selectedChannelName)) continue;
        }

        // Type filter
        if (m_typeFilterIdx > 0) {
            if (m_typeFilterIdx == 1 && p.type != PresetType::FM_4OP && p.type != PresetType::FM_2OP) continue;
            if (m_typeFilterIdx == 2 && p.type != PresetType::SQUARE) continue;
            if (m_typeFilterIdx == 3 && p.type != PresetType::TRIANGLE) continue;
            if (m_typeFilterIdx == 4 && p.type != PresetType::NOISE) continue;
            if (m_typeFilterIdx == 5 && p.type != PresetType::WAVETABLE) continue;
        }

        // Search text filter
        if (!searchLower.empty()) {
            std::string nameLower = p.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            std::string chipLower = p.chipName;
            std::transform(chipLower.begin(), chipLower.end(), chipLower.begin(), ::tolower);
            std::string chsLower = p.GetChannelsString();
            std::transform(chsLower.begin(), chsLower.end(), chsLower.begin(), ::tolower);

            if (nameLower.find(searchLower) == std::string::npos &&
                chipLower.find(searchLower) == std::string::npos &&
                chsLower.find(searchLower) == std::string::npos) {
                continue;
            }
        }

        filteredIndices.push_back(i);
    }

    // Sort filtered presets
    std::sort(filteredIndices.begin(), filteredIndices.end(), [&](int a, int b) {
        const auto& pa = presets[a];
        const auto& pb = presets[b];
        if (m_sortModeIdx == 0) {
            int countA = pa.useCount;
            int countB = pb.useCount;
            if (!selectedChannelName.empty()) {
                auto itA = pa.channelUseCounts.find(selectedChannelName);
                if (itA != pa.channelUseCounts.end()) countA = itA->second;
                auto itB = pb.channelUseCounts.find(selectedChannelName);
                if (itB != pb.channelUseCounts.end()) countB = itB->second;
            }
            if (countA != countB) return countA > countB;
            return pa.id < pb.id;
        } else if (m_sortModeIdx == 1) {
            return pa.id < pb.id;
        } else if (m_sortModeIdx == 2) {
            if (pa.channelName != pb.channelName) return pa.channelName < pb.channelName;
            return pa.useCount > pb.useCount;
        } else {
            return pa.name < pb.name;
        }
    });

    // Keep selection valid
    bool selInFiltered = false;
    for (int idx : filteredIndices) {
        if (idx == m_selectedPresetIdx) {
            selInFiltered = true;
            break;
        }
    }
    if (!selInFiltered && !filteredIndices.empty()) {
        m_selectedPresetIdx = filteredIndices[0];
    }

    for (int i : filteredIndices) {
        const auto& p = presets[i];
        bool isSelected = (i == m_selectedPresetIdx);
        char itemLabel[160];
        std::snprintf(itemLabel, sizeof(itemLabel), "%02d. %s##pre_%d", p.id, p.name.c_str(), i);
        std::string compactCh = p.GetCompactChannelsString();
        char badgeBuf[64];
        if (!selectedChannelName.empty()) {
            auto it = p.channelUseCounts.find(selectedChannelName);
            int chCnt = (it != p.channelUseCounts.end()) ? it->second : 0;
            std::snprintf(badgeBuf, sizeof(badgeBuf), "%dx (%d)", chCnt, p.useCount);
        } else {
            std::snprintf(badgeBuf, sizeof(badgeBuf), "[%s] x%d", compactCh.c_str(), p.useCount);
        }

        float countWidth = 105.0f;
        float itemWidth = ImGui::GetContentRegionAvail().x - countWidth;
        if (itemWidth < 120.0f) itemWidth = 120.0f;

        bool hovered = false;
        if (ImGui::Selectable(itemLabel, isSelected, 0, ImVec2(itemWidth, 0.0f))) {
            m_selectedPresetIdx = i;
            if (m_mouseTesting) {
                m_mouseTesting = false;
                StopAuditionNote(audioEngine);
            }
        }
        if (ImGui::IsItemHovered()) hovered = true;

        ImGui::SameLine();
        ImGui::TextDisabled("%s", badgeBuf);
        if (ImGui::IsItemHovered()) hovered = true;

        if (hovered) {
            std::string chBreakdown;
            for (const auto& ch : p.usedChannels) {
                auto it = p.channelUseCounts.find(ch);
                int c = (it != p.channelUseCounts.end()) ? it->second : 1;
                chBreakdown += "  • " + ch + ": " + std::to_string(c) + " notes\n";
            }
            ImGui::SetTooltip("%s\nChip: %s\nFirst used: %s (Sample %u)\nTotal triggers: %d times\n\nChannels using this preset:\n%s",
                              p.name.c_str(), p.chipName.c_str(), p.timestampStr.c_str(),
                              p.firstSamplePos, p.useCount, chBreakdown.c_str());
        }
    }
    ImGui::EndChild();

    // Export Buttons Panel
    ImGui::Spacing();
    if (!presets.empty() && m_selectedPresetIdx >= 0 && m_selectedPresetIdx < (int)presets.size()) {
        const auto& curP = presets[m_selectedPresetIdx];
        bool isFm = (curP.type == PresetType::FM_4OP);

        ImGui::BeginDisabled(!isFm);
        if (ImGui::Button("Export .TFI")) {
            std::filesystem::create_directories("presets");
            std::string path = "presets/" + curP.chipName + "_preset_" + std::to_string(curP.id) + ".tfi";
            if (VgmPatchExtractor::ExportTFI(curP, path)) {
                m_exportStatusMessage = "Exported to: " + path;
                m_exportStatusTimer = 3.0f;
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!isFm);
        if (ImGui::Button("Export .VGI")) {
            std::filesystem::create_directories("presets");
            std::string path = "presets/" + curP.chipName + "_preset_" + std::to_string(curP.id) + ".vgi";
            if (VgmPatchExtractor::ExportVGI(curP, path)) {
                m_exportStatusMessage = "Exported to: " + path;
                m_exportStatusTimer = 3.0f;
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Copy Text")) {
            std::string txt = VgmPatchExtractor::FormatPresetText(curP);
            ImGui::SetClipboardText(txt.c_str());
            m_exportStatusMessage = "Copied patch parameters to clipboard!";
            m_exportStatusTimer = 2.5f;
        }

        if (m_exportStatusTimer > 0.0f) {
            m_exportStatusTimer -= ImGui::GetIO().DeltaTime;
            ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.4f, 1.0f), "%s", m_exportStatusMessage.c_str());
        }
    }

    ImGui::EndChild();
}

void VgmPatchViewer::RenderPresetDetails(AudioEngine& audioEngine, const VgmPreset& preset, float width) {
    ImGui::BeginChild("PresetDetailsPanel", ImVec2(width, 0.0f), true);

    // Header Badge
    ImGui::TextColored(ImVec4(0.0f, 0.95f, 1.0f, 1.0f), "%s", preset.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("| Chip: %s | Source Channel: %s | Notes: %d | Time: %s",
                        preset.chipName.c_str(), preset.channelName.c_str(), preset.useCount, preset.timestampStr.c_str());

    ImGui::Spacing();
    // Channels row with interactive badges
    ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.98f, 1.0f), "Channels using this preset:");
    for (const auto& ch : preset.usedChannels) {
        auto it = preset.channelUseCounts.find(ch);
        int count = (it != preset.channelUseCounts.end()) ? it->second : 1;
        char badge[64];
        std::snprintf(badge, sizeof(badge), " %s (%dx) ", ch.c_str(), count);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.28f, 0.38f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.44f, 0.58f, 0.95f));
        if (ImGui::SmallButton(badge)) {
            // Quick filter presets by clicking this channel badge
            const auto& allChs = m_extractor.GetAllChannels();
            for (size_t cIdx = 0; cIdx < allChs.size(); ++cIdx) {
                if (allChs[cIdx] == ch) {
                    m_channelFilterIdx = (int)cIdx + 1;
                    break;
                }
            }
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click to filter preset list to %s (%d notes)", ch.c_str(), count);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Specific inspector based on preset type
    if (preset.type == PresetType::FM_4OP || preset.type == PresetType::FM_2OP) {
        RenderFmDetails(preset.fm);
    } else if (preset.type == PresetType::SQUARE) {
        RenderSquareDetails(preset.square);
    } else if (preset.type == PresetType::TRIANGLE) {
        RenderTriangleDetails(preset.triangle);
    } else if (preset.type == PresetType::NOISE) {
        RenderNoiseDetails(preset.noise);
    } else if (preset.type == PresetType::WAVETABLE) {
        RenderWavetableDetails(preset.wavetable);
    }

    // Decoded Raw VGM Hardware Registers
    if (!preset.rawRegisters.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Decoded VGM Hardware Register Instructions", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Extracted directly from VGM/VGZ write commands for %s (%d registers captured):",
                                preset.channelName.c_str(), (int)preset.rawRegisters.size());
            ImGui::Spacing();
            int colCount = 0;
            for (const auto& kv : preset.rawRegisters) {
                char badge[64];
                std::snprintf(badge, sizeof(badge), "%s: 0x%02X (%d)", kv.first.c_str(), kv.second, kv.second);
                if (colCount > 0 && colCount % 3 != 0) ImGui::SameLine(0.0f, 8.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.14f, 0.20f, 0.9f));
                ImGui::SmallButton(badge);
                ImGui::PopStyleColor();
                colCount++;
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Audition Keyboard
    RenderAuditionKeyboard(audioEngine, preset);

    ImGui::EndChild();
}

void VgmPatchViewer::Render(AudioEngine& audioEngine, bool* pOpen) {
    if (m_focusRequested) {
        m_focusRequested = false;
        ImGui::SetNextWindowFocus();
    }

    if (!m_hasScanned && audioEngine.IsLoaded()) {
        ScanSong(audioEngine);
    }

    ImGui::SetNextWindowSize(ImVec2(1020, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("VGM Preset / Patch Viewer###VGMPatchViewer", pOpen)) {
        ImGui::End();
        return;
    }

    if (!audioEngine.IsLoaded()) {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "No VGM or VGZ song loaded.");
        ImGui::TextDisabled("Load a song with FM, NES APU, Game Boy, HuC6280, or PSG sound chips to view and dump extracted presets.");
        ImGui::End();
        return;
    }

    if (m_extractor.IsEmpty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Scanning song for presets...");
        if (ImGui::Button("Scan VGM Now")) {
            ScanSong(audioEngine);
        }
        ImGui::End();
        return;
    }

    float totalW = ImGui::GetContentRegionAvail().x;
    float leftW = std::clamp(totalW * 0.36f, 320.0f, 420.0f);
    float rightW = totalW - leftW - 10.0f;

    RenderPresetList(audioEngine, leftW);
    ImGui::SameLine(0.0f, 8.0f);

    const auto& presets = m_extractor.GetPresets();
    if (m_selectedPresetIdx >= 0 && m_selectedPresetIdx < (int)presets.size()) {
        RenderPresetDetails(audioEngine, presets[m_selectedPresetIdx], rightW);
    }

    ImGui::End();
}

} // namespace vgmstudio
