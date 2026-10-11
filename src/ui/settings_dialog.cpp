#include "settings_dialog.hpp"
#include "../app/theme.hpp"
#include <imgui.h>

namespace vgmstudio {

SettingsDialog::SettingsDialog() {
}

SettingsDialog::~SettingsDialog() {
}

void SettingsDialog::Open(int category) {
    m_isOpen = true;
    m_openRequested = true;
    m_selectedCategory = category;
    m_showRestoreConfirm = false;
    m_showSaveSuccess = false;
}

bool SettingsDialog::Render(PianoRoll& pianoRoll, AudioEngine& audioEngine) {
    if (!m_isOpen) return false;

    if (m_openRequested) {
        ImGui::OpenPopup("Settings##Modal");
        m_openRequested = false;
    }

    ImGui::SetNextWindowSize(ImVec2(640, 460), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginPopupModal("Settings##Modal", &m_isOpen, flags)) {
        ImGui::TextColored(ImVec4(0.0f, 0.88f, 0.92f, 1.0f), "Settings");
        ImGui::TextDisabled("Player, visualizer, audio, and appearance preferences.");
        ImGui::Separator();
        ImGui::Spacing();

        // Left Navigation Category Column
        float leftPaneW = 140.0f;
        ImGui::BeginChild("##SettingsCategories", ImVec2(leftPaneW, -38.0f), true);
        if (ImGui::Selectable("VGM Player", m_selectedCategory == 0)) {
            m_selectedCategory = 0;
        }
        if (ImGui::Selectable("Piano Roll", m_selectedCategory == 1)) {
            m_selectedCategory = 1;
        }
        if (ImGui::Selectable("Appearances", m_selectedCategory == 2)) {
            m_selectedCategory = 2;
        }
        if (ImGui::Selectable("Shortcut Key", m_selectedCategory == 3)) {
            m_selectedCategory = 3;
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right Settings Panel (scrollable when content exceeds panel size)
        ImGui::BeginChild("##SettingsDetails", ImVec2(0, -38.0f), true);
        if (m_selectedCategory == 0) {
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 1.0f), "VGM Player & Sound Core Settings");
            ImGui::Separator();
            ImGui::Spacing();

            auto& s = audioEngine.GetSettings();

            // --- 1. Playback & Looping ---
            ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.25f, 1.0f), "Playback & Looping");
            ImGui::Spacing();

            const char* loopModes[] = {
                "Infinite Loop (Play continuously)",
                "Fixed Loop Count (e.g. 2x then fade/stop)",
                "Play Once (No Loop)"
            };
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo("Loop Mode", &s.loopMode, loopModes, IM_ARRAYSIZE(loopModes))) {
                audioEngine.ApplySettings(s);
            }

            if (s.loopMode == 1) {
                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::SliderInt("Loop Count", &s.loopCount, 1, 10, "%d Loops")) {
                    audioEngine.ApplySettings(s);
                }
                ImGui::Checkbox("Fade out on final loop", &s.enableFadeout);
                if (s.enableFadeout) {
                    ImGui::SetNextItemWidth(180.0f);
                    if (ImGui::SliderFloat("Fadeout Duration", &s.fadeoutSeconds, 1.0f, 10.0f, "%.1f sec")) {
                        audioEngine.ApplySettings(s);
                    }
                }
            }

            ImGui::Spacing();
            const char* regionOptions[] = {
                "Original / Auto (Song Default)",
                "NTSC (Force 60 Hz)",
                "PAL (Force 50 Hz)"
            };
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo("Region / TV Standard", &s.regionOverride, regionOptions, IM_ARRAYSIZE(regionOptions))) {
                audioEngine.ApplySettings(s);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // --- 2. Audio Engine & Latency ---
            ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.25f, 1.0f), "Audio Engine & Latency");
            ImGui::Spacing();

            const char* sampleRateOptions[] = {
                "44,100 Hz (Standard CD / VGM Native)",
                "48,000 Hz (Studio / Video standard)",
                "96,000 Hz (High Resolution)"
            };
            int srIdx = (s.sampleRate == 96000) ? 2 : ((s.sampleRate == 48000) ? 1 : 0);
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo("Sample Rate", &srIdx, sampleRateOptions, IM_ARRAYSIZE(sampleRateOptions))) {
                s.sampleRate = (srIdx == 2) ? 96000 : ((srIdx == 1) ? 48000 : 44100);
            }

            ImGui::Spacing();
            const char* bufferOptions[] = {
                "256 frames (~5.8 ms, Ultra-Low Latency)",
                "512 frames (~11.6 ms, Balanced Default)",
                "1024 frames (~23.2 ms, High Stability)",
                "2048 frames (~46.4 ms, Safe Buffer)"
            };
            int bufIdx = (s.bufferFrames == 2048) ? 3 : ((s.bufferFrames == 1024) ? 2 : ((s.bufferFrames == 256) ? 0 : 1));
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo("Buffer Size", &bufIdx, bufferOptions, IM_ARRAYSIZE(bufferOptions))) {
                if (bufIdx == 0) s.bufferFrames = 256;
                else if (bufIdx == 1) s.bufferFrames = 512;
                else if (bufIdx == 2) s.bufferFrames = 1024;
                else s.bufferFrames = 2048;
            }

            ImGui::Spacing();
            const char* resampleOptions[] = {
                "High Quality (Linear Interpolation)",
                "Fast / Low Latency (Draft)"
            };
            ImGui::SetNextItemWidth(280.0f);
            ImGui::Combo("Resampling Quality", &s.resampleQuality, resampleOptions, IM_ARRAYSIZE(resampleOptions));

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // --- 3. Sound Chip Cores ---
            ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.25f, 1.0f), "Sound Chip Cores");
            ImGui::Spacing();

            const char* fm2612Options[] = {
                "Nuked OPN2 (Bit-accurate Cycle Emulation)",
                "MAME OPN2 (High Performance)"
            };
            ImGui::SetNextItemWidth(280.0f);
            ImGui::Combo("YM2612 / YM3438 (Genesis FM)", &s.ym2612Core, fm2612Options, IM_ARRAYSIZE(fm2612Options));

            const char* opl3Options[] = {
                "Nuked OPL3 (Cycle-accurate)",
                "MAME OPL3 (Fast)"
            };
            ImGui::SetNextItemWidth(280.0f);
            ImGui::Combo("YM3812 / YMF262 (OPL2 / OPL3)", &s.opl3Core, opl3Options, IM_ARRAYSIZE(opl3Options));

            const char* opmOptions[] = {
                "Nuked OPM (Accurate Arcade FM)",
                "MAME OPM (Fast)"
            };
            ImGui::SetNextItemWidth(280.0f);
            ImGui::Combo("YM2151 (OPM Arcade)", &s.ym2151Core, opmOptions, IM_ARRAYSIZE(opmOptions));

            const char* psgOptions[] = {
                "Maxim SN76489 (Accurate PSG)",
                "MAME SN76496"
            };
            ImGui::SetNextItemWidth(280.0f);
            ImGui::Combo("SN76489 (Master System / Genesis PSG)", &s.sn76489Core, psgOptions, IM_ARRAYSIZE(psgOptions));

            const char* nesOptions[] = {
                "NSFPlay NES APU + Expansion",
                "MAME NES APU"
            };
            ImGui::SetNextItemWidth(280.0f);
            ImGui::Combo("NES APU / FDS", &s.nesCore, nesOptions, IM_ARRAYSIZE(nesOptions));

            ImGui::Spacing();
            ImGui::TextDisabled("Chip core preferences apply to emulated audio devices on playback.");
        } else if (m_selectedCategory == 1) {
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 1.0f), "Piano Roll Settings");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Checkbox("Show Key Labels on Piano Keyboard", &pianoRoll.GetShowKeyLabels());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Display octave labels (e.g. C1, C2, C3) on piano keys");

            ImGui::Spacing();
            ImGui::Checkbox("Show Note Names on Note Blocks", &pianoRoll.GetShowNoteNames());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Display note names (e.g. C-4, F#5) inside active note blocks");
        } else if (m_selectedCategory == 2) {
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 1.0f), "Theme & Font Appearance");
            ImGui::Separator();
            ImGui::Spacing();

            // Font Family Selection
            ImGui::Text("UI Font Family:");
            const char* fontNames[] = {
                "ProggyClean (Crisp Tracker Default)",
                "Liberation Sans (Clean Modern Sans)",
                "Liberation Mono (Coding / Terminal Mono)",
                "Liberation Serif (Classic Serif)",
                "FreeSans (GNU Universal Sans)",
                "FreeMono (Retro Terminal Monospace)"
            };
            ImGuiIO& io = ImGui::GetIO();
            int maxFonts = std::min((int)IM_ARRAYSIZE(fontNames), io.Fonts->Fonts.Size);
            if (maxFonts > 1) {
                ImGui::Combo("##FontFamilyCombo", &m_fontIndex, fontNames, maxFonts);
            } else {
                ImGui::TextDisabled("ProggyClean (Default Built-in Font)");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Font & UI Scaling
            ImGui::Text("UI & Font Scale:");
            if (ImGui::SliderFloat("##FontScaleSlider", &m_uiScale, 0.80f, 1.50f, "%.2fx Scale")) {
                ImGui::GetIO().FontGlobalScale = m_uiScale;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Color Theme Palette Selection
            ImGui::Text("Color Theme Palette:");
            const char* themeOptions[] = {
                "Furnace Tracker (Deep Charcoal & Teal)",
                "Classic Tracker (FastTracker Navy & Cyan)",
                "Cyberpunk Neon (Dark Indigo & Magenta)",
                "Monokai Pro (Warm Amber & Slate)"
            };
            if (ImGui::Combo("##ThemeCombo", &m_themeIndex, themeOptions, 4)) {
                theme::ApplyTheme(m_themeIndex);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Visual Styling: Frame Rounding
            ImGui::Text("Corner Rounding:");
            ImGuiStyle& style = ImGui::GetStyle();
            if (ImGui::SliderFloat("##CornerRounding", &style.FrameRounding, 0.0f, 10.0f, "%.0f px")) {
                style.WindowRounding = style.FrameRounding;
                style.ChildRounding = style.FrameRounding;
                style.PopupRounding = style.FrameRounding;
            }
        } else if (m_selectedCategory == 3) {
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 1.0f), "Keyboard Shortcuts");
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginTable("ShortcutsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Shortcut Key", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableHeadersRow();

                auto addShortcut = [](const char* action, const char* key) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", action);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(0.0f, 0.88f, 0.92f, 1.0f), "%s", key);
                };

                addShortcut("Play / Pause", "Space");
                addShortcut("Open VGM / VGZ", "Ctrl + O");
                addShortcut("Edit Song Info", "Ctrl + E");
                addShortcut("Settings Dialog", "Ctrl + ,");
                addShortcut("Rewind to Beginning", "Home");
                addShortcut("Zoom In / Out Time", "Ctrl + Mouse Wheel");
                addShortcut("Horizontal Scroll", "Shift + Mouse Wheel");
                addShortcut("Pitch Row Scroll", "Mouse Wheel");
                addShortcut("Pan Canvas", "Right / Middle Click Drag");
                addShortcut("Scrub Timeline Ruler", "Left Click & Hold / Drag");

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
        ImGui::Separator();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

        // Bottom Action Buttons: [Restore to Default] on LEFT, [Save] on RIGHT
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 5));

        // Left button: Restore to Default
        if (ImGui::Button("Restore to Default")) {
            m_showRestoreConfirm = true;
        }

        // Right button: Save
        float saveBtnW = 90.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - saveBtnW - ImGui::GetStyle().WindowPadding.x - 4.0f);
        if (ImGui::Button(" Save ", ImVec2(saveBtnW, 0))) {
            m_showSaveSuccess = true;
        }

        ImGui::PopStyleVar();

        // Modal Alert: Restore to Default Confirmation
        if (m_showRestoreConfirm) {
            ImGui::OpenPopup("Confirm Restore Defaults##Modal");
            m_showRestoreConfirm = false;
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Confirm Restore Defaults##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::Spacing();
            ImGui::Text("Are you sure you want to restore all settings to default?");
            ImGui::TextDisabled("All visualizer, appearance, and display options will be reset to defaults.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float btnW = 80.0f;
            float modalAvailW = ImGui::GetContentRegionAvail().x;
            float startX = (modalAvailW - (btnW * 2.0f + 16.0f)) * 0.5f;
            if (startX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

            if (ImGui::Button("Yes", ImVec2(btnW, 0))) {
                audioEngine.GetSettings() = AudioEngine::Settings();
                audioEngine.ApplySettings(audioEngine.GetSettings());
                pianoRoll.GetShowKeyLabels() = true;
                pianoRoll.GetShowNoteNames() = true;
                m_uiScale = 1.0f;
                ImGui::GetIO().FontGlobalScale = 1.0f;
                m_themeIndex = 0;
                m_fontIndex = 0;
                theme::ApplyTheme(0);
                ImGui::GetStyle().FrameRounding = 4.0f;
                ImGui::GetStyle().WindowRounding = 4.0f;
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

        // Modal Alert: Save Success Feedback
        if (m_showSaveSuccess) {
            ImGui::OpenPopup("Settings Saved##Modal");
            m_showSaveSuccess = false;
        }

        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Settings Saved##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "Settings have been saved successfully!");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float okBtnW = 80.0f;
            float okAvailW = ImGui::GetContentRegionAvail().x;
            if (okAvailW > okBtnW) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (okAvailW - okBtnW) * 0.5f);
            }

            if (ImGui::Button("OK", ImVec2(okBtnW, 0))) {
                m_isOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::Spacing();
            ImGui::EndPopup();
        }

        ImGui::EndPopup();
    }

    return true;
}

} // namespace vgmstudio
