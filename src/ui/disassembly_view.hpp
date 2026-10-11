#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include "../audio/audio_engine.hpp"

namespace vgmstudio {

class DisassemblyView {
public:
    DisassemblyView();
    ~DisassemblyView();

    void Render(AudioEngine& audioEngine, bool* pOpen = nullptr);
    void RequestFocus() { m_focusRequested = true; }

private:
    bool m_focusRequested = false;
    char m_searchFilter[128] = {0};
    int m_categoryFilter = 0;   // 0: All, 1: Sound Chips Only, 2: FM, 3: PSG/Square, 4: Delays, 5: Key Events, 6: Data Block
    int m_chipFilter = 0;       // 0: All Chips, 1+: index into detected chips
    int m_channelFilter = 0;    // 0: All Channels, 1+: index into detected channels
    bool m_followPlayhead = true;
    int m_lastTrackedInst = -1;
    bool m_requestScrollToCurrent = false;

    // Cache of filtered indices
    std::vector<int> m_filteredIndices;
    std::string m_lastFilterKey;
    size_t m_lastTotalInstructions = 0;

    void UpdateFilter(const VgmDisassembly& disasm);
};

} // namespace vgmstudio
