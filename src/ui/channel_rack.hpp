#pragma once

#include <imgui.h>
#include <vector>
#include "../audio/audio_engine.hpp"

namespace vgmstudio {

class ChannelRack {
public:
    ChannelRack();
    ~ChannelRack();

    // Render the Channel Rack & Mixer window
    void Render(AudioEngine& audioEngine, bool* pOpen = nullptr);

private:
    void RenderVerticalMasterStrip(AudioEngine& audioEngine, float availW, float availH);
    void RenderTableHeader(float width);
    void RenderChannelStrip(int chIdx, TrackedChannel& ch, AudioEngine& audioEngine);
    bool DrawKnob(const char* label, float* pValue, float vMin, float vMax, 
                  const char* tooltipFmt, float radius = 9.0f, ImU32 activeColor = IM_COL32(0, 210, 220, 255),
                  bool* pDoubleClicked = nullptr);

    float m_channelDecay[64]{0.0f};
    bool m_masterMuted = false;
    float m_savedMasterVol = 0.8f;

    // Channel percentage input modal state
    int m_editChannelIndex = -1;
    bool m_isEditingPan = false;
    float m_inputPercent = 100.0f;
    bool m_openKnobInputModal = false;
};

} // namespace vgmstudio
