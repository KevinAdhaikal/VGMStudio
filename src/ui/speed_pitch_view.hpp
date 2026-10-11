#pragma once

namespace vgmstudio {

class AudioEngine;

class SpeedPitchView {
public:
    SpeedPitchView();
    ~SpeedPitchView();

    void Render(AudioEngine& audioEngine, bool* pOpen = nullptr);
};

} // namespace vgmstudio
