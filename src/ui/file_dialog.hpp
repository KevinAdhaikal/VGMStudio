#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "../vgm/vgm_info.hpp"

namespace vgmstudio {

struct FileItem {
    bool isDir = false;
    bool isParent = false;
    std::string name;
    std::string path;
    size_t sizeBytes = 0;
};

class FileDialog {
public:
    FileDialog();

    void Open(const std::string& startDir = "samples");
    void Close();
    bool IsOpen() const { return m_isOpen; }

    // Renders the dialog modal. Returns true when a file is confirmed/selected.
    bool Render(std::string& outSelectedFile);

private:
    void SetDirectory(const std::string& dir);
    void RefreshDirectory();
    void SelectFile(const std::string& path);

    bool m_isOpen = false;
    bool m_openRequested = false;
    std::string m_currentDir;
    std::string m_selectedPath;
    std::string m_selectedName;
    bool m_isSelectedDir = false;
    bool m_hasSelectedFile = false;

    std::vector<FileItem> m_items;
    VgmInfo m_previewInfo;
    int m_selectedIdx = -1;
};

} // namespace vgmstudio
