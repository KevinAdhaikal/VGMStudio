#include "file_dialog.hpp"
#include <imgui.h>
#include <algorithm>
#include <iostream>

namespace vgmstudio {

static std::string FormatFileSize(size_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
        return std::string(buf);
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0 * 1024.0));
        return std::string(buf);
    }
}

// =============================================================================
// Custom Vector Icons Drawn with ImDrawList (NO EMOJIS, NO UNICODE TEXT)
// =============================================================================

static void DrawFolderIcon(ImDrawList* dl, ImVec2 pos) {
    // 16x13 folder icon
    float w = 16.0f;
    float h = 13.0f;

    ImU32 colTab  = IM_COL32(230, 160, 45, 255); // #E6A02D (folder tab)
    ImU32 colBody = IM_COL32(245, 185, 65, 255); // #F5B941 (folder body)
    ImU32 colEdge = IM_COL32(185, 125, 25, 255);

    // Tab (top-left)
    dl->AddRectFilled(pos, ImVec2(pos.x + 7.0f, pos.y + 4.5f), colTab, 1.5f);

    // Main Body
    ImVec2 bodyMin(pos.x, pos.y + 3.0f);
    ImVec2 bodyMax(pos.x + w, pos.y + h);
    dl->AddRectFilled(bodyMin, bodyMax, colBody, 2.0f);
    dl->AddRect(bodyMin, bodyMax, colEdge, 2.0f, 0, 1.0f);

    // Top subtle highlight line
    dl->AddLine(ImVec2(pos.x + 1.0f, pos.y + 4.5f), ImVec2(pos.x + w - 1.0f, pos.y + 4.5f), IM_COL32(255, 255, 255, 80));
}

static void DrawParentDirIcon(ImDrawList* dl, ImVec2 pos) {
    // Folder icon with an upward arrow inside
    DrawFolderIcon(dl, pos);

    // Up arrow in center
    float cx = pos.x + 8.0f;
    float cy = pos.y + 8.0f;
    dl->AddTriangleFilled(
        ImVec2(cx, cy - 3.5f),
        ImVec2(cx - 3.0f, cy + 1.0f),
        ImVec2(cx + 3.0f, cy + 1.0f),
        IM_COL32(40, 45, 55, 255)
    );
    dl->AddRectFilled(
        ImVec2(cx - 1.0f, cy + 1.0f),
        ImVec2(cx + 1.0f, cy + 3.5f),
        IM_COL32(40, 45, 55, 255)
    );
}

static void DrawVgmFileIcon(ImDrawList* dl, ImVec2 pos) {
    // 13x15 document icon with folded top-right corner and music note
    float w = 13.0f;
    float h = 15.0f;

    ImU32 colBg   = IM_COL32(32, 40, 52, 255);   // Dark slate document body
    ImU32 colEdge = IM_COL32(0, 173, 181, 255);  // Teal border
    ImU32 colFold = IM_COL32(0, 215, 225, 255);  // Folded corner
    ImU32 colNote = IM_COL32(79, 209, 197, 255); // Bright teal note

    float fold = 4.0f;

    // 5-point polygon for folded page
    ImVec2 pts[5] = {
        ImVec2(pos.x, pos.y),
        ImVec2(pos.x + w - fold, pos.y),
        ImVec2(pos.x + w, pos.y + fold),
        ImVec2(pos.x + w, pos.y + h),
        ImVec2(pos.x, pos.y + h)
    };
    dl->AddConvexPolyFilled(pts, 5, colBg);
    dl->AddPolyline(pts, 5, colEdge, ImDrawFlags_Closed, 1.0f);

    // Fold triangle
    dl->AddTriangleFilled(
        ImVec2(pos.x + w - fold, pos.y),
        ImVec2(pos.x + w - fold, pos.y + fold),
        ImVec2(pos.x + w, pos.y + fold),
        colFold
    );

    // Music note inside document
    float nx = pos.x + 4.5f;
    float ny = pos.y + 10.5f;
    dl->AddCircleFilled(ImVec2(nx, ny), 1.8f, colNote);
    dl->AddLine(ImVec2(nx + 1.5f, ny), ImVec2(nx + 1.5f, ny - 5.0f), colNote, 1.2f);
    dl->AddLine(ImVec2(nx + 1.5f, ny - 5.0f), ImVec2(nx + 4.0f, ny - 3.5f), colNote, 1.2f);
}

// =============================================================================
// FileDialog Implementation
// =============================================================================

FileDialog::FileDialog() {
    m_currentDir = std::filesystem::current_path().string();
}

void FileDialog::Open(const std::string& startDir) {
    m_isOpen = true;
    m_openRequested = true;
    m_selectedPath.clear();
    m_selectedName.clear();
    m_isSelectedDir = false;
    m_hasSelectedFile = false;
    m_selectedIdx = -1;
    m_previewInfo = VgmInfo();

    if (!m_currentDir.empty() && std::filesystem::exists(m_currentDir)) {
        RefreshDirectory();
    } else if (!startDir.empty() && std::filesystem::exists(startDir)) {
        SetDirectory(startDir);
    } else {
        SetDirectory(std::filesystem::current_path().string());
    }
}

void FileDialog::Close() {
    m_isOpen = false;
}

void FileDialog::SetDirectory(const std::string& dir) {
    try {
        std::filesystem::path p(dir);
        if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
            m_currentDir = std::filesystem::canonical(p).string();
            RefreshDirectory();
        }
    } catch (const std::exception& e) {
        std::cerr << "[FileDialog] Error setting directory: " << e.what() << std::endl;
    }
}

void FileDialog::RefreshDirectory() {
    m_items.clear();
    m_selectedIdx = -1;
    m_isSelectedDir = false;
    m_hasSelectedFile = false;
    m_selectedPath.clear();
    m_selectedName.clear();
    m_previewInfo = VgmInfo();

    std::filesystem::path cur(m_currentDir);

    // Add parent directory ".." if not root
    if (cur.has_parent_path() && cur.parent_path() != cur) {
        FileItem parentItem;
        parentItem.isDir = true;
        parentItem.isParent = true;
        parentItem.name = "..";
        parentItem.path = cur.parent_path().string();
        m_items.push_back(parentItem);
    }

    std::vector<FileItem> dirItems;
    std::vector<FileItem> fileItems;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(cur)) {
            std::string filename = entry.path().filename().string();
            if (filename.empty() || filename[0] == '.') continue; // Skip hidden

            if (entry.is_directory()) {
                FileItem item;
                item.isDir = true;
                item.isParent = false;
                item.name = filename;
                item.path = entry.path().string();
                dirItems.push_back(item);
            } else if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                // Only show .vgm and .vgz files
                if (ext == ".vgm" || ext == ".vgz") {
                    FileItem item;
                    item.isDir = false;
                    item.isParent = false;
                    item.name = filename;
                    item.path = entry.path().string();
                    item.sizeBytes = entry.file_size();
                    fileItems.push_back(item);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[FileDialog] Directory iterate error: " << e.what() << std::endl;
    }

    // Sort alphabetically
    std::sort(dirItems.begin(), dirItems.end(), [](const FileItem& a, const FileItem& b) {
        return a.name < b.name;
    });
    std::sort(fileItems.begin(), fileItems.end(), [](const FileItem& a, const FileItem& b) {
        return a.name < b.name;
    });

    for (auto& d : dirItems) m_items.push_back(d);
    for (auto& f : fileItems) m_items.push_back(f);
}

void FileDialog::SelectFile(const std::string& path) {
    m_selectedPath = path;
    m_previewInfo = InspectVgmFile(path);
}

bool FileDialog::Render(std::string& outSelectedFile) {
    if (!m_isOpen) return false;

    if (m_openRequested) {
        ImGui::OpenPopup("Open VGM/VGZ File##Dialog");
        m_openRequested = false;
    }

    bool confirmed = false;

    ImGui::SetNextWindowSize(ImVec2(840, 530), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::BeginPopupModal("Open VGM/VGZ File##Dialog", &m_isOpen, flags)) {
        // Toolbar: Directory path and quick navigation buttons (vector styled, NO emojis)
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
        if (ImGui::Button(" Up ")) {
            std::filesystem::path cur(m_currentDir);
            if (cur.has_parent_path()) {
                SetDirectory(cur.parent_path().string());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(" Refresh ")) {
            RefreshDirectory();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Location: %s", m_currentDir.c_str());
        ImGui::PopStyleVar();

        ImGui::Separator();

        // Left Pane: File & Folder List (58% width)
        // Right Pane: VGM Information Preview (42% width)
        float leftWidth = ImGui::GetContentRegionAvail().x * 0.58f;
        float rightWidth = ImGui::GetContentRegionAvail().x - leftWidth - 12.0f;
        float listHeight = ImGui::GetContentRegionAvail().y - 48.0f;

        // --- LEFT PANE: File & Folder Table ---
        ImGui::BeginChild("FileListPane", ImVec2(leftWidth, listHeight), true);
        if (ImGui::BeginTable("FilesTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            for (int i = 0; i < (int)m_items.size(); ++i) {
                const auto& item = m_items[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                bool isSelected = (m_selectedIdx == i);

                // Position for drawing custom vector icon
                ImVec2 iconPos = ImGui::GetCursorScreenPos();
                iconPos.y += 2.0f; // Center vertically with text

                if (item.isParent) {
                    DrawParentDirIcon(drawList, iconPos);
                } else if (item.isDir) {
                    DrawFolderIcon(drawList, iconPos);
                } else {
                    DrawVgmFileIcon(drawList, iconPos);
                }

                // Indent text past the 16px icon + spacing
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24.0f);

                // Display name with distinct colors
                if (item.isDir) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.78f, 0.40f, 1.0f)); // Golden amber for folders
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.94f, 0.98f, 1.0f)); // Crisp clean white for VGM files
                }

                std::string displayName = item.isParent ? ".. (Parent Directory)" : item.name;
                std::string selectableId = displayName + "##item_" + std::to_string(i);

                // SINGLE CLICK: Select item (Windows Explorer style!)
                // DOUBLE CLICK: If folder -> Enter; If file -> Open!
                if (ImGui::Selectable(selectableId.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_selectedIdx = i;
                    m_selectedPath = item.path;
                    m_selectedName = item.isParent ? ".." : item.name;

                    if (item.isDir) {
                        m_isSelectedDir = true;
                        m_hasSelectedFile = false;
                        m_previewInfo = VgmInfo();
                    } else {
                        m_isSelectedDir = false;
                        m_hasSelectedFile = true;
                        SelectFile(item.path);
                    }

                    // Handle Double Click
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        if (m_isSelectedDir) {
                            // Double-clicked on folder or ".." -> enter directory!
                            SetDirectory(item.path);
                            ImGui::PopStyleColor();
                            break;
                        } else {
                            // Double-clicked on VGM file -> select and close!
                            outSelectedFile = item.path;
                            confirmed = true;
                            m_isOpen = false;
                            ImGui::CloseCurrentPopup();
                            ImGui::PopStyleColor();
                            break;
                        }
                    }
                }
                ImGui::PopStyleColor();

                ImGui::TableNextColumn();
                if (item.isDir) {
                    ImGui::TextDisabled("<DIR>");
                } else {
                    ImGui::TextDisabled("%s", FormatFileSize(item.sizeBytes).c_str());
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // --- RIGHT PANE: Song Information Preview ---
        ImGui::BeginChild("VgmInfoPreviewPane", ImVec2(rightWidth, listHeight), true);
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.88f, 1.0f), "Song Preview");
        ImGui::Separator();

        if (m_hasSelectedFile && m_previewInfo.valid) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.88f, 0.92f, 1.0f));
            ImGui::TextWrapped("Title: %s", m_previewInfo.title.empty() ? "(Untitled Track)" : m_previewInfo.title.c_str());
            ImGui::PopStyleColor();
            if (!m_previewInfo.titleJp.empty()) {
                ImGui::TextDisabled("Title (JP): %s", m_previewInfo.titleJp.c_str());
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Game:");
            ImGui::SameLine(70);
            ImGui::TextWrapped("%s", m_previewInfo.game.empty() ? "-" : m_previewInfo.game.c_str());

            ImGui::TextDisabled("System:");
            ImGui::SameLine(70);
            ImGui::TextWrapped("%s", m_previewInfo.system.empty() ? "-" : m_previewInfo.system.c_str());

            ImGui::TextDisabled("Author:");
            ImGui::SameLine(70);
            ImGui::TextWrapped("%s", m_previewInfo.author.empty() ? "-" : m_previewInfo.author.c_str());

            ImGui::TextDisabled("Date:");
            ImGui::SameLine(70);
            ImGui::Text("%s", m_previewInfo.date.empty() ? "-" : m_previewInfo.date.c_str());

            ImGui::Separator();
            ImGui::TextDisabled("Length:");
            ImGui::SameLine(70);
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.88f, 1.0f), "%s", m_previewInfo.GetFormattedDuration().c_str());

            ImGui::TextDisabled("Loop:");
            ImGui::SameLine(70);
            if (m_previewInfo.hasLoop) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", m_previewInfo.GetFormattedLoopDuration().c_str());
            } else {
                ImGui::TextDisabled("No Loop");
            }

            ImGui::TextDisabled("Size:");
            ImGui::SameLine(70);
            ImGui::Text("%s", FormatFileSize(m_previewInfo.fileSizeBytes).c_str());

            ImGui::TextDisabled("Format:");
            ImGui::SameLine(70);
            char verBuf[16];
            snprintf(verBuf, sizeof(verBuf), "VGM %X.%02X", (m_previewInfo.version >> 8) & 0xFF, m_previewInfo.version & 0xFF);
            ImGui::Text("%s", verBuf);

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.88f, 1.0f), "Sound Chips:");
            if (m_previewInfo.chips.empty()) {
                ImGui::TextDisabled("Standard VGM Sound");
            } else {
                for (const auto& chip : m_previewInfo.chips) {
                    double mhz = (double)chip.clock / 1000000.0;
                    if (chip.clock > 0) {
                        ImGui::BulletText("%s (%.2f MHz)", chip.name.c_str(), mhz);
                    } else {
                        ImGui::BulletText("%s", chip.name.c_str());
                    }
                }
            }
        } else if (m_hasSelectedFile && !m_previewInfo.valid) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to parse VGM header!");
        } else {
            ImGui::Spacing();
            ImGui::TextDisabled("Select a .vgm or .vgz file to preview.");
        }

        ImGui::EndChild();

        // =====================================================================
        // Bottom Action Bar (Windows File Explorer Style)
        // =====================================================================
        ImGui::Separator();

        // Keyboard Shortcut: Enter triggers Open action on selected item
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && m_selectedIdx >= 0) {
            if (m_isSelectedDir) {
                SetDirectory(m_selectedPath);
            } else {
                outSelectedFile = m_selectedPath;
                confirmed = true;
                m_isOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("File name:");
        ImGui::SameLine();

        // Read-only filename field reflecting the selected item
        float openW = ImGui::CalcTextSize("Open").x + 32.0f;
        float cancelW = ImGui::CalcTextSize("Cancel").x + 32.0f;
        float bottomBtnsW = openW + cancelW + 16.0f;

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bottomBtnsW);
        char nameBuf[256];
        snprintf(nameBuf, sizeof(nameBuf), "%s", m_selectedIdx >= 0 ? m_selectedName.c_str() : "");
        ImGui::InputText("##SelectedFileName", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_ReadOnly);

        ImGui::SameLine();
        bool hasSelection = (m_selectedIdx >= 0);
        if (!hasSelection) {
            ImGui::BeginDisabled();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16, 5));
        if (ImGui::Button("Open", ImVec2(openW, 0))) {
            if (m_isSelectedDir) {
                SetDirectory(m_selectedPath);
            } else {
                outSelectedFile = m_selectedPath;
                confirmed = true;
                m_isOpen = false;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!hasSelection) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(cancelW, 0))) {
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleVar();

        ImGui::EndPopup();
    }

    return confirmed;
}

} // namespace vgmstudio
