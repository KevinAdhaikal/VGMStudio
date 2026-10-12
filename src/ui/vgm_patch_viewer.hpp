#pragma once

#include <vector>
#include <string>
#include <imgui.h>
#include "../vgm/vgm_patch_extractor.hpp"
#include "../audio/audio_engine.hpp"

namespace vgmstudio {

class VgmPatchViewer {
public:
    VgmPatchViewer();
    ~VgmPatchViewer();

    void Render(AudioEngine& audioEngine, bool* pOpen);
    void RequestFocus() { m_focusRequested = true; }
    void Reset();
    void ScanSong(AudioEngine& audioEngine);
    void SetChannelFilter(const std::string& chName);

private:
    VgmPatchExtractor m_extractor;
    bool m_focusRequested = false;
    bool m_hasScanned = false;
    int m_selectedPresetIdx = 0;

    // Filters
    char m_searchFilter[64] = {0};
    int m_chipFilterIdx = 0;     // 0 = All Chips
    int m_channelFilterIdx = 0;  // 0 = All Channels
    int m_typeFilterIdx = 0;     // 0 = All Types
    int m_sortModeIdx = 0;       // 0 = Most Used (Default), 1 = Song Order, 2 = Channel, 3 = Name

    // Audition keyboard state
    int m_testOctave = 3;
    float m_testVolume = 0.85f;
    int m_activeTestKey = -1;
    bool m_mouseTesting = false;
    int m_lastTestedMidi = -1;

    // Export status notification
    std::string m_exportStatusMessage;
    float m_exportStatusTimer = 0.0f;

    void RenderPresetList(AudioEngine& audioEngine, float width);
    void RenderPresetDetails(AudioEngine& audioEngine, const VgmPreset& preset, float width);
    void RenderFmDetails(const FmPresetData& fm);
    void RenderSquareDetails(const SquarePresetData& sq);
    void RenderTriangleDetails(const TrianglePresetData& tr);
    void RenderNoiseDetails(const NoisePresetData& ns);
    void RenderWavetableDetails(const WavetablePresetData& wt);
    void RenderAuditionKeyboard(AudioEngine& audioEngine, const VgmPreset& preset);

    void RenderAlgorithmDiagram(int algorithm, int feedback, const FmOperatorPreset ops[4]);
    void RenderEnvelopeCurve(const FmOperatorPreset& op, ImVec2 size, bool isCarrier);
    void RenderOperatorCard(int opIdx, const FmOperatorPreset& op, bool isCarrier, float cardW);

    void TriggerAuditionNote(AudioEngine& audioEngine, const VgmPreset& preset, float freq);
    void StopAuditionNote(AudioEngine& audioEngine);
};

} // namespace vgmstudio
