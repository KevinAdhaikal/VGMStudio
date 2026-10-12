#pragma once

#include <vector>
#include <string>
#include <map>
#include <cstdint>
#include <imgui.h>
#include "../audio/audio_engine.hpp"

namespace vgmstudio {

struct ChannelLiveEventState {
    std::string lastDesc = "Idle";
    std::string lastHex;
    uint32_t lastSample = 0;
    std::string vgmPanText = "Center";
    float vgmPan = 0.0f;            // -1.0 (L) to +1.0 (R), 0.0 = Center
    float eventFlash = 0.0f;        // 1.0 (bright) down to 0.0
    float meterLevel = 0.0f;        // Smoothed level for peak meter
    float peakHold = 0.0f;          // Peak hold level
    float peakTimer = 0.0f;         // Timer before peak hold falls
};

class VgmEventsMixer {
public:
    VgmEventsMixer();
    ~VgmEventsMixer();

    void Render(AudioEngine& audioEngine, bool* pOpen);
    void RequestFocus() { m_focusRequested = true; }
    void Reset();

private:
    bool m_focusRequested = false;
    bool m_showActiveOnly = false;
    std::map<std::string, bool> m_chipFilter;

    // Live event telemetry per channel
    std::vector<ChannelLiveEventState> m_channelStates;
    uint32_t m_lastSample = 0;
    int m_lastInstIdx = 0;

    void UpdateTelemetry(AudioEngine& audioEngine, const std::vector<TrackedChannel>& channels);
    int MatchInstructionToChannel(const VgmInstruction& inst, const std::vector<TrackedChannel>& channels);
    void RenderChannelStrip(int chIdx, const TrackedChannel& ch, ChannelLiveEventState& state, float stripW, float stripH);
};

} // namespace vgmstudio
