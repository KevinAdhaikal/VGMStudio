#include "app.hpp"
#include "theme.hpp"
#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_internal.h"
#include "../third_party/imgui/backends/imgui_impl_sdl2.h"
#include "../third_party/imgui/backends/imgui_impl_opengl3.h"

#include <GL/gl.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <algorithm>

namespace vgmstudio {

static std::string FormatTime(double seconds) {
    if (seconds < 0) seconds = 0;
    int totalSec = (int)seconds;
    int min = totalSec / 60;
    int sec = totalSec % 60;
    int ms = (int)((seconds - totalSec) * 100);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d.%02d", min, sec, ms);
    return std::string(buf);
}

App::App() {}

App::~App() {
    Shutdown();
}

static std::string NormalizeRecentPath(const std::string& p) {
    if (p.empty()) return "";
    try {
        std::filesystem::path fp(p);
        if (std::filesystem::exists(fp)) {
            return std::filesystem::canonical(fp).string();
        }
        return std::filesystem::weakly_canonical(fp).string();
    } catch (...) {
        return p;
    }
}

static bool IsValidRecentPath(const std::string& p) {
    if (p.empty() || p.size() > 1024) return false;
    for (char c : p) {
        if ((unsigned char)c < 32 && c != '\t') return false;
    }
    return true;
}

void App::LoadRecentFiles() {
    m_recentFiles.clear();
    std::ifstream ifs("recent_files.txt");
    if (!ifs.is_open()) return;
    std::string line;
    while (std::getline(ifs, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
        if (!IsValidRecentPath(line)) continue;
        std::string norm = NormalizeRecentPath(line);
        if (std::find(m_recentFiles.begin(), m_recentFiles.end(), norm) == m_recentFiles.end()) {
            m_recentFiles.push_back(norm);
        }
    }
}

void App::SaveRecentFiles() {
    std::ofstream ofs("recent_files.txt");
    if (!ofs.is_open()) return;
    for (const auto& file : m_recentFiles) {
        ofs << file << "\n";
    }
}

void App::AddRecentFile(const std::string& path) {
    if (!IsValidRecentPath(path)) return;
    std::string norm = NormalizeRecentPath(path);
    auto it = std::find(m_recentFiles.begin(), m_recentFiles.end(), norm);
    if (it != m_recentFiles.end()) {
        m_recentFiles.erase(it);
    }
    m_recentFiles.insert(m_recentFiles.begin(), norm);
    if (m_recentFiles.size() > 10) {
        m_recentFiles.resize(10);
    }
    SaveRecentFiles();
}

bool App::Init(const char* title, int width, int height, const char* initialFile) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::cerr << "[Error] SDL_Init failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // OpenGL 3.0+ GLSL 130
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    m_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        window_flags
    );
    if (!m_window) {
        std::cerr << "[Error] SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        return false;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        std::cerr << "[Error] SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
        return false;
    }
    SDL_GL_MakeCurrent(m_window, m_glContext);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Load fonts into font atlas (0: Default ProggyClean, 1..N: System fonts)
    io.Fonts->AddFontDefault();

    if (std::filesystem::exists("/usr/share/fonts/liberation/LiberationSans-Regular.ttf")) {
        io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationSans-Regular.ttf", 16.0f);
    }
    if (std::filesystem::exists("/usr/share/fonts/liberation/LiberationMono-Regular.ttf")) {
        io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationMono-Regular.ttf", 15.0f);
    }
    if (std::filesystem::exists("/usr/share/fonts/liberation/LiberationSerif-Regular.ttf")) {
        io.Fonts->AddFontFromFileTTF("/usr/share/fonts/liberation/LiberationSerif-Regular.ttf", 16.0f);
    }
    if (std::filesystem::exists("/usr/share/fonts/gnu-free/FreeSans.otf")) {
        io.Fonts->AddFontFromFileTTF("/usr/share/fonts/gnu-free/FreeSans.otf", 16.0f);
    }
    if (std::filesystem::exists("/usr/share/fonts/gnu-free/FreeMono.otf")) {
        io.Fonts->AddFontFromFileTTF("/usr/share/fonts/gnu-free/FreeMono.otf", 15.0f);
    }

    // Setup Theme
    theme::ApplyFurnaceTheme();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(m_window, m_glContext);
    ImGui_ImplOpenGL3_Init("#version 130");

    LoadRecentFiles();

    // Initialize Audio Engine
    if (!m_audioEngine.Init(44100, 512)) {
        std::cerr << "[App] Failed to initialize AudioEngine!" << std::endl;
    } else {
        // Load initial file only if explicitly specified via command-line argument
        if (initialFile) {
            if (std::filesystem::exists(initialFile)) {
                if (m_audioEngine.LoadFile(initialFile)) {
                    m_pianoRoll.ResetView(m_audioEngine.GetNoteTracker());
                    AddRecentFile(initialFile);
                } else {
                    m_corruptedFilePath = initialFile;
                    m_showFileCorruptedAlert = true;
                }
            } else {
                m_fileNotFoundPath = initialFile;
                m_showFileNotFoundAlert = true;
            }
        }
    }

    if (std::getenv("VGMSTUDIO_OPEN_SETTINGS")) {
        m_settingsDialog.Open();
    }
    if (std::getenv("VGMSTUDIO_OPEN_SETTINGS_APPEARANCES")) {
        m_settingsDialog.Open(2);
    }
    if (std::getenv("VGMSTUDIO_OPEN_SPEED")) {
        m_showSpeedWindow = true;
    }
    if (std::getenv("VGMSTUDIO_OPEN_ABOUT")) {
        m_showAboutDialog = true;
    }
    if (std::getenv("VGMSTUDIO_TEST_NOT_FOUND")) {
        m_fileNotFoundPath = "samples/missing_song.vgm";
        m_showFileNotFoundAlert = true;
    }
    if (std::getenv("VGMSTUDIO_OPEN_EDIT_TAB")) {
        m_editSongDialog.Open(m_audioEngine.GetCurrentInfo());
    }

    m_running = true;
    return true;
}

void App::Run() {
    while (m_running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                m_running = false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(m_window))
                m_running = false;
        }

        // Start Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Push active font selected in Settings
        ImFont* activeFont = nullptr;
        ImGuiIO& io = ImGui::GetIO();
        int fontIdx = m_settingsDialog.GetFontIndex();
        if (fontIdx >= 0 && fontIdx < io.Fonts->Fonts.Size) {
            activeFont = io.Fonts->Fonts[fontIdx];
            ImGui::PushFont(activeFont);
        }

        RenderDockSpace();
        bool playToggledThisFrame = false;
        RenderUI(&playToggledThisFrame);

        // Global keyboard shortcuts (blocked if a modal popup like FileDialog or EditSongDialog is open)
        if (!io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            // Space: Toggle Play/Pause (only if not already toggled by Play button this frame,
            // and only if no other widget with keyboard focus was activated by space)
            if (!playToggledThisFrame && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
                ImGuiContext& g = *GImGui;
                bool navActivatedOther = (g.NavId != 0 && (g.NavActivatePressedId != 0 || ImGui::IsAnyItemActive()));
                if (!navActivatedOther) {
                    m_audioEngine.TogglePlayPause();
                }
            }
            // Ctrl+Shift+O: Oscilloscope Visualizer
            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                m_showOscilloscopeView = !m_showOscilloscopeView;
                if (m_showOscilloscopeView) m_oscilloscopeView.RequestFocus();
            }
            // Ctrl+O: Open file
            else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                m_fileDialog.Open("samples");
            }
            // Ctrl+,: Preferences / Settings
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma, false)) {
                m_settingsDialog.Open();
            }
            // Ctrl+E: Edit Song Info
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
                if (m_audioEngine.IsLoaded()) {
                    m_editSongDialog.Open(m_audioEngine.GetCurrentInfo());
                }
            }
            // Ctrl+D: Instruction Disassembly
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
                m_showDisassembly = true;
                m_disassemblyView.RequestFocus();
            }
            // Ctrl+M: Sample Viewer
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_M, false)) {
                m_showSampleViewer = true;
                m_sampleViewer.RequestFocus();
            }
            // Home: Rewind to beginning
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
                m_audioEngine.Rewind();
                m_pianoRoll.RewindToStart();
            }
        }

        // Render File Dialog (Modal)
        std::string selectedVgm;
        if (m_fileDialog.Render(selectedVgm)) {
            std::cout << "[App] Loading selected VGM file: " << selectedVgm << std::endl;
            if (m_audioEngine.LoadFile(selectedVgm)) {
                m_pianoRoll.ResetView(m_audioEngine.GetNoteTracker());
                AddRecentFile(selectedVgm);
            } else {
                m_corruptedFilePath = selectedVgm;
                m_showFileCorruptedAlert = true;
            }
        }

        // Render Edit Song Info Dialog (Modal)
        m_editSongDialog.Render(m_audioEngine);

        // Render Preferences & Settings Dialog (Modal)
        m_settingsDialog.Render(m_pianoRoll, m_audioEngine);

        if (activeFont) {
            ImGui::PopFont();
        }

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(m_window);
    }
}

void App::RenderDockSpace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_window_flags = 0;
    host_window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking;
    host_window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    host_window_flags |= ImGuiWindowFlags_MenuBar;

    char label[32];
    snprintf(label, sizeof(label), "DockSpaceHost_%08X", viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(label, NULL, host_window_flags);
    ImGui::PopStyleVar(3);

    // Menu Bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open VGM/VGZ...", "Ctrl+O")) {
                m_fileDialog.Open("samples");
            }
            if (ImGui::BeginMenu("Open Recent")) {
                if (m_recentFiles.empty()) {
                    ImGui::MenuItem("(No Recent Files)", nullptr, false, false);
                } else {
                    for (size_t idx = 0; idx < m_recentFiles.size(); ++idx) {
                        const auto& rFile = m_recentFiles[idx];
                        std::string label = rFile + "##recent_" + std::to_string(idx);

                        ImGui::PushID((int)idx);
                        if (ImGui::MenuItem(label.c_str())) {
                            if (!std::filesystem::exists(rFile)) {
                                m_fileNotFoundPath = rFile;
                                m_showFileNotFoundAlert = true;
                            } else {
                                if (m_audioEngine.LoadFile(rFile)) {
                                    m_pianoRoll.ResetView(m_audioEngine.GetNoteTracker());
                                    AddRecentFile(rFile);
                                } else {
                                    m_corruptedFilePath = rFile;
                                    m_showFileCorruptedAlert = true;
                                }
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", rFile.c_str());
                        }
                        ImGui::PopID();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent Files")) {
                        m_showClearRecentDialog = true;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Settings...", "Ctrl+,")) {
                m_settingsDialog.Open();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                m_running = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            bool canEdit = m_audioEngine.IsLoaded();
            if (ImGui::MenuItem("Edit Song Info...", "Ctrl+E", false, canEdit)) {
                m_editSongDialog.Open(m_audioEngine.GetCurrentInfo());
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Playback")) {
            if (ImGui::MenuItem("Play/Pause", "Space")) {
                m_audioEngine.TogglePlayPause();
            }
            if (ImGui::MenuItem("Stop")) {
                m_audioEngine.Stop();
                m_pianoRoll.RewindToStart();
            }
            if (ImGui::MenuItem("Rewind", "Home")) {
                m_audioEngine.Rewind();
                m_pianoRoll.RewindToStart();
            }
            ImGui::Separator();
            bool loop = m_audioEngine.GetLoop();
            if (ImGui::MenuItem("Loop Song", nullptr, &loop)) {
                m_audioEngine.SetLoop(loop);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::BeginMenu("Piano Roll")) {
                bool isHoriz = (m_pianoRoll.GetVisualMode() == VisualMode::Horizontal);
                if (ImGui::MenuItem("Horizontal", nullptr, isHoriz)) {
                    m_pianoRoll.SetVisualMode(VisualMode::Horizontal);
                    m_showPianoRoll = true;
                }
                bool isWaterfall = (m_pianoRoll.GetVisualMode() == VisualMode::Waterfall);
                if (ImGui::MenuItem("Waterfall", nullptr, isWaterfall)) {
                    m_pianoRoll.SetVisualMode(VisualMode::Waterfall);
                    m_showPianoRoll = true;
                }
                ImGui::EndMenu();
            }
            ImGui::MenuItem("Channels / Mixer", nullptr, &m_showChannels);
            ImGui::MenuItem("Song Info", nullptr, &m_showSongInfo);
            ImGui::MenuItem("Player Control", nullptr, &m_showTransport);
            ImGui::MenuItem("Speed", nullptr, &m_showSpeedWindow);
            if (ImGui::MenuItem("VGM Instruction Disassembly", "Ctrl+D", &m_showDisassembly)) {
                if (m_showDisassembly) m_disassemblyView.RequestFocus();
            }
            if (ImGui::MenuItem("Sample Viewer", "Ctrl+M", &m_showSampleViewer)) {
                if (m_showSampleViewer) m_sampleViewer.RequestFocus();
            }
            if (ImGui::MenuItem("Oscilloscope Visualizer", "Ctrl+Shift+O", &m_showOscilloscopeView)) {
                if (m_showOscilloscopeView) m_oscilloscopeView.RequestFocus();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout to Default")) {
                m_showResetLayoutDialog = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About VGM Studio")) {
                m_showAboutDialog = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGuiID dockspace_id = ImGui::GetID("VGMStudioDockSpace");
    ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace_id);
    if (node == nullptr || node->IsEmpty() || m_resetLayoutRequested) {
        m_resetLayoutRequested = false;
        SetupDefaultDockLayout(dockspace_id, viewport->WorkSize);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

void App::SetupDefaultDockLayout(unsigned int dockspace_id, const ImVec2& size) {
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, size);

    ImGuiID dock_main_id = dockspace_id;
    ImGuiID dock_top = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up, 0.12f, nullptr, &dock_main_id);
    ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.28f, nullptr, &dock_main_id);
    ImGuiID dock_down = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.40f, nullptr, &dock_main_id);

    ImGui::DockBuilderDockWindow("Player Control", dock_top);
    ImGui::DockBuilderDockWindow("Piano Roll", dock_main_id);
    ImGui::DockBuilderDockWindow("Oscilloscope Visualizer", dock_main_id);
    ImGui::DockBuilderDockWindow("VGM Instruction Disassembly", dock_down);
    ImGui::DockBuilderDockWindow("Channels / Mixer", dock_down);
    ImGui::DockBuilderDockWindow("Sample Viewer", dock_down);
    ImGui::DockBuilderDockWindow("Song Info", dock_right);

    ImGui::DockBuilderFinish(dockspace_id);

    ImGuiDockNode* centralNode = ImGui::DockBuilderGetNode(dock_main_id);
    if (centralNode) {
        centralNode->SelectedTabId = ImHashStr("Piano Roll");
    }
    ImGuiDockNode* bottomNode = ImGui::DockBuilderGetNode(dock_down);
    if (bottomNode) {
        bottomNode->SelectedTabId = ImHashStr("VGM Instruction Disassembly");
    }
}

void App::RenderUI(bool* pPlayToggled) {
    // 1. Transport Bar Window
    if (m_showTransport) {
        m_transportBar.Render(m_audioEngine, m_fileDialog, &m_pianoRoll, &m_showTransport, pPlayToggled, &m_showSpeedWindow);
    }

    // 2. Speed & Pitch Window
    if (m_showSpeedWindow) {
        m_speedPitchView.Render(m_audioEngine, &m_showSpeedWindow);
    }

    // 3. Channel Rack & Mixer Window
    if (m_showChannels) {
        m_channelRack.Render(m_audioEngine, &m_showChannels);
    }

    // 4. VGM Instruction Disassembly Window
    if (m_showDisassembly) {
        m_disassemblyView.Render(m_audioEngine, &m_showDisassembly);
    }

    // 5. Piano Roll Window
    if (m_showPianoRoll) {
        m_pianoRoll.Render(m_audioEngine, &m_showPianoRoll);
    }

    // 6. Song Info Window
    if (m_showSongInfo) {
        m_songInfo.Render(m_audioEngine, &m_showSongInfo);
    }

    // 7. Sample Viewer Window
    if (m_showSampleViewer) {
        m_sampleViewer.Render(m_audioEngine, &m_showSampleViewer);
    }

    // 8. Oscilloscope Visualizer Window
    if (m_showOscilloscopeView) {
        m_oscilloscopeView.Render(m_audioEngine, &m_showOscilloscopeView);
    }

    // About Dialog
    if (m_showAboutDialog) {
        ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("About VGM Studio", &m_showAboutDialog, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
            // Header
            ImGui::TextColored(ImVec4(0.0f, 0.88f, 0.95f, 1.0f), "VGM Studio");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1.0f), "Release v1.0.0");
            ImGui::SameLine();
            ImGui::TextDisabled("|  Retro Sound Workstation");
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginTabBar("AboutTabBar", ImGuiTabBarFlags_None)) {
                if (ImGui::BeginTabItem("About")) {
                    ImGui::Spacing();
                    ImGui::TextWrapped("VGM Studio is an audio player and visualizer for Video Game Music (.vgm, .vgz) files.");
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "Features:");
                    ImGui::BulletText("Piano Roll: Horizontal timeline and waterfall note visualizations");
                    ImGui::BulletText("Channel Mixer: Per-channel mute, solo, volume, and stereo panning");
                    ImGui::BulletText("Instruction Disassembly: Live VGM register writes and opcode log");
                    ImGui::BulletText("Speed & Pitch: Independent or linked playback tempo and pitch control");
                    ImGui::BulletText("Song Info & GD3: View and edit metadata tags and chip info");
                    ImGui::BulletText("Oscilloscope Visualizer: Per-channel live waveform monitoring and CRT scope displays");
                    ImGui::Spacing();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Shortcuts")) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "Keyboard Shortcuts:");
                    ImGui::Spacing();

                    if (ImGui::BeginTable("ShortcutsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Key / Shortcut", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();

                        auto addShortcut = [](const char* key, const char* action) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.25f, 1.0f), "%s", key);
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", action);
                        };

                        addShortcut("Space", "Play / Pause playback");
                        addShortcut("Home", "Rewind song to beginning");
                        addShortcut("Ctrl + O", "Open file browser (.vgm, .vgz)");
                        addShortcut("Ctrl + E", "Edit song metadata (GD3 tags)");
                        addShortcut("Ctrl + D", "Toggle VGM Disassembly view");
                        addShortcut("Ctrl + M", "Toggle Sample Viewer");
                        addShortcut("Ctrl + Shift + O", "Toggle Oscilloscope Visualizer");
                        addShortcut("Ctrl + ,", "Open Settings dialog");
                        addShortcut("Alt + F4", "Exit application");

                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Credits")) {
                    ImGui::Spacing();
                    ImGui::TextWrapped("Built with open-source libraries and sound emulation cores:");
                    ImGui::Spacing();

                    ImGui::BulletText("libvgm / VGMPly: Valley Bell and contributors");
                    ImGui::BulletText("Chip Emulation: Nuked, ymfm, emu2413, SameBoy, and MAME team");
                    ImGui::BulletText("Dear ImGui: Omar Cornut");
                    ImGui::BulletText("SDL2: Simple DirectMedia Layer");
                    ImGui::BulletText("zlib: Gailly & Adler");
                    ImGui::Spacing();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float closeW = 90.0f;
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - closeW) * 0.5f);
            if (ImGui::Button("Close", ImVec2(closeW, 0))) {
                m_showAboutDialog = false;
            }
            ImGui::End();
        }
    }

    // 7. Reset Layout Confirmation Alert Box Modal
    if (m_showResetLayoutDialog) {
        ImGui::OpenPopup("Reset Layout Confirmation");
        m_showResetLayoutDialog = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Reset Layout Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Spacing();
        ImGui::Text("Reset all panels and windows to default layout?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float btnW = 90.0f;
        float availW = ImGui::GetContentRegionAvail().x;
        float startX = (availW - (btnW * 2.0f + 16.0f)) * 0.5f;
        if (startX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

        if (ImGui::Button("Yes", ImVec2(btnW, 0))) {
            m_resetLayoutRequested = true;
            m_showPianoRoll = true;
            m_showChannels = true;
            m_showSongInfo = true;
            m_showTransport = true;
            m_showSpeedWindow = false;
            m_showSampleViewer = true;
            m_showOscilloscopeView = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine(0.0f, 16.0f);
        if (ImGui::Button("No", ImVec2(btnW, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::Spacing();
        ImGui::EndPopup();
    }

    // 8. File Not Found Modal Alert
    if (m_showFileNotFoundAlert) {
        ImGui::OpenPopup("File Not Found##AlertModal");
        m_showFileNotFoundAlert = false;
    }
    if (ImGui::BeginPopupModal("File Not Found##AlertModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.35f, 1.0f), "File Not Found");
        ImGui::Spacing();
        ImGui::Text("The selected file does not exist or has been moved:");
        ImGui::TextDisabled("%s", m_fileNotFoundPath.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float okBtnW = 90.0f;
        float modalAvailW = ImGui::GetContentRegionAvail().x;
        if (modalAvailW > okBtnW) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (modalAvailW - okBtnW) * 0.5f);
        }
        if (ImGui::Button("OK", ImVec2(okBtnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::Spacing();
        ImGui::EndPopup();
    }

    // 9. File Corrupted or Invalid Modal Alert
    if (m_showFileCorruptedAlert) {
        ImGui::OpenPopup("File Corrupted or Invalid##AlertModal");
        m_showFileCorruptedAlert = false;
    }
    if (ImGui::BeginPopupModal("File Corrupted or Invalid##AlertModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.35f, 1.0f), "File Corrupted or Invalid");
        ImGui::Spacing();
        ImGui::Text("Unable to open the selected file:");
        ImGui::TextDisabled("%s", m_corruptedFilePath.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("The file is either not a valid VGM/VGZ audio file or the header data is corrupted.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float okBtnW = 90.0f;
        float modalAvailW = ImGui::GetContentRegionAvail().x;
        if (modalAvailW > okBtnW) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (modalAvailW - okBtnW) * 0.5f);
        }
        if (ImGui::Button("OK", ImVec2(okBtnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::Spacing();
        ImGui::EndPopup();
    }

    // 10. Clear Recent Files Confirmation Modal
    if (m_showClearRecentDialog) {
        ImGui::OpenPopup("Clear Recent Files Confirmation##Modal");
        m_showClearRecentDialog = false;
    }
    if (ImGui::BeginPopupModal("Clear Recent Files Confirmation##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Spacing();
        ImGui::Text("Are you sure you want to clear the recent files list?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float btnW = 90.0f;
        float availW = ImGui::GetContentRegionAvail().x;
        float startX = (availW - (btnW * 2.0f + 16.0f)) * 0.5f;
        if (startX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

        if (ImGui::Button("Yes", ImVec2(btnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            m_recentFiles.clear();
            SaveRecentFiles();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine(0.0f, 16.0f);
        if (ImGui::Button("Cancel", ImVec2(btnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::Spacing();
        ImGui::EndPopup();
    }
}

void App::Shutdown() {
    m_audioEngine.Shutdown();

    if (m_glContext) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();

        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
    m_running = false;
}

} // namespace vgmstudio
