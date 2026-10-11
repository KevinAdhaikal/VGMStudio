#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace vgmstudio {

struct ChipInfo {
    std::string name;
    uint32_t clock = 0;
    int channels = 0;
};

struct VgmInfo {
    bool valid = false;
    std::string filePath;
    std::string fileName;
    size_t fileSizeBytes = 0;
    uint32_t version = 0;

    // GD3 metadata
    std::string title;
    std::string titleJp;
    std::string game;
    std::string gameJp;
    std::string system;
    std::string systemJp;
    std::string author;
    std::string authorJp;
    std::string date;
    std::string encodedBy;
    std::string comment;

    // Playback duration
    uint32_t totalSamples = 0;
    uint32_t loopSamples = 0;
    double totalSeconds = 0.0;
    double loopSeconds = 0.0;
    bool hasLoop = false;

    // Active chips
    std::vector<ChipInfo> chips;

    std::string GetFormattedDuration() const {
        int totalSec = (int)totalSeconds;
        int min = totalSec / 60;
        int sec = totalSec % 60;
        int ms = (int)((totalSeconds - totalSec) * 100);
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d.%02d", min, sec, ms);
        return std::string(buf);
    }

    std::string GetFormattedLoopDuration() const {
        if (!hasLoop) return "None";
        int loopSec = (int)loopSeconds;
        int min = loopSec / 60;
        int sec = loopSec % 60;
        int ms = (int)((loopSeconds - loopSec) * 100);
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d.%02d", min, sec, ms);
        return std::string(buf);
    }
};

// Helper function to inspect a VGM/VGZ file and return its info
VgmInfo InspectVgmFile(const std::string& path);

} // namespace vgmstudio
