#pragma once

#include <imgui.h>
#include "../audio/audio_engine.hpp"
#include "file_dialog.hpp"

namespace vgmstudio {

class PianoRoll;

class TransportBar {
public:
    TransportBar();
    ~TransportBar();

    // Render the Transport Bar window
    void Render(AudioEngine& audioEngine, FileDialog& fileDialog, PianoRoll* pianoRoll = nullptr, bool* pOpen = nullptr, bool* pPlayToggled = nullptr, bool* pShowSpeed = nullptr);

private:
    float m_savedVolumeBeforeMute = 0.8f;
    bool m_isMuted = false;
};

} // namespace vgmstudio
