#include "disassembly_view.hpp"
#include <imgui_internal.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace vgmstudio {

DisassemblyView::DisassemblyView() = default;
DisassemblyView::~DisassemblyView() = default;

static bool StrCaseContains(const char* haystack, const char* needle) {
    if (!needle || !needle[0]) return true;
    if (!haystack) return false;
    
    // Quick lowercase comparison
    std::string h(haystack);
    std::string n(needle);
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    return h.find(n) != std::string::npos;
}

void DisassemblyView::UpdateFilter(const VgmDisassembly& disasm) {
    const auto& instructions = disasm.GetInstructions();
    const auto& detectedChips = disasm.GetDetectedChips();
    const auto& detectedChannels = disasm.GetDetectedChannels();

    std::string key = std::string(m_searchFilter) + "|" + 
                      std::to_string(m_categoryFilter) + "|" + 
                      std::to_string(m_chipFilter) + "|" + 
                      std::to_string(m_channelFilter) + "|" + 
                      std::to_string(instructions.size());

    if (key == m_lastFilterKey && instructions.size() == m_lastTotalInstructions) {
        return; // Cache is valid
    }

    m_lastFilterKey = key;
    m_lastTotalInstructions = instructions.size();
    m_filteredIndices.clear();
    m_filteredIndices.reserve(instructions.size());

    std::string targetChipName;
    if (m_chipFilter > 0 && m_chipFilter <= (int)detectedChips.size()) {
        targetChipName = detectedChips[m_chipFilter - 1];
    }

    std::string targetChannelName;
    if (m_channelFilter > 0 && m_channelFilter <= (int)detectedChannels.size()) {
        targetChannelName = detectedChannels[m_channelFilter - 1];
    }

    bool hasSearch = (m_searchFilter[0] != '\0');

    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto& inst = instructions[i];

        // 1. Category Filter
        if (m_categoryFilter == 1) { // Sound Chips Only (no waits)
            if (inst.isWait) continue;
        } else if (m_categoryFilter == 2) { // FM Only
            if (inst.category != InstCategory::FM) continue;
        } else if (m_categoryFilter == 3) { // PSG / Square Only
            if (inst.category != InstCategory::PSG_Square) continue;
        } else if (m_categoryFilter == 4) { // Delays Only
            if (!inst.isWait) continue;
        } else if (m_categoryFilter == 5) { // Key Events
            if (!inst.isKeyOn && !inst.isKeyOff) continue;
        } else if (m_categoryFilter == 6) { // Data Block
            if (inst.category != InstCategory::DataBlock) continue;
        }

        // 2. Chip Filter
        if (!targetChipName.empty()) {
            if (std::string(inst.chip).find(targetChipName) == std::string::npos) {
                continue;
            }
        }

        // 3. Channel Filter
        if (!targetChannelName.empty()) {
            if (std::string(inst.channel) != targetChannelName) {
                continue;
            }
        }

        // 4. Search Text Filter
        if (hasSearch) {
            char hexBuf[16];
            std::snprintf(hexBuf, sizeof(hexBuf), "%02X", inst.cmd);
            char ofsBuf[16];
            std::snprintf(ofsBuf, sizeof(ofsBuf), "%06X", inst.fileOffset);

            bool match = StrCaseContains(inst.desc, m_searchFilter) ||
                         StrCaseContains(inst.chip, m_searchFilter) ||
                         StrCaseContains(inst.channel, m_searchFilter) ||
                         StrCaseContains(inst.hexBytes, m_searchFilter) ||
                         StrCaseContains(hexBuf, m_searchFilter) ||
                         StrCaseContains(ofsBuf, m_searchFilter);
            if (!match) continue;
        }

        m_filteredIndices.push_back((int)i);
    }
}

void DisassemblyView::Render(AudioEngine& audioEngine, bool* pOpen) {
    if (pOpen && !*pOpen) return;

    if (m_focusRequested) {
        ImGui::SetNextWindowFocus();
        m_focusRequested = false;
    }

    if (!ImGui::Begin("VGM Instruction Disassembly", pOpen)) {
        ImGui::End();
        return;
    }

    const VgmDisassembly& disasm = audioEngine.GetDisassembly();
    const auto& instructions = disasm.GetInstructions();
    const auto& detectedChips = disasm.GetDetectedChips();

    if (instructions.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No VGM instructions loaded. Open a .vgm or .vgz file to inspect the instruction stream.");
        ImGui::End();
        return;
    }

    UpdateFilter(disasm);

    uint32_t curSample = audioEngine.GetCurSample();
    int activeInstIdx = disasm.FindInstructionIndexAtSample(curSample);

    // Find closest index in m_filteredIndices for active instruction
    int activeVisibleRow = -1;
    if (!m_filteredIndices.empty() && activeInstIdx >= 0) {
        auto it = std::upper_bound(m_filteredIndices.begin(), m_filteredIndices.end(), activeInstIdx);
        if (it != m_filteredIndices.begin()) {
            activeVisibleRow = (int)std::distance(m_filteredIndices.begin(), it - 1);
        } else {
            activeVisibleRow = 0;
        }
    }

    // --- Toolbar ---
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));

    // Follow Playhead toggle
    ImGui::Checkbox("Follow Playhead", &m_followPlayhead);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically scroll to the instruction currently playing");

    ImGui::SameLine();

    // Jump to Current button
    if (ImGui::Button("Jump to Current")) {
        m_requestScrollToCurrent = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scroll the view to the currently playing instruction");

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Category Filter Combo
    ImGui::SetNextItemWidth(140.0f);
    const char* categories[] = {
        "All Instructions",
        "Chips (Hide Waits)",
        "FM Chips Only",
        "PSG / Square Only",
        "Delays / Waits",
        "Key On / Off Events",
        "Data Blocks"
    };
    if (ImGui::Combo("##CategoryFilter", &m_categoryFilter, categories, IM_ARRAYSIZE(categories))) {
        m_lastFilterKey.clear(); // Invalidate cache
    }

    ImGui::SameLine();

    // Chip Filter Combo
    std::vector<const char*> chipNames;
    chipNames.push_back("All Chips");
    for (const auto& ch : detectedChips) {
        chipNames.push_back(ch.c_str());
    }
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::Combo("##ChipFilter", &m_chipFilter, chipNames.data(), (int)chipNames.size())) {
        m_lastFilterKey.clear();
    }

    ImGui::SameLine();

    // Channel Filter Combo
    const auto& detectedChannels = disasm.GetDetectedChannels();
    std::vector<const char*> chanNames;
    chanNames.push_back("All Channels");
    for (const auto& chn : detectedChannels) {
        chanNames.push_back(chn.c_str());
    }
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::Combo("##ChannelFilter", &m_channelFilter, chanNames.data(), (int)chanNames.size())) {
        m_lastFilterKey.clear();
    }

    ImGui::SameLine();

    // Search input
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputTextWithHint("##SearchDisasm", "Search opcode, reg, desc...", m_searchFilter, sizeof(m_searchFilter))) {
        m_lastFilterKey.clear();
    }

    ImGui::SameLine();

    // Copy to Clipboard button
    if (ImGui::Button("Copy Text")) {
        std::stringstream ss;
        ss << "#\tOffset\tTime\tSample\tBytes\tChip\tDescription\n";
        size_t exportMax = std::min(m_filteredIndices.size(), (size_t)10000);
        for (size_t r = 0; r < exportMax; ++r) {
            const auto& inst = instructions[m_filteredIndices[r]];
            double sec = inst.samplePos / 44100.0;
            char tBuf[32];
            std::snprintf(tBuf, sizeof(tBuf), "%02d:%02d.%03d", (int)sec / 60, (int)sec % 60, (int)((sec - (int)sec) * 1000));
            ss << inst.index << "\t0x" << std::hex << inst.fileOffset << std::dec << "\t"
               << tBuf << "\t" << inst.samplePos << "\t"
               << inst.hexBytes << "\t" << inst.chip << "\t" << inst.desc << "\n";
        }
        ImGui::SetClipboardText(ss.str().c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy visible disassembly list to system clipboard (up to 10,000 lines)");

    ImGui::PopStyleVar(2);

    // --- Sub-header: Statistics Bar ---
    ImGui::Spacing();
    char statsStr[256];
    double curSec = curSample / 44100.0;
    std::snprintf(statsStr, sizeof(statsStr), 
                  "Instructions: %zu of %zu  |  Current: #%d (at %02d:%02d.%02d, sample %u)  |  Offset: 0x%06X",
                  m_filteredIndices.size(), instructions.size(),
                  activeInstIdx >= 0 ? activeInstIdx : 0,
                  (int)curSec / 60, (int)curSec % 60, (int)((curSec - (int)curSec) * 100),
                  curSample,
                  activeInstIdx >= 0 && activeInstIdx < (int)instructions.size() ? instructions[activeInstIdx].fileOffset : disasm.GetDataStartOffset());
    ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.92f, 1.0f), "%s", statsStr);

    ImGui::Separator();

    // --- Instruction Stream Table ---
    ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | 
                                 ImGuiTableFlags_Borders | 
                                 ImGuiTableFlags_Resizable | 
                                 ImGuiTableFlags_Reorderable | 
                                 ImGuiTableFlags_Hideable | 
                                 ImGuiTableFlags_ScrollY;

    float availH = ImGui::GetContentRegionAvail().y;
    if (availH < 80.0f) availH = 80.0f;

    // Handle auto-scroll before beginning table
    bool shouldScroll = false;
    if (m_requestScrollToCurrent) {
        shouldScroll = true;
        m_requestScrollToCurrent = false;
    } else if (m_followPlayhead && audioEngine.IsPlaying()) {
        if (activeVisibleRow != m_lastTrackedInst) {
            m_lastTrackedInst = activeVisibleRow;
            shouldScroll = true;
        }
    }

    if (ImGui::BeginTable("DisassemblyTable", 8, tableFlags, ImVec2(0.0f, availH))) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableSetupColumn("Chip / Type", ImGuiTableColumnFlags_WidthFixed, 95.0f);
        ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Disassembly / Description", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        if (shouldScroll && activeVisibleRow >= 0) {
            float targetScrollY = (float)activeVisibleRow * rowHeight - availH * 0.35f;
            if (targetScrollY < 0.0f) targetScrollY = 0.0f;
            ImGui::SetScrollY(targetScrollY);
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)m_filteredIndices.size(), rowHeight);
        if (activeVisibleRow >= 0 && activeVisibleRow < (int)m_filteredIndices.size()) {
            clipper.IncludeItemByIndex(activeVisibleRow);
        }

        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                int instIdx = m_filteredIndices[r];
                const auto& inst = instructions[instIdx];
                bool isCurrent = (r == activeVisibleRow);

                ImGui::TableNextRow(0, rowHeight);
                ImGui::PushID(instIdx);

                // Row highlighting
                if (isCurrent) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(0, 140, 180, 85));
                } else if (inst.isKeyOn) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(20, 80, 45, 45));
                } else if (inst.category == InstCategory::DataBlock) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(80, 60, 20, 40));
                }

                // Col 0: Index #
                ImGui::TableSetColumnIndex(0);
                if (isCurrent) {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "> %u", inst.index);
                } else {
                    ImGui::TextDisabled("%u", inst.index);
                }

                // Col 1: File Offset (Hex)
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(0.70f, 0.78f, 0.88f, 1.0f), "0x%06X", inst.fileOffset);

                // Col 2: Time (mm:ss.ms)
                ImGui::TableSetColumnIndex(2);
                double sec = inst.samplePos / 44100.0;
                int m = (int)sec / 60;
                int s = (int)sec % 60;
                int ms = (int)((sec - (int)sec) * 1000);
                ImGui::Text("%02d:%02d.%03d", m, s, ms);

                // Col 3: Sample
                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%u", inst.samplePos);

                // Col 4: Hex Bytes
                ImGui::TableSetColumnIndex(4);
                ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.55f, 1.0f), "%s", inst.hexBytes);

                // Col 5: Chip / Type
                ImGui::TableSetColumnIndex(5);
                if (inst.isWait) {
                    ImGui::TextDisabled("%s", inst.chip);
                } else if (inst.category == InstCategory::FM) {
                    ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.95f, 1.0f), "%s", inst.chip);
                } else if (inst.category == InstCategory::PSG_Square) {
                    ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "%s", inst.chip);
                } else if (inst.category == InstCategory::DataBlock) {
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "%s", inst.chip);
                } else {
                    ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.95f, 1.0f), "%s", inst.chip);
                }

                // Col 6: Channel
                ImGui::TableSetColumnIndex(6);
                if (std::strcmp(inst.channel, "Timing") == 0) {
                    ImGui::TextDisabled("---");
                } else if (std::strstr(inst.channel, "DAC")) {
                    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.2f, 1.0f), "%s", inst.channel);
                } else if (std::strstr(inst.channel, "Noise")) {
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.85f, 1.0f), "%s", inst.channel);
                } else if (std::strcmp(inst.channel, "Global") == 0) {
                    ImGui::TextDisabled("Global");
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.65f, 1.0f), "%s", inst.channel);
                }

                // Col 7: Disassembly / Description (Selectable for clicking to seek)
                ImGui::TableSetColumnIndex(7);
                ImVec4 descCol(0.92f, 0.94f, 0.97f, 1.0f);
                if (inst.isKeyOn) {
                    descCol = ImVec4(0.35f, 1.0f, 0.55f, 1.0f);
                } else if (inst.isKeyOff) {
                    descCol = ImVec4(0.95f, 0.45f, 0.45f, 1.0f);
                } else if (inst.isWait) {
                    descCol = ImVec4(0.65f, 0.72f, 0.82f, 0.9f);
                }

                ImGuiSelectableFlags selFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
                if (ImGui::Selectable(inst.desc, isCurrent, selFlags)) {
                    // Click to seek directly to this instruction timestamp
                    audioEngine.SeekToSample(inst.samplePos);
                    m_requestScrollToCurrent = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Click to seek playback to %02d:%02d.%03d (Sample %u)", m, s, ms, inst.samplePos);
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace vgmstudio
