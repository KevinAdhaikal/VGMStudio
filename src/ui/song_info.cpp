#include "song_info.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

namespace vgmstudio {

static std::string FormatClockHz(uint32_t clock) {
    if (clock == 0) return "N/A";
    uint32_t clk = clock & 0x7FFFFFFF;
    char buf[32];
    if (clk >= 1000000) {
        std::snprintf(buf, sizeof(buf), "%.3f MHz", (double)clk / 1000000.0);
    } else if (clk >= 1000) {
        std::snprintf(buf, sizeof(buf), "%.2f kHz", (double)clk / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%u Hz", clk);
    }
    return std::string(buf);
}

static std::string FormatDuration(double seconds) {
    int totalSec = (int)seconds;
    int min = totalSec / 60;
    int sec = totalSec % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", min, sec);
    return std::string(buf);
}

SongInfo::SongInfo() {
}

SongInfo::~SongInfo() {
}

void SongInfo::Render(AudioEngine& audioEngine, bool* pOpen) {
    ImGui::SetNextWindowSize(ImVec2(360, 480), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Song Info", pOpen)) {
        ImGui::End();
        return;
    }

    const auto& info = audioEngine.GetCurrentInfo();

    if (!info.valid) {
        ImGui::TextDisabled("No VGM song currently loaded.");
        ImGui::End();
        return;
    }

    ImGui::Spacing();

    // 1. Title Banner
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.88f, 0.92f, 1.0f));
    ImGui::TextWrapped("Title: %s", info.title.empty() ? "(Untitled Track)" : info.title.c_str());
    ImGui::PopStyleColor();

    if (!info.titleJp.empty()) {
        ImGui::TextDisabled("Title (JP): %s", info.titleJp.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Metadata Information Table
    auto renderField = [](const char* label, const std::string& value) {
        if (value.empty()) return;
        ImGui::TextDisabled("%s:", label);
        ImGui::SameLine(90.0f);
        ImGui::TextWrapped("%s", value.c_str());
    };

    renderField("Game", info.game);
    if (!info.gameJp.empty()) {
        renderField("Game (JP)", info.gameJp);
    }
    renderField("System", info.system);
    if (!info.systemJp.empty()) {
        renderField("System (JP)", info.systemJp);
    }
    renderField("Author", info.author);
    if (!info.authorJp.empty()) {
        renderField("Author (JP)", info.authorJp);
    }
    renderField("Date", info.date);
    renderField("Ripper", info.encodedBy);

    if (!info.comment.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Notes:");
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.12f, 0.16f, 1.0f));
        ImGui::BeginChild("NotesBox", ImVec2(0, 60.0f), true);
        ImGui::TextWrapped("%s", info.comment.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 3. Sound Chips
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Sound Chips");

    if (info.chips.empty()) {
        ImGui::TextDisabled("No chip clock data found in header.");
    } else {
        ImGui::Spacing();
        for (size_t c = 0; c < info.chips.size(); ++c) {
            const auto& chip = info.chips[c];
            ImGui::PushID((int)c);

            std::string clkStr = FormatClockHz(chip.clock);
            float clkW = ImGui::CalcTextSize(clkStr.c_str()).x;
            float availW = ImGui::GetContentRegionAvail().x;

            ImGui::Bullet();
            ImGui::TextColored(ImVec4(0.90f, 0.93f, 0.98f, 1.0f), "%s", chip.name.c_str());
            ImGui::SameLine();
            if (availW - clkW > ImGui::GetCursorPosX()) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - clkW - ImGui::GetCursorPosX()));
            }
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 0.9f), "%s", clkStr.c_str());

            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 4. Stream & Format Specs
    ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 1.0f), "Format & Duration");

    ImGui::TextDisabled("VGM Version:");
    ImGui::SameLine(100.0f);
    char verBuf[16];
    std::snprintf(verBuf, sizeof(verBuf), "v%x.%02x", info.version >> 8, info.version & 0xFF);
    ImGui::Text("%s", verBuf);

    ImGui::TextDisabled("Duration:");
    ImGui::SameLine(100.0f);
    char durBuf[32];
    std::snprintf(durBuf, sizeof(durBuf), "%s", FormatDuration(info.totalSeconds).c_str());
    ImGui::Text("%s", durBuf);

    ImGui::TextDisabled("Loop Time:");
    ImGui::SameLine(100.0f);
    if (info.hasLoop) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "%s", 
                           FormatDuration(info.loopSeconds).c_str());
    } else {
        ImGui::TextDisabled("No Loop (One-shot)");
    }

    ImGui::End();
}

} // namespace vgmstudio
