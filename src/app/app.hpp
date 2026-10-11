#pragma once

#include <SDL.h>
#include <string>
#include "../audio/audio_engine.hpp"
#include "../ui/file_dialog.hpp"
#include "../ui/piano_roll.hpp"
#include "../ui/transport_bar.hpp"
#include "../ui/channel_rack.hpp"
#include "../ui/song_info.hpp"
#include "../ui/disassembly_view.hpp"
#include "../ui/edit_song_dialog.hpp"
#include "../ui/settings_dialog.hpp"
#include "../ui/speed_pitch_view.hpp"
#include "../ui/sample_viewer.hpp"
#include "../ui/oscilloscope_view.hpp"

namespace vgmstudio {

class App {
public:
    App();
    ~App();

    bool Init(const char* title, int width, int height, const char* initialFile = nullptr);
    void Run();
    void Shutdown();

private:
    void RenderUI(bool* pPlayToggled = nullptr);
    void RenderDockSpace();
    void SetupDefaultDockLayout(unsigned int dockspace_id, const struct ImVec2& size);

    SDL_Window* m_window = nullptr;
    SDL_GLContext m_glContext = nullptr;
    bool m_running = false;
    bool m_showAboutDialog = false;
    bool m_showResetLayoutDialog = false;
    bool m_resetLayoutRequested = false;

    // Subsystems
    AudioEngine m_audioEngine;
    FileDialog m_fileDialog;
    PianoRoll m_pianoRoll;
    TransportBar m_transportBar;
    ChannelRack m_channelRack;
    SongInfo m_songInfo;
    DisassemblyView m_disassemblyView;
    EditSongDialog m_editSongDialog;
    SettingsDialog m_settingsDialog;
    SpeedPitchView m_speedPitchView;
    SampleViewer m_sampleViewer;
    OscilloscopeView m_oscilloscopeView;

    void LoadRecentFiles();
    void SaveRecentFiles();
    void AddRecentFile(const std::string& path);

    std::vector<std::string> m_recentFiles;
    std::string m_fileNotFoundPath;
    bool m_showFileNotFoundAlert = false;
    std::string m_corruptedFilePath;
    bool m_showFileCorruptedAlert = false;
    bool m_showClearRecentDialog = false;

    // Window visibility flags
    bool m_showPianoRoll = true;
    bool m_showChannels = true;
    bool m_showSongInfo = true;
    bool m_showTransport = true;
    bool m_showDisassembly = true;
    bool m_showSpeedWindow = false;
    bool m_showSampleViewer = true;
    bool m_showOscilloscopeView = true;
};

} // namespace vgmstudio
