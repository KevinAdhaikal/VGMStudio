#pragma once

#include <imgui.h>
#include "../audio/audio_engine.hpp"
#include <string>

namespace vgmstudio {

class SongInfo {
public:
    SongInfo();
    ~SongInfo();

    // Render the Song Info & GD3 metadata window
    void Render(AudioEngine& audioEngine, bool* pOpen = nullptr);
};

} // namespace vgmstudio
