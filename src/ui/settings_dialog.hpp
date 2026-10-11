#pragma once

#include "piano_roll.hpp"
#include "../audio/audio_engine.hpp"
#include <imgui.h>

namespace vgmstudio {

class SettingsDialog {
public:
    SettingsDialog();
    ~SettingsDialog();

    void Open(int category = 0);
    bool Render(PianoRoll& pianoRoll, AudioEngine& audioEngine);

    int GetFontIndex() const { return m_fontIndex; }
    void SetFontIndex(int idx) { m_fontIndex = idx; }

private:
    bool m_isOpen = false;
    bool m_openRequested = false;
    int m_selectedCategory = 0; // 0 = VGM Player, 1 = Piano Roll, 2 = Appearance, 3 = Shortcut Key
    bool m_showRestoreConfirm = false;
    bool m_showSaveSuccess = false;

    // Appearance state
    int m_themeIndex = 0;
    int m_fontIndex = 0;
    float m_uiScale = 1.0f;
};

} // namespace vgmstudio
