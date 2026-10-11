#pragma once

#include "../audio/audio_engine.hpp"
#include <string>

namespace vgmstudio {

class EditSongDialog {
public:
    EditSongDialog();
    ~EditSongDialog();

    void Open(const VgmInfo& currentInfo);
    bool Render(AudioEngine& audioEngine);

private:
    bool m_isOpen = false;
    bool m_openRequested = false;

    char m_title[128]{0};
    char m_titleJp[128]{0};
    char m_game[128]{0};
    char m_gameJp[128]{0};
    char m_system[128]{0};
    char m_systemJp[128]{0};
    char m_author[128]{0};
    char m_authorJp[128]{0};
    char m_date[64]{0};
    char m_encodedBy[64]{0};
    char m_comment[512]{0};
};

} // namespace vgmstudio
