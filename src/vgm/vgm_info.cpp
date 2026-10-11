#include "vgm_info.hpp"
#include "FileLoader.h"
#include "vgmplayer.hpp"
#include "../emu/SoundDevs.h"
#include "../emu/SoundEmu.h"

#include <sys/stat.h>
#include <cstring>
#include <filesystem>
#include <memory>

namespace vgmstudio {

static const char* GetChipFriendlyName(UINT8 devId) {
    const char* name = SndEmu_GetDevName(devId, 0, NULL);
    if (name) return name;
    switch(devId) {
        case DEVID_SN76496: return "SN76489 / SN76496";
        case DEVID_YM2413:  return "YM2413 (OPLL)";
        case DEVID_YM2612:  return "YM2612 (OPN2)";
        case DEVID_YM2151:  return "YM2151 (OPM)";
        case DEVID_SEGAPCM: return "SegaPCM";
        case DEVID_RF5C68:  return "RF5C68";
        case DEVID_YM2203:  return "YM2203 (OPN)";
        case DEVID_YM2608:  return "YM2608 (OPNA)";
        case DEVID_YM2610:  return "YM2610 (OPNB)";
        case DEVID_YM3812:  return "YM3812 (OPL2)";
        case DEVID_YMF262:  return "YMF262 (OPL3)";
        case DEVID_GB_DMG:  return "Game Boy DMG";
        case DEVID_NES_APU: return "NES APU";
        case DEVID_YMW258:  return "MultiPCM";
        case DEVID_MSM6295: return "OKIM6295";
        case DEVID_MSM6258: return "OKIM6258";
        case DEVID_C6280:   return "HuC6280";
        case DEVID_QSOUND:  return "QSound";
        case DEVID_SCSP:    return "SCSP";
        default:            return "Sound Chip";
    }
}

VgmInfo InspectVgmFile(const std::string& path) {
    VgmInfo info;
    info.filePath = path;
    info.fileName = std::filesystem::path(path).filename().string();

    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        info.fileSizeBytes = (size_t)st.st_size;
    }

    DATA_LOADER* dLoad = FileLoader_Init(path.c_str());
    if (!dLoad) {
        return info;
    }
    if (DataLoader_Load(dLoad)) {
        DataLoader_Deinit(dLoad);
        return info;
    }

    auto player = std::make_unique<VGMPlayer>();
    UINT8 err = player->LoadFile(dLoad);
    if (err) {
        DataLoader_Deinit(dLoad);
        return info;
    }

    info.valid = true;
    const VGM_HEADER* hdr = player->GetFileHeader();
    if (hdr) {
        info.version = hdr->fileVer;
        info.totalSamples = hdr->numTicks;
        info.loopSamples = hdr->loopTicks;
        info.hasLoop = (hdr->loopOfs != 0 && hdr->loopTicks > 0);
        info.totalSeconds = (double)hdr->numTicks / 44100.0;
        info.loopSeconds = (double)hdr->loopTicks / 44100.0;
    }

    const char* const* tags = player->GetTags();
    if (tags) {
        for (int i = 0; tags[i] != nullptr; i += 2) {
            std::string key = tags[i];
            std::string val = tags[i + 1] ? tags[i + 1] : "";
            if (val.empty()) continue;

            if (key == "TITLE") info.title = val;
            else if (key == "TITLE-JPN") info.titleJp = val;
            else if (key == "GAME") info.game = val;
            else if (key == "GAME-JPN") info.gameJp = val;
            else if (key == "SYSTEM") info.system = val;
            else if (key == "SYSTEM-JPN") info.systemJp = val;
            else if (key == "ARTIST") info.author = val;
            else if (key == "ARTIST-JPN") info.authorJp = val;
            else if (key == "DATE") info.date = val;
            else if (key == "ENCODED_BY") info.encodedBy = val;
            else if (key == "COMMENT") info.comment = val;
        }
    }

    // Fallback title to filename if missing
    if (info.title.empty()) {
        std::string stem = std::filesystem::path(path).stem().string();
        info.title = stem;
    }

    std::vector<PLR_DEV_INFO> devList;
    player->GetSongDeviceInfo(devList);
    for (const auto& dev : devList) {
        if (dev.parentIdx != (UINT32)-1) continue; // Skip linked devices

        ChipInfo chip;
        if (dev.devDecl && dev.devDecl->name) {
            chip.name = dev.devDecl->name(dev.devCfg);
        } else {
            const char* n = SndEmu_GetDevName(dev.type, 0, dev.devCfg);
            chip.name = n ? n : GetChipFriendlyName(dev.type);
        }
        if (dev.instance > 0) {
            chip.name += " #" + std::to_string(dev.instance + 1);
        }
        if (dev.devCfg) {
            chip.clock = dev.devCfg->clock;
        }
        if (dev.devDecl && dev.devDecl->channelCount) {
            chip.channels = dev.devDecl->channelCount(dev.devCfg);
        } else if (dev.type == DEVID_RF5C68) {
            chip.channels = 8;
        }
        info.chips.push_back(chip);
    }

    player->UnloadFile();
    DataLoader_Deinit(dLoad);
    return info;
}

} // namespace vgmstudio
