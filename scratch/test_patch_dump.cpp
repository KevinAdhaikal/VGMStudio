#include <iostream>
#include <fstream>
#include <vector>
#include <zlib.h>
#include "../src/vgm/vgm_patch_extractor.hpp"

int main() {
    gzFile gz = gzopen("samples/15 Victory Way (Ending).vgz", "rb");
    if (!gz) {
        std::cerr << "Failed to open file\n";
        return 1;
    }
    std::vector<uint8_t> buffer;
    uint8_t chunk[16384];
    int bytesRead = 0;
    while ((bytesRead = gzread(gz, chunk, sizeof(chunk))) > 0) {
        buffer.insert(buffer.end(), chunk, chunk + bytesRead);
    }
    gzclose(gz);

    vgmstudio::VgmPatchExtractor extractor;
    bool ok = extractor.ExtractFromVgm(buffer.data(), buffer.size());
    std::cout << "Extracted: " << (ok ? "true" : "false") << ", count: " << extractor.GetPresetCount() << "\n";
    std::cout << "All Channels in song (" << extractor.GetAllChannels().size() << "):\n";
    for (const auto& ch : extractor.GetAllChannels()) {
        std::cout << "  - " << ch << "\n";
    }

    std::cout << "\n=== Presets ===\n";
    for (const auto& p : extractor.GetPresets()) {
        std::cout << "Preset #" << p.id << ": " << p.name
                  << "\n  First: " << p.timestampStr
                  << " | Total: " << p.useCount
                  << "\n  Channels: " << p.GetChannelsDetailString() << "\n\n";
    }
    return 0;
}
