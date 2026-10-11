#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <imgui.h>
#include "../audio/audio_engine.hpp"

namespace vgmstudio {

enum class OscWaveStyle {
    Glow = 0,
    Line = 1,
    Filled = 2
};

class OscilloscopeView {
public:
    OscilloscopeView();
    ~OscilloscopeView();

    void Render(AudioEngine& audioEngine, bool* pOpen);
    void RequestFocus() { m_focusRequested = true; }

private:
    bool m_focusRequested = false;

    // View options
    int m_columnMode = 0;       // 0 = Auto, 1 = 1 col, 2 = 2 cols, 3 = 3 cols, 4 = 4 cols, 6 = 6 cols
    OscWaveStyle m_style = OscWaveStyle::Glow;
    float m_gain = 1.0f;        // 0.5x to 4.0x
    int m_sampleCount = 512;    // 256, 512, 1024
    bool m_showGrid = true;
    bool m_showOnlyActive = false;
    bool m_hideMuted = false;

    // Buffer for fetching waveform samples from AudioEngine
    std::vector<float> m_waveBuffer;
    std::vector<ImVec2> m_polyPoints;
};

} // namespace vgmstudio
