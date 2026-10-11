#include "note_tracker.hpp"
#include "playerbase.hpp"
#include "../emu/SoundDevs.h"
#include "../emu/SoundEmu.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <iostream>

namespace vgmstudio {

static inline uint16_t ReadLE16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t ReadLE32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint32_t ReadRelOfs(const uint8_t* p, uint32_t offset) {
    uint32_t rel = ReadLE32(&p[offset]);
    return rel ? (offset + rel) : 0;
}

static const char* const NOTE_NAMES[12] = {
    "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
};

NoteTracker::NoteTracker() {
}

NoteTracker::~NoteTracker() {
}

void NoteTracker::Clear() {
    m_channels.clear();
    m_notes.clear();
    m_channelNotes.clear();
    m_openNotes.clear();
    m_minMidiNote = 127;
    m_maxMidiNote = 0;
    m_totalSamples = 0;
}

float NoteTracker::FreqToMidi(float freq) {
    if (freq <= 8.0f || freq > 24000.0f) return -1.0f;
    return 69.0f + 12.0f * (float)std::log2((double)freq / 440.0);
}

void NoteTracker::MidiToNoteName(float midiNote, char* outStr, size_t maxLen) {
    if (!outStr || maxLen < 4) return;
    int rounded = (int)std::round(midiNote);
    if (rounded < 0 || rounded > 127) {
        snprintf(outStr, maxLen, "---");
        return;
    }
    int semitone = rounded % 12;
    int octave = (rounded / 12) - 1; // MIDI 60 is C4
    snprintf(outStr, maxLen, "%s%d", NOTE_NAMES[semitone], octave);
}

uint32_t NoteTracker::GetChannelColor(const std::string& chip, const std::string& type, int chIndex) {
    // Return RGBA packed uint32 (ImU32 compatible: 0xAABBGGRR)
    // Custom curated palette inspired by Furnace Tracker
    static const uint32_t FM_COLORS[] = {
        0xFFE0B020, // Cyan 1: R=0x20, G=0xB0, B=0xE0
        0xFFDF802A, // Sky Blue
        0xFFE04050, // Deep Sky
        0xFF80E040, // Aquamarine
        0xFF40DF80, // Teal
        0xFF40B0E0, // Ocean Blue
        0xFF8080E0, // Cornflower
        0xFFB060DF  // Violet Blue
    };

    static const uint32_t PULSE_COLORS[] = {
        0xFF2090F0, // Bright Orange
        0xFF30B0F8, // Coral Gold
        0xFF20D0E8, // Warm Amber
        0xFF4080F8, // Deep Orange
    };

    static const uint32_t TRIANGLE_COLORS[] = {
        0xFF50D060, // Emerald Green
        0xFF70E080, // Spring Green
    };

    static const uint32_t WAVE_COLORS[] = {
        0xFF90D040, // Chartreuse
        0xFF40D090, // Sea Green
    };

    static const uint32_t NOISE_COLORS[] = {
        0xFFB040B0, // Magenta
        0xFF9040C0, // Purple
    };

    static const uint32_t PCM_COLORS[] = {
        0xFF4060E0, // Coral Red
        0xFF6040E0, // Crimson
        0xFF7060D0, // Plum
        0xFF3080E8, // Amber Orange
        0xFF9040D0, // Purple Red
        0xFF4080D8, // Salmon Pink
        0xFF8050C0, // Orchid
        0xFF3090E0  // Golden Coral
    };

    if (type == "FM") {
        return FM_COLORS[chIndex % 8];
    } else if (type == "Square" || type == "Pulse") {
        return PULSE_COLORS[chIndex % 4];
    } else if (type == "Triangle") {
        return TRIANGLE_COLORS[chIndex % 2];
    } else if (type == "Wave") {
        return WAVE_COLORS[chIndex % 2];
    } else if (type == "Noise") {
        return NOISE_COLORS[chIndex % 2];
    } else if (type == "PCM" || type == "DAC") {
        return PCM_COLORS[chIndex % 8];
    }

    // Default pleasant cyan
    return 0xFFDF9020;
}

void NoteTracker::NoteOn(int channelId, uint32_t currentSample, float midiNote, float volume) {
    if (channelId < 0 || channelId >= (int)m_channels.size()) return;
    if (midiNote < 12.0f || midiNote > 120.0f) return;

    OpenNote& op = m_openNotes[channelId];
    if (op.active) {
        // If it's the same pitch (within 0.45 semitone), envelope or volume tick: continue existing note
        if (std::abs(op.midiNote - midiNote) < 0.45f) {
            op.volume = std::max(op.volume, volume);
            return;
        }

        // Close previous note if pitch changed
        uint32_t noteEnd = currentSample;
        if (noteEnd <= op.startSample) noteEnd = op.startSample + 400; // minimum duration

        TrackedNote tn;
        tn.channelId = channelId;
        tn.startSample = op.startSample;
        tn.endSample = noteEnd;
        tn.midiNote = op.midiNote;
        tn.noteNumber = (int)std::round(op.midiNote);
        tn.volume = op.volume;
        MidiToNoteName(tn.midiNote, tn.noteName, sizeof(tn.noteName));
        m_notes.push_back(tn);

        m_minMidiNote = std::min(m_minMidiNote, tn.noteNumber);
        m_maxMidiNote = std::max(m_maxMidiNote, tn.noteNumber);
    }

    op.active = true;
    op.startSample = currentSample;
    op.midiNote = midiNote;
    op.volume = volume;
}

void NoteTracker::NoteOff(int channelId, uint32_t currentSample) {
    if (channelId < 0 || channelId >= (int)m_channels.size()) return;

    OpenNote& op = m_openNotes[channelId];
    if (op.active) {
        uint32_t noteEnd = currentSample;
        if (noteEnd <= op.startSample) noteEnd = op.startSample + 400;

        TrackedNote tn;
        tn.channelId = channelId;
        tn.startSample = op.startSample;
        tn.endSample = noteEnd;
        tn.midiNote = op.midiNote;
        tn.noteNumber = (int)std::round(op.midiNote);
        tn.volume = op.volume;
        MidiToNoteName(tn.midiNote, tn.noteName, sizeof(tn.noteName));
        m_notes.push_back(tn);

        m_minMidiNote = std::min(m_minMidiNote, tn.noteNumber);
        m_maxMidiNote = std::max(m_maxMidiNote, tn.noteNumber);

        op.active = false;
    }
}

void NoteTracker::FlushOpenNotes(uint32_t finalSample) {
    for (size_t ch = 0; ch < m_openNotes.size(); ++ch) {
        OpenNote& op = m_openNotes[ch];
        if (op.active) {
            uint32_t noteEnd = std::max(finalSample, op.startSample + 400);

            TrackedNote tn;
            tn.channelId = (int)ch;
            tn.startSample = op.startSample;
            tn.endSample = noteEnd;
            tn.midiNote = op.midiNote;
            tn.noteNumber = (int)std::round(op.midiNote);
            tn.volume = op.volume;
            MidiToNoteName(tn.midiNote, tn.noteName, sizeof(tn.noteName));
            m_notes.push_back(tn);

            m_minMidiNote = std::min(m_minMidiNote, tn.noteNumber);
            m_maxMidiNote = std::max(m_maxMidiNote, tn.noteNumber);

            op.active = false;
        }
    }
}

bool NoteTracker::ProcessVgm(const uint8_t* vgmData, size_t dataSize, uint32_t sampleRate, PlayerBase* player) {
    Clear();

    if (!vgmData || dataSize < 0x40) return false;
    if (std::memcmp(vgmData, "Vgm ", 4) != 0) return false;

    uint32_t fileVer = ReadLE32(&vgmData[0x08]);
    uint32_t dataOfs = (fileVer >= 0x150) ? ReadRelOfs(vgmData, 0x34) : 0x40;
    if (!dataOfs) dataOfs = 0x40;
    if (dataOfs >= dataSize) return false;

    uint32_t eofOfs = ReadRelOfs(vgmData, 0x04);
    if (!eofOfs || eofOfs > dataSize) eofOfs = (uint32_t)dataSize;

    uint32_t totalSamples = ReadLE32(&vgmData[0x18]);
    m_totalSamples = totalSamples;

    // Detect Sound Chips from VGM Header Clocks
    uint32_t snClk = ReadLE32(&vgmData[0x0C]);
    uint32_t ym2413Clk = ReadLE32(&vgmData[0x10]);
    uint32_t ym2612Clk = (fileVer >= 0x110) ? ReadLE32(&vgmData[0x2C]) : 0;
    uint32_t ym2151Clk = (fileVer >= 0x110) ? ReadLE32(&vgmData[0x30]) : 0;
    uint32_t segaPcmClk = (fileVer >= 0x151 && dataSize >= 0x3C) ? ReadLE32(&vgmData[0x38]) : 0;
    uint32_t rf5c68Clk = (fileVer >= 0x151 && dataSize >= 0x44) ? ReadLE32(&vgmData[0x40]) : 0;
    uint32_t ym2203Clk = (fileVer >= 0x151 && dataSize >= 0x48) ? ReadLE32(&vgmData[0x44]) : 0;
    uint32_t ym2608Clk = (fileVer >= 0x151 && dataSize >= 0x4C) ? ReadLE32(&vgmData[0x48]) : 0;
    uint32_t ym2610Clk = (fileVer >= 0x151 && dataSize >= 0x50) ? ReadLE32(&vgmData[0x4C]) : 0;
    uint32_t ym3812Clk = (fileVer >= 0x151 && dataSize >= 0x54) ? ReadLE32(&vgmData[0x50]) : 0;
    uint32_t ymf262Clk = (fileVer >= 0x151 && dataSize >= 0x60) ? ReadLE32(&vgmData[0x5C]) : 0;
    uint32_t rf5c164Clk = (fileVer >= 0x151 && dataSize >= 0x70) ? ReadLE32(&vgmData[0x6C]) : 0;
    uint32_t ayClk = (fileVer >= 0x151 && dataSize >= 0x78) ? ReadLE32(&vgmData[0x74]) : 0;
    uint32_t gbClk = (fileVer >= 0x161 && dataSize >= 0x84) ? ReadLE32(&vgmData[0x80]) : 0;
    uint32_t nesClk = (fileVer >= 0x161 && dataSize >= 0x88) ? ReadLE32(&vgmData[0x84]) : 0;
    uint32_t multiPcmClk = (fileVer >= 0x161 && dataSize >= 0x8C) ? ReadLE32(&vgmData[0x88]) : 0;
    uint32_t pceClk = (fileVer >= 0x161 && dataSize >= 0xA8) ? ReadLE32(&vgmData[0xA4]) : 0;

    // Check if any chips are indicated, or scan commands to discover chips
    bool hasSN = (snClk > 0);
    bool hasYM2612 = (ym2612Clk > 0);
    bool hasYM2151 = (ym2151Clk > 0);
    bool hasYM2413 = (ym2413Clk > 0);
    bool hasYM2203 = (ym2203Clk > 0);
    bool hasSegaPCM = (segaPcmClk > 0);
    bool hasRF5C = (rf5c68Clk > 0 || rf5c164Clk > 0);
    bool hasYM2608 = (ym2608Clk > 0);
    bool hasYM2610 = (ym2610Clk > 0);
    bool hasNES = (nesClk > 0);
    bool hasGB = (gbClk > 0);
    bool hasAY = (ayClk > 0);
    bool hasMultiPcm = (multiPcmClk > 0);
    bool hasPCE = (pceClk > 0);
    bool hasYM3812 = (ym3812Clk > 0);
    bool hasYMF262 = (ymf262Clk > 0);

    // Fast pre-scan to catch commands if clocks in header were 0
    size_t probePos = dataOfs;
    while (probePos < eofOfs && probePos < dataOfs + 32768) {
        uint8_t cmd = vgmData[probePos];
        if (cmd == 0x66) break;
        if (cmd == 0x50) hasSN = true;
        else if (cmd == 0x51) hasYM2413 = true;
        else if (cmd == 0x52 || cmd == 0x53) hasYM2612 = true;
        else if (cmd == 0x54) hasYM2151 = true;
        else if (cmd == 0x55) hasYM2203 = true;
        else if (cmd == 0x56 || cmd == 0x57) hasYM2608 = true;
        else if (cmd == 0x58 || cmd == 0x59) hasYM2610 = true;
        else if (cmd == 0x5A) hasYM3812 = true;
        else if (cmd == 0x5E || cmd == 0x5F) hasYMF262 = true;
        else if (cmd == 0xA0) hasAY = true;
        else if (cmd == 0xB0 || cmd == 0xB1) hasRF5C = true;
        else if (cmd == 0xB3) hasGB = true;
        else if (cmd == 0xB4) hasNES = true;
        else if (cmd == 0xB5) hasMultiPcm = true;
        else if (cmd == 0xB9) hasPCE = true;
        else if (cmd == 0xC0) hasSegaPCM = true;
        else if (cmd == 0xC1 || cmd == 0xC2) hasRF5C = true;

        // Skip command length
        if (cmd == 0x61) probePos += 3;
        else if (cmd == 0x62 || cmd == 0x63 || (cmd >= 0x70 && cmd <= 0x8F)) probePos += 1;
        else if (cmd >= 0x50 && cmd <= 0x5F) probePos += (cmd == 0x50 ? 2 : 3);
        else if (cmd >= 0xA0 && cmd <= 0xBF) probePos += 3;
        else if (cmd >= 0xC0 && cmd <= 0xDF) probePos += 4;
        else if (cmd >= 0xE0) probePos += 5;
        else if (cmd == 0x67) {
            if (probePos + 7 > eofOfs) break;
            uint32_t bLen = ReadLE32(&vgmData[probePos + 3]) & 0x7FFFFFFF;
            probePos += 7 + bLen;
        } else if (cmd == 0x68) {
            probePos += 12;
        } else if (cmd >= 0x90 && cmd <= 0x95) {
            static const int dacLens[6] = {5, 5, 6, 11, 2, 5};
            probePos += dacLens[cmd - 0x90];
        } else {
            probePos += 1;
        }
    }

    // Default clock fallbacks if not in header
    float clkSN = (snClk & 0x7FFFFFFF) ? (float)(snClk & 0x7FFFFFFF) : 3579545.0f;
    float clkYM2612 = (ym2612Clk & 0x7FFFFFFF) ? (float)(ym2612Clk & 0x7FFFFFFF) : 7670453.0f;
    float clkYM2151 = (ym2151Clk & 0x7FFFFFFF) ? (float)(ym2151Clk & 0x7FFFFFFF) : 3579545.0f;
    float clkNES = (nesClk & 0x7FFFFFFF) ? (float)(nesClk & 0x7FFFFFFF) : 1789773.0f;
    float clkGB = (gbClk & 0x7FFFFFFF) ? (float)(gbClk & 0x7FFFFFFF) : 4194304.0f;
    float clkYM2413 = (ym2413Clk & 0x7FFFFFFF) ? (float)(ym2413Clk & 0x7FFFFFFF) : 3579545.0f;
    float clkYM2203 = (ym2203Clk & 0x7FFFFFFF) ? (float)(ym2203Clk & 0x7FFFFFFF) : 3000000.0f;
    float clkAY = (ayClk & 0x7FFFFFFF) ? (float)(ayClk & 0x7FFFFFFF) : 1500000.0f;
    float clkRF5C = 12500000.0f;
    if (rf5c164Clk & 0x7FFFFFFF) clkRF5C = (float)(rf5c164Clk & 0x7FFFFFFF);
    else if (rf5c68Clk & 0x7FFFFFFF) clkRF5C = (float)(rf5c68Clk & 0x7FFFFFFF);

    // Register detected channels
    int chId = 0;
    int chipBaseCh[64][2];
    int linkBaseCh[64][2];
    for (int t = 0; t < 64; ++t) {
        chipBaseCh[t][0] = -1; chipBaseCh[t][1] = -1;
        linkBaseCh[t][0] = -1; linkBaseCh[t][1] = -1;
    }
    int nesFdsCh = -1;

    std::vector<PLR_DEV_INFO> devList;
    if (player) {
        player->GetSongDeviceInfo(devList);
    }

    if (!devList.empty()) {
        for (size_t di = 0; di < devList.size(); ++di) {
            const auto& dev = devList[di];
            std::string chipFriendly = dev.devDecl && dev.devDecl->name ? dev.devDecl->name(dev.devCfg) : "";
            if (chipFriendly.empty()) {
                const char* n = SndEmu_GetDevName(dev.type, 0, NULL);
                chipFriendly = n ? n : "Sound Chip";
            }
            if (dev.instance > 0) {
                chipFriendly += " #" + std::to_string(dev.instance + 1);
            }

            int chCount = (dev.devDecl && dev.devDecl->channelCount) ? dev.devDecl->channelCount(dev.devCfg) : 0;
            if (chCount <= 0) {
                if (dev.type == DEVID_SEGAPCM) chCount = 16;
                else if (dev.type == DEVID_YM2612) chCount = 6;
                else if (dev.type == DEVID_SN76496) chCount = 4;
                else if (dev.type == DEVID_YM2151) chCount = 8;
                else if (dev.type == DEVID_YM2413) chCount = 9;
                else if (dev.type == DEVID_YM2203) chCount = 3;
                else if (dev.type == DEVID_AY8910) chCount = 3;
                else if (dev.type == DEVID_NES_APU) chCount = (nesClk & 0x80000000) ? 6 : 5;
                else if (dev.type == DEVID_GB_DMG) chCount = 4;
                else if (dev.type == DEVID_YMW258) chCount = 28;
                else if (dev.type == DEVID_C6280) chCount = 6;
                else if (dev.type == DEVID_SAA1099) chCount = 6;
                else if (dev.type == DEVID_QSOUND) chCount = 16;
                else if (dev.type == DEVID_SCSP) chCount = 32;
                else if (dev.type == DEVID_MSM6295) chCount = 4;
                else if (dev.type == DEVID_RF5C68) chCount = 8;
                else if (dev.type == DEVID_K051649) chCount = 5;
                else chCount = 4;
            }

            const char** customNames = (dev.devDecl && dev.devDecl->channelNames) ? dev.devDecl->channelNames(dev.devCfg) : nullptr;

            int inst = (dev.instance < 2) ? (int)dev.instance : 0;
            int devType = (dev.type < 64) ? (int)dev.type : 0;

            if (dev.parentIdx != (UINT32)-1) {
                linkBaseCh[devType][inst] = chId;
            } else {
                chipBaseCh[devType][inst] = chId;
            }

            if (dev.type == DEVID_NES_APU && chCount >= 6) {
                nesFdsCh = chId + 5;
            }

            std::string effectiveChipName = chipFriendly;
            if (dev.parentIdx != (UINT32)-1 && dev.parentIdx < devList.size()) {
                const auto& parentDev = devList[dev.parentIdx];
                if (parentDev.devDecl && parentDev.devDecl->name) {
                    effectiveChipName = parentDev.devDecl->name(parentDev.devCfg);
                }
            }

            for (int i = 0; i < chCount; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = effectiveChipName;

                if (dev.type == DEVID_YM2612 && i == 5) {
                    ch.name = "Ch 6 / DAC";
                } else if (dev.parentIdx != (UINT32)-1 && dev.type == DEVID_AY8910) {
                    ch.name = "SSG " + std::to_string(i + 1);
                } else if (dev.type == DEVID_RF5C68) {
                    ch.name = "PCM " + std::to_string(i + 1);
                } else if (customNames && customNames[i] && strlen(customNames[i]) > 0) {
                    ch.name = customNames[i];
                } else {
                    ch.name = "Ch " + std::to_string(i + 1);
                }

                std::string nLower = ch.name;
                std::transform(nLower.begin(), nLower.end(), nLower.begin(), ::tolower);

                if (nLower.find("fm") != std::string::npos) {
                    ch.type = "FM";
                } else if (nLower.find("pulse") != std::string::npos || nLower.find("square") != std::string::npos || nLower.find("ssg") != std::string::npos || nLower.find("tone") != std::string::npos) {
                    ch.type = (nLower.find("pulse") != std::string::npos) ? "Pulse" : "Square";
                } else if (nLower.find("triangle") != std::string::npos) {
                    ch.type = "Triangle";
                } else if (nLower.find("noise") != std::string::npos) {
                    ch.type = "Noise";
                } else if (nLower.find("pcm") != std::string::npos || nLower.find("dac") != std::string::npos || nLower.find("adpcm") != std::string::npos || nLower.find("dpcm") != std::string::npos || nLower.find("dmc") != std::string::npos) {
                    ch.type = "PCM";
                } else if (nLower.find("wave") != std::string::npos || nLower.find("fds") != std::string::npos) {
                    ch.type = "Wave";
                } else if (dev.type == DEVID_YM2612 || dev.type == DEVID_YM2151 || dev.type == DEVID_YM2413 || dev.type == DEVID_YM2203 || dev.type == DEVID_YM2608 || dev.type == DEVID_YM2610 || dev.type == DEVID_YM3812 || dev.type == DEVID_YMF262) {
                    ch.type = "FM";
                } else if (dev.type == DEVID_SN76496 || dev.type == DEVID_AY8910) {
                    ch.type = (i == chCount - 1 && dev.type == DEVID_SN76496) ? "Noise" : "Square";
                } else if (dev.type == DEVID_SEGAPCM || dev.type == DEVID_YMW258 || dev.type == DEVID_MSM6295 || dev.type == DEVID_MSM6258 || dev.type == DEVID_RF5C68 || dev.type == DEVID_QSOUND || dev.type == DEVID_SCSP) {
                    ch.type = "PCM";
                } else {
                    ch.type = "Wave";
                }

                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = dev.type;
                ch.chipInstance = (int)dev.instance;
                ch.channelInChip = i;
                ch.linkIndex = (dev.parentIdx != (UINT32)-1) ? 1 : 0;
                m_channels.push_back(ch);
            }
        }
    } else {
        // Fallback when player device info is unavailable
        if (hasYM2612) {
            chipBaseCh[DEVID_YM2612][0] = chId;
            for (int i = 0; i < 6; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "YM2612";
                ch.name = (i == 5) ? "Ch 6 / DAC" : ("FM " + std::to_string(i + 1));
                ch.type = (i == 5) ? "PCM" : "FM";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_YM2612;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasSN) {
            chipBaseCh[DEVID_SN76496][0] = chId;
            for (int i = 0; i < 4; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "SN76489";
                ch.name = (i < 3) ? ("Tone " + std::to_string(i + 1)) : "Noise";
                ch.type = (i < 3) ? "Square" : "Noise";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_SN76496;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasYM2151) {
            chipBaseCh[DEVID_YM2151][0] = chId;
            for (int i = 0; i < 8; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "YM2151";
                ch.name = "FM " + std::to_string(i + 1);
                ch.type = "FM";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_YM2151;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasNES) {
            chipBaseCh[DEVID_NES_APU][0] = chId;
            bool hasFds = (nesClk & 0x80000000) != 0;
            static const char* nesNames[6] = {"Pulse 1", "Pulse 2", "Triangle", "Noise", "DMC", "FDS"};
            static const char* nesTypes[6] = {"Pulse", "Pulse", "Triangle", "Noise", "PCM", "Wave"};
            int cnt = hasFds ? 6 : 5;
            if (hasFds) nesFdsCh = chId + 5;
            for (int i = 0; i < cnt; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "NES APU";
                ch.name = nesNames[i];
                ch.type = nesTypes[i];
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_NES_APU;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasGB) {
            chipBaseCh[DEVID_GB_DMG][0] = chId;
            static const char* gbNames[4] = {"Pulse 1", "Pulse 2", "Wave", "Noise"};
            static const char* gbTypes[4] = {"Pulse", "Pulse", "Wave", "Noise"};
            for (int i = 0; i < 4; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "GB DMG";
                ch.name = gbNames[i];
                ch.type = gbTypes[i];
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_GB_DMG;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasYM2413) {
            chipBaseCh[DEVID_YM2413][0] = chId;
            for (int i = 0; i < 9; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "YM2413";
                ch.name = "FM " + std::to_string(i + 1);
                ch.type = "FM";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_YM2413;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasYM2203) {
            chipBaseCh[DEVID_YM2203][0] = chId;
            linkBaseCh[DEVID_AY8910][0] = chId + 3;
            for (int i = 0; i < 6; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "YM2203";
                ch.name = (i < 3) ? ("FM " + std::to_string(i + 1)) : ("SSG " + std::to_string(i - 2));
                ch.type = (i < 3) ? "FM" : "Square";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_YM2203;
                ch.chipInstance = 0;
                ch.channelInChip = (i < 3) ? i : (i - 3);
                ch.linkIndex = (i < 3) ? 0 : 1;
                m_channels.push_back(ch);
            }
        }
        if (hasSegaPCM) {
            chipBaseCh[DEVID_SEGAPCM][0] = chId;
            for (int i = 0; i < 16; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "SegaPCM";
                ch.name = "PCM " + std::to_string(i + 1);
                ch.type = "PCM";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_SEGAPCM;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasMultiPcm) {
            chipBaseCh[DEVID_YMW258][0] = chId;
            for (int i = 0; i < 28; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "MultiPCM";
                ch.name = "Voice " + std::to_string(i + 1);
                ch.type = "PCM";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_YMW258;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasPCE) {
            chipBaseCh[DEVID_C6280][0] = chId;
            for (int i = 0; i < 6; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "HuC6280";
                ch.name = "Voice " + std::to_string(i + 1);
                ch.type = "Wave";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_C6280;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasYM3812) {
            chipBaseCh[DEVID_YM3812][0] = chId;
            for (int i = 0; i < 9; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "YM3812";
                ch.name = "FM " + std::to_string(i + 1);
                ch.type = "FM";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_YM3812;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasYMF262) {
            chipBaseCh[DEVID_YMF262][0] = chId;
            for (int i = 0; i < 18; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "YMF262";
                ch.name = "FM " + std::to_string(i + 1);
                ch.type = "FM";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_YMF262;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasYM2608) {
            chipBaseCh[DEVID_YM2608][0] = chId;
            linkBaseCh[DEVID_AY8910][0] = chId + 6;
            for (int i = 0; i < 9; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "YM2608";
                ch.name = (i < 6) ? ("FM " + std::to_string(i + 1)) : ("SSG " + std::to_string(i - 5));
                ch.type = (i < 6) ? "FM" : "Square";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_YM2608;
                ch.chipInstance = 0;
                ch.channelInChip = (i < 6) ? i : (i - 6);
                ch.linkIndex = (i < 6) ? 0 : 1;
                m_channels.push_back(ch);
            }
        }
        if (hasYM2610) {
            chipBaseCh[DEVID_YM2610][0] = chId;
            linkBaseCh[DEVID_AY8910][0] = chId + 6;
            for (int i = 0; i < 9; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "YM2610";
                ch.name = (i < 6) ? ("FM " + std::to_string(i + 1)) : ("SSG " + std::to_string(i - 5));
                ch.type = (i < 6) ? "FM" : "Square";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_YM2610;
                ch.chipInstance = 0;
                ch.channelInChip = (i < 6) ? i : (i - 6);
                ch.linkIndex = (i < 6) ? 0 : 1;
                m_channels.push_back(ch);
            }
        }
        if (hasAY) {
            chipBaseCh[DEVID_AY8910][0] = chId;
            for (int i = 0; i < 3; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "AY-3-8910";
                ch.name = "SSG " + std::to_string(i + 1);
                ch.type = "Square";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_AY8910;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (hasRF5C) {
            chipBaseCh[DEVID_RF5C68][0] = chId;
            for (int i = 0; i < 8; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = (rf5c164Clk > 0) ? "RF5C164" : "RF5C68";
                ch.name = "PCM " + std::to_string(i + 1);
                ch.type = "PCM";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                ch.chipType = DEVID_RF5C68;
                ch.chipInstance = 0;
                ch.channelInChip = i;
                ch.linkIndex = 0;
                m_channels.push_back(ch);
            }
        }
        if (m_channels.empty()) {
            for (int i = 0; i < 4; ++i) {
                TrackedChannel ch;
                ch.id = chId++;
                ch.chipName = "Channel";
                ch.name = "Channel " + std::to_string(i + 1);
                ch.type = "Square";
                ch.color = GetChannelColor(ch.chipName, ch.type, i);
                m_channels.push_back(ch);
            }
        }
    }

    // Direct channel index lookups
    int ym2612Base = chipBaseCh[DEVID_YM2612][0];
    int snBase = chipBaseCh[DEVID_SN76496][0];
    int ym2151Base = chipBaseCh[DEVID_YM2151][0];
    int nesBase = chipBaseCh[DEVID_NES_APU][0];
    int gbBase = chipBaseCh[DEVID_GB_DMG][0];
    int ym2413Base = chipBaseCh[DEVID_YM2413][0];
    int ym2203Base = chipBaseCh[DEVID_YM2203][0];
    int ym2608Base = chipBaseCh[DEVID_YM2608][0];
    int ym2610Base = chipBaseCh[DEVID_YM2610][0];
    int ym3812Base = chipBaseCh[DEVID_YM3812][0];
    int ymf262Base = chipBaseCh[DEVID_YMF262][0];
    int segaPcmBase = chipBaseCh[DEVID_SEGAPCM][0];
    int multiPcmBase = chipBaseCh[DEVID_YMW258][0];
    int pceBase = chipBaseCh[DEVID_C6280][0];
    int saaBase = chipBaseCh[DEVID_SAA1099][0];
    int qsoundBase = chipBaseCh[DEVID_QSOUND][0];
    int okiBase = chipBaseCh[DEVID_MSM6295][0];
    int sccBase = chipBaseCh[DEVID_K051649][0];
    int rf5cBase = chipBaseCh[DEVID_RF5C68][0];
    int ayBase = chipBaseCh[DEVID_AY8910][0];
    int ssgBase = linkBaseCh[DEVID_AY8910][0];

    m_openNotes.resize(m_channels.size());
    for (auto& op : m_openNotes) op = OpenNote();

    // -------------------------------------------------------------
    // Sound Chip State Emulation during Scan
    // -------------------------------------------------------------

    // SN76489 state
    int snLatchCh = 0;
    int snLatchType = 0;
    uint16_t snPeriod[4] = {0};
    uint8_t snAtten[4] = {15, 15, 15, 15};

    // YM2612 state
    uint8_t ym2612FNumLow[6] = {0};
    uint8_t ym2612FNumHigh[6] = {0};
    uint8_t ym2612Block[6] = {0};
    bool ym2612KeyOn[6] = {false};

    // YM2151 state
    uint8_t ym2151KC[8] = {0};
    bool ym2151KeyOn[8] = {false};

    // NES APU state
    uint8_t nesRegs[32] = {0};
    nesRegs[0x15] = 0x1F; // default enables
    uint8_t nesFdsRegs[128] = {0};

    // GB DMG state
    uint8_t gbRegs[64] = {0};

    // YM2413 state
    uint8_t ym2413FNumLow[9] = {0};
    uint8_t ym2413FNumHigh[9] = {0};
    uint8_t ym2413Block[9] = {0};
    bool ym2413KeyOn[9] = {false};

    // YM2203 / SSG state
    uint8_t ym2203FNumLow[3] = {0};
    uint8_t ym2203FNumHigh[3] = {0};
    uint8_t ym2203Block[3] = {0};
    uint16_t ssgPeriod[3] = {0};
    uint8_t ssgVol[3] = {0};

    // HuC6280 state
    int pceCurCh = 0;
    uint8_t pceRegs[6][16] = {{0}};

    // SAA1099 state
    uint8_t saaRegs[32] = {0};
    uint8_t saaFreq[6] = {0};
    uint8_t saaOctave[6] = {0};

    // OPL state
    uint8_t oplFNumLow[18] = {0};

    // MultiPCM state
    int multiPcmSlot = 0;
    int multiPcmReg = 0;
    uint8_t multiPcmRegs[32][8] = {{0}};

    // AY8910 state
    uint16_t ayPeriod[3] = {0};
    uint8_t ayVol[3] = {0};

    // SegaPCM state
    uint8_t segaPcmLowRegs[16][8] = {{0}};
    uint8_t segaPcmHighRegs[16][8] = {{0}};
    for (int ch = 0; ch < 16; ++ch) segaPcmHighRegs[ch][6] = 1; // start disabled

    // SCC state
    uint16_t sccFreq[5] = {0};
    uint8_t sccVol[5] = {0};

    // RF5C68 / RF5C164 state (up to 2 chips)
    int rf5cCurCh[2] = {0, 0};
    uint16_t rf5cStep[2][8] = {{0}};
    uint8_t rf5cEnv[2][8] = {{0}};
    uint8_t rf5cPan[2][8] = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
                             {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
    bool rf5cEnabled[2][8] = {{false}};
    uint8_t rf5cStart[2][8] = {{0}};
    uint16_t rf5cLoop[2][8] = {{0}};

    // Main Parse Loop
    size_t filePos = dataOfs;
    uint32_t curSample = 0;

    while (filePos < eofOfs && filePos < dataSize) {
        uint8_t cmd = vgmData[filePos];

        if (cmd == 0x66) {
            // End of sound data
            break;
        }

        switch (cmd) {
        // Delay commands
        case 0x61: {
            if (filePos + 3 > dataSize) break;
            uint16_t waitSmpl = ReadLE16(&vgmData[filePos + 1]);
            curSample += waitSmpl;
            filePos += 3;
            break;
        }
        case 0x62: {
            curSample += 735;
            filePos += 1;
            break;
        }
        case 0x63: {
            curSample += 882;
            filePos += 1;
            break;
        }

        // SN76489 write (0x50 dd)
        case 0x50: {
            if (filePos + 2 > dataSize) break;
            uint8_t data = vgmData[filePos + 1];
            filePos += 2;

            if (snBase >= 0) {
                if (data & 0x80) {
                    snLatchCh = (data >> 5) & 3;
                    snLatchType = (data >> 4) & 1;
                    if (snLatchType == 1) {
                        snAtten[snLatchCh] = data & 0x0F;
                    } else {
                        if (snLatchCh < 3) {
                            snPeriod[snLatchCh] = (snPeriod[snLatchCh] & 0x3F0) | (data & 0x0F);
                        } else {
                            snPeriod[3] = data & 0x07;
                        }
                    }
                } else {
                    if (snLatchType == 0 && snLatchCh < 3) {
                        snPeriod[snLatchCh] = (snPeriod[snLatchCh] & 0x0F) | ((data & 0x3F) << 4);
                    } else if (snLatchType == 1) {
                        snAtten[snLatchCh] = data & 0x0F;
                    }
                }

                // Evaluate tones for SN channels 0..2
                for (int ch = 0; ch < 3; ++ch) {
                    int globalCh = snBase + ch;
                    if (snAtten[ch] < 15 && snPeriod[ch] >= 6) {
                        float freq = clkSN / (32.0f * (float)snPeriod[ch]);
                        float midi = FreqToMidi(freq);
                        float vol = (15.0f - (float)snAtten[ch]) / 15.0f;
                        NoteOn(globalCh, curSample, midi, vol);
                    } else {
                        NoteOff(globalCh, curSample);
                    }
                }
                // Evaluate Noise for SN channel 3
                if (snAtten[3] < 15) {
                    int rateSel = snPeriod[3] & 3;
                    float freq = 0.0f;
                    if (rateSel == 3 && snPeriod[2] >= 6) {
                        freq = clkSN / (32.0f * (float)snPeriod[2]);
                    } else {
                        static const int divs[3] = {16, 32, 64};
                        freq = clkSN / (32.0f * (float)divs[rateSel]);
                    }
                    float midi = std::clamp(FreqToMidi(freq), 24.0f, 96.0f);
                    float vol = (15.0f - (float)snAtten[3]) / 15.0f;
                    NoteOn(snBase + 3, curSample, midi, vol);
                } else {
                    NoteOff(snBase + 3, curSample);
                }
            }
            break;
        }

        // YM2413 write (0x51 rr dd)
        case 0x51: {
            if (filePos + 3 > dataSize) break;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (ym2413Base >= 0) {
                if (reg >= 0x10 && reg <= 0x18) {
                    int ch = reg - 0x10;
                    ym2413FNumLow[ch] = data;
                    if (ym2413KeyOn[ch]) {
                        uint16_t fnum = ((uint16_t)ym2413FNumHigh[ch] << 8) | ym2413FNumLow[ch];
                        float freq = ((float)fnum * clkYM2413) / (72.0f * (float)(1 << (19 - ym2413Block[ch])));
                        NoteOn(ym2413Base + ch, curSample, FreqToMidi(freq), 0.85f);
                    }
                } else if (reg >= 0x20 && reg <= 0x28) {
                    int ch = reg - 0x20;
                    bool kon = (data & 0x10) != 0;
                    ym2413Block[ch] = (data >> 1) & 7;
                    ym2413FNumHigh[ch] = data & 1;
                    ym2413KeyOn[ch] = kon;

                    int globalCh = ym2413Base + ch;
                    if (kon) {
                        uint16_t fnum = ((uint16_t)ym2413FNumHigh[ch] << 8) | ym2413FNumLow[ch];
                        float freq = ((float)fnum * clkYM2413) / (72.0f * (float)(1 << (19 - ym2413Block[ch])));
                        float midi = FreqToMidi(freq);
                        NoteOn(globalCh, curSample, midi, 0.85f);
                    } else {
                        NoteOff(globalCh, curSample);
                    }
                }
            }
            break;
        }

        // YM2612 write Port 0 (0x52 rr dd) & Port 1 (0x53 rr dd)
        case 0x52:
        case 0x53: {
            if (filePos + 3 > dataSize) break;
            uint8_t port = (cmd == 0x53) ? 1 : 0;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (ym2612Base >= 0) {
                if (port == 0 && reg == 0x28) {
                    // Key On / Key Off
                    uint8_t chRaw = data & 0x07;
                    int ch = -1;
                    if (chRaw <= 2) ch = chRaw;
                    else if (chRaw >= 4 && chRaw <= 6) ch = chRaw - 1; // 4->3, 5->4, 6->5

                    if (ch >= 0 && ch < 6) {
                        uint8_t slots = (data >> 4) & 0x0F;
                        int globalCh = ym2612Base + ch;
                        if (slots != 0) {
                            ym2612KeyOn[ch] = true;
                            uint16_t fnum = ((uint16_t)ym2612FNumHigh[ch] << 8) | ym2612FNumLow[ch];
                            float freq = ((float)fnum * clkYM2612) / (144.0f * (float)(1 << (20 - ym2612Block[ch])));
                            float midi = FreqToMidi(freq);
                            NoteOn(globalCh, curSample, midi, 0.85f);
                        } else {
                            ym2612KeyOn[ch] = false;
                            NoteOff(globalCh, curSample);
                        }
                    }
                } else if (reg >= 0xA0 && reg <= 0xA2) {
                    int ch = (port * 3) + (reg - 0xA0);
                    ym2612FNumLow[ch] = data;
                } else if (reg >= 0xA4 && reg <= 0xA6) {
                    int ch = (port * 3) + (reg - 0xA4);
                    ym2612Block[ch] = (data >> 3) & 0x07;
                    ym2612FNumHigh[ch] = data & 0x07;
                    if (ym2612KeyOn[ch]) {
                        uint16_t fnum = ((uint16_t)ym2612FNumHigh[ch] << 8) | ym2612FNumLow[ch];
                        float freq = ((float)fnum * clkYM2612) / (144.0f * (float)(1 << (20 - ym2612Block[ch])));
                        float midi = FreqToMidi(freq);
                        NoteOn(ym2612Base + ch, curSample, midi, 0.85f);
                    }
                }
            }
            break;
        }

        // YM2151 write (0x54 rr dd)
        case 0x54: {
            if (filePos + 3 > dataSize) break;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (ym2151Base >= 0) {
                if (reg == 0x08) {
                    // Key On / Key Off
                    int ch = data & 0x07;
                    uint8_t slots = (data >> 3) & 0x0F;
                    int globalCh = ym2151Base + ch;
                    if (slots != 0) {
                        ym2151KeyOn[ch] = true;
                        uint8_t kc = ym2151KC[ch];
                        int octave = (kc >> 4) & 0x07;
                        int noteIdx = (int)kc - ((int)kc >> 2) + 1;
                        int midi = (octave + 1) * 12 + (noteIdx % 12);
                        NoteOn(globalCh, curSample, (float)midi, 0.85f);
                    } else {
                        ym2151KeyOn[ch] = false;
                        NoteOff(globalCh, curSample);
                    }
                } else if (reg >= 0x28 && reg <= 0x2F) {
                    int ch = reg - 0x28;
                    ym2151KC[ch] = data & 0x7F;
                    if (ym2151KeyOn[ch]) {
                        uint8_t kc = ym2151KC[ch];
                        int octave = (kc >> 4) & 0x07;
                        int noteIdx = (int)kc - ((int)kc >> 2) + 1;
                        int midi = (octave + 1) * 12 + (noteIdx % 12);
                        NoteOn(ym2151Base + ch, curSample, (float)midi, 0.85f);
                    }
                }
            }
            break;
        }

        // YM2203 write (0x55 rr dd)
        case 0x55: {
            if (filePos + 3 > dataSize) break;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (ym2203Base >= 0) {
                // FM Part (0x28 key on, 0xA0..0xA2, 0xA4..0xA6)
                if (reg == 0x28) {
                    int ch = data & 3;
                    if (ch < 3) {
                        uint8_t slots = (data >> 4) & 0x0F;
                        int globalCh = ym2203Base + ch;
                        if (slots != 0) {
                            uint16_t fnum = ((uint16_t)ym2203FNumHigh[ch] << 8) | ym2203FNumLow[ch];
                            float freq = ((float)fnum * clkYM2203) / (144.0f * (float)(1 << (20 - ym2203Block[ch])));
                            NoteOn(globalCh, curSample, FreqToMidi(freq), 0.85f);
                        } else {
                            NoteOff(globalCh, curSample);
                        }
                    }
                } else if (reg >= 0xA0 && reg <= 0xA2) {
                    ym2203FNumLow[reg - 0xA0] = data;
                } else if (reg >= 0xA4 && reg <= 0xA6) {
                    int ch = reg - 0xA4;
                    ym2203Block[ch] = (data >> 3) & 7;
                    ym2203FNumHigh[ch] = data & 7;
                }
                // SSG Part
                else if (reg <= 0x05) {
                    int ssgCh = reg / 2;
                    if (reg & 1) ssgPeriod[ssgCh] = (ssgPeriod[ssgCh] & 0xFF) | ((data & 0x0F) << 8);
                    else ssgPeriod[ssgCh] = (ssgPeriod[ssgCh] & 0xF00) | data;
                    if ((ssgVol[ssgCh] & 0x0F) > 0 && ssgPeriod[ssgCh] >= 4) {
                        int targetSsg = (ssgBase >= 0) ? ssgBase : (ym2203Base + 3);
                        int globalCh = targetSsg + ssgCh;
                        float freq = (clkYM2203 / 2.0f) / (16.0f * (float)ssgPeriod[ssgCh]);
                        NoteOn(globalCh, curSample, FreqToMidi(freq), (float)(ssgVol[ssgCh] & 0x0F) / 15.0f);
                    }
                } else if (reg >= 0x08 && reg <= 0x0A) {
                    int ssgCh = reg - 0x08;
                    ssgVol[ssgCh] = data & 0x1F;
                    int targetSsg = (ssgBase >= 0) ? ssgBase : (ym2203Base + 3);
                    int globalCh = targetSsg + ssgCh;
                    if ((data & 0x0F) > 0 && ssgPeriod[ssgCh] >= 4) {
                        float freq = (clkYM2203 / 2.0f) / (16.0f * (float)ssgPeriod[ssgCh]);
                        NoteOn(globalCh, curSample, FreqToMidi(freq), (float)(data & 0x0F) / 15.0f);
                    } else {
                        NoteOff(globalCh, curSample);
                    }
                }
            }
            break;
        }

        // YM2608 (0x56, 0x57) & YM2610 (0x58, 0x59)
        case 0x56:
        case 0x57:
        case 0x58:
        case 0x59: {
            if (filePos + 3 > dataSize) break;
            uint8_t port = (cmd == 0x57 || cmd == 0x59) ? 1 : 0;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            int targetBase = (cmd == 0x56 || cmd == 0x57) ? ym2608Base : ym2610Base;
            if (targetBase >= 0) {
                if (port == 0 && reg == 0x28) {
                    uint8_t chRaw = data & 0x07;
                    int ch = -1;
                    if (chRaw <= 2) ch = chRaw;
                    else if (chRaw >= 4 && chRaw <= 6) ch = chRaw - 1;
                    if (ch >= 0 && ch < 6) {
                        uint8_t slots = (data >> 4) & 0x0F;
                        int globalCh = targetBase + ch;
                        if (slots != 0) {
                            uint16_t fnum = ((uint16_t)ym2612FNumHigh[ch] << 8) | ym2612FNumLow[ch];
                            float freq = ((float)fnum * clkYM2612) / (144.0f * (float)(1 << (20 - ym2612Block[ch])));
                            NoteOn(globalCh, curSample, FreqToMidi(freq), 0.85f);
                        } else {
                            NoteOff(globalCh, curSample);
                        }
                    }
                } else if (reg >= 0xA0 && reg <= 0xA2) {
                    int ch = (port * 3) + (reg - 0xA0);
                    ym2612FNumLow[ch] = data;
                } else if (reg >= 0xA4 && reg <= 0xA6) {
                    int ch = (port * 3) + (reg - 0xA4);
                    ym2612Block[ch] = (data >> 3) & 0x07;
                    ym2612FNumHigh[ch] = data & 0x07;
                } else if (port == 0 && reg <= 0x05) {
                    int ssgCh = reg / 2;
                    if (reg & 1) ssgPeriod[ssgCh] = (ssgPeriod[ssgCh] & 0xFF) | ((data & 0x0F) << 8);
                    else ssgPeriod[ssgCh] = (ssgPeriod[ssgCh] & 0xF00) | data;
                    if ((ssgVol[ssgCh] & 0x0F) > 0 && ssgPeriod[ssgCh] >= 4) {
                        int targetSsg = (ssgBase >= 0) ? ssgBase : (targetBase + 6);
                        int globalCh = targetSsg + ssgCh;
                        float freq = 1500000.0f / (16.0f * (float)ssgPeriod[ssgCh]);
                        NoteOn(globalCh, curSample, FreqToMidi(freq), (float)(ssgVol[ssgCh] & 0x0F) / 15.0f);
                    }
                } else if (port == 0 && reg >= 0x08 && reg <= 0x0A) {
                    int ssgCh = reg - 0x08;
                    ssgVol[ssgCh] = data & 0x1F;
                    int targetSsg = (ssgBase >= 0) ? ssgBase : (targetBase + 6);
                    int globalCh = targetSsg + ssgCh;
                    if ((data & 0x0F) > 0 && ssgPeriod[ssgCh] >= 4) {
                        float freq = 1500000.0f / (16.0f * (float)ssgPeriod[ssgCh]);
                        NoteOn(globalCh, curSample, FreqToMidi(freq), (float)(data & 0x0F) / 15.0f);
                    } else {
                        NoteOff(globalCh, curSample);
                    }
                }
            }
            break;
        }

        // YM3812 / YM3526 / Y8950 (OPL2 / OPL) (0x5A, 0x5B, 0x5C)
        case 0x5A:
        case 0x5B:
        case 0x5C: {
            if (filePos + 3 > dataSize) break;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (ym3812Base >= 0) {
                if (reg >= 0xA0 && reg <= 0xA8) {
                    int ch = reg - 0xA0;
                    oplFNumLow[ch] = data;
                } else if (reg >= 0xB0 && reg <= 0xB8) {
                    int ch = reg - 0xB0;
                    bool kon = (data & 0x20) != 0;
                    int block = (data >> 2) & 7;
                    uint16_t fnum = ((uint16_t)(data & 3) << 8) | oplFNumLow[ch];
                    int globalCh = ym3812Base + ch;
                    if (kon && fnum > 0) {
                        float freq = (3579545.0f * (float)fnum) / (72.0f * (float)(1 << (20 - block)));
                        NoteOn(globalCh, curSample, FreqToMidi(freq), 0.85f);
                    } else if (!kon) {
                        NoteOff(globalCh, curSample);
                    }
                }
            }
            break;
        }

        // YMF262 (OPL3) (0x5E, 0x5F)
        case 0x5E:
        case 0x5F: {
            if (filePos + 3 > dataSize) break;
            uint8_t port = (cmd == 0x5F) ? 1 : 0;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (ymf262Base >= 0) {
                int chOffset = port * 9;
                if (reg >= 0xA0 && reg <= 0xA8) {
                    int ch = chOffset + (reg - 0xA0);
                    oplFNumLow[ch] = data;
                } else if (reg >= 0xB0 && reg <= 0xB8) {
                    int ch = chOffset + (reg - 0xB0);
                    bool kon = (data & 0x20) != 0;
                    int block = (data >> 2) & 7;
                    uint16_t fnum = ((uint16_t)(data & 3) << 8) | oplFNumLow[ch];
                    int globalCh = ymf262Base + ch;
                    if (kon && fnum > 0) {
                        float freq = (3579545.0f * (float)fnum) / (72.0f * (float)(1 << (20 - block)));
                        NoteOn(globalCh, curSample, FreqToMidi(freq), 0.85f);
                    } else if (!kon) {
                        NoteOff(globalCh, curSample);
                    }
                }
            }
            break;
        }

        // AY8910 / YM2149 PSG write (0xA0 rr dd)
        case 0xA0: {
            if (filePos + 3 > dataSize) break;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            int targetAy = (ayBase >= 0) ? ayBase : ssgBase;
            if (targetAy >= 0 && reg < 16) {
                if (reg <= 0x05) {
                    int ssgCh = reg / 2;
                    if (reg & 1) ayPeriod[ssgCh] = (ayPeriod[ssgCh] & 0xFF) | ((data & 0x0F) << 8);
                    else ayPeriod[ssgCh] = (ayPeriod[ssgCh] & 0xF00) | data;
                    if ((ayVol[ssgCh] & 0x0F) > 0 && ayPeriod[ssgCh] >= 4) {
                        int globalCh = targetAy + ssgCh;
                        float freq = (clkAY / 2.0f) / (16.0f * (float)ayPeriod[ssgCh]);
                        NoteOn(globalCh, curSample, FreqToMidi(freq), (float)(ayVol[ssgCh] & 0x0F) / 15.0f);
                    }
                } else if (reg >= 0x08 && reg <= 0x0A) {
                    int ssgCh = reg - 0x08;
                    ayVol[ssgCh] = data & 0x1F;
                    int globalCh = targetAy + ssgCh;
                    if ((data & 0x0F) > 0 && ayPeriod[ssgCh] >= 4) {
                        float freq = (clkAY / 2.0f) / (16.0f * (float)ayPeriod[ssgCh]);
                        NoteOn(globalCh, curSample, FreqToMidi(freq), (float)(data & 0x0F) / 15.0f);
                    } else {
                        NoteOff(globalCh, curSample);
                    }
                }
            }
            break;
        }

        // RF5C68 (0xB0) / RF5C164 (0xB1)
        case 0xB0:
        case 0xB1: {
            if (filePos + 3 > dataSize) break;
            uint8_t regRaw = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            int chipId = (regRaw & 0x80) ? 1 : 0;
            uint8_t reg = regRaw & 0x7F;
            int baseCh = chipBaseCh[DEVID_RF5C68][chipId];
            if (baseCh < 0 && chipId == 0) baseCh = rf5cBase;
            if (baseCh < 0) break;

            if (reg == 0x07) {
                // Control register:
                // bit 7: Chip enable
                // bit 6: 1 = Channel register select (bits 0-2 = channel 0..7)
                //        0 = RAM bank select (bits 0-3 = RAM bank 0..15)
                if (data & 0x40) {
                    rf5cCurCh[chipId] = data & 0x07;
                }
            } else if (reg == 0x08) {
                // Channel On/Off register:
                // Bit i: 0 = Channel i ENABLED (active / playing)
                //        1 = Channel i DISABLED (stopped / muted)
                for (int ch = 0; ch < 8; ++ch) {
                    bool newEnable = ((~data >> ch) & 1);
                    bool oldEnable = rf5cEnabled[chipId][ch];
                    rf5cEnabled[chipId][ch] = newEnable;

                    int globalCh = baseCh + ch;
                    if (newEnable && !oldEnable) {
                        // Key On: start note with current channel parameters
                        uint16_t step = rf5cStep[chipId][ch];
                        if (step > 0) {
                            float midi = 60.0f + 12.0f * std::log2(((float)step / 1024.0f) * (clkRF5C / 12500000.0f));
                            midi = std::clamp(midi, 21.0f, 108.0f);
                            uint8_t env = rf5cEnv[chipId][ch];
                            uint8_t pan = rf5cPan[chipId][ch];
                            int panMax = std::max(pan & 0x0F, (pan >> 4) & 0x0F);
                            if (panMax == 0) panMax = 15;
                            float vol = ((float)panMax * (float)env) / (15.0f * 255.0f);
                            if (vol < 0.1f) vol = 0.85f;
                            NoteOn(globalCh, curSample, midi, std::clamp(vol, 0.1f, 1.0f));
                        }
                    } else if (!newEnable && oldEnable) {
                        // Key Off: release playing note
                        NoteOff(globalCh, curSample);
                    }
                }
            } else if (reg <= 0x06) {
                int ch = rf5cCurCh[chipId];
                if (ch >= 0 && ch < 8) {
                    if (reg == 0x00) {
                        rf5cEnv[chipId][ch] = data;
                    } else if (reg == 0x01) {
                        rf5cPan[chipId][ch] = data;
                    } else if (reg == 0x02) {
                        rf5cStep[chipId][ch] = (rf5cStep[chipId][ch] & 0xFF00) | data;
                    } else if (reg == 0x03) {
                        rf5cStep[chipId][ch] = (rf5cStep[chipId][ch] & 0x00FF) | ((uint16_t)data << 8);
                    } else if (reg == 0x04) {
                        rf5cLoop[chipId][ch] = (rf5cLoop[chipId][ch] & 0xFF00) | data;
                    } else if (reg == 0x05) {
                        rf5cLoop[chipId][ch] = (rf5cLoop[chipId][ch] & 0x00FF) | ((uint16_t)data << 8);
                    } else if (reg == 0x06) {
                        rf5cStart[chipId][ch] = data;
                    }

                    // If channel is already playing, handle pitch change or re-trigger
                    if (rf5cEnabled[chipId][ch] && (reg == 0x02 || reg == 0x03)) {
                        uint16_t step = rf5cStep[chipId][ch];
                        if (step > 0) {
                            float midi = 60.0f + 12.0f * std::log2(((float)step / 1024.0f) * (clkRF5C / 12500000.0f));
                            midi = std::clamp(midi, 21.0f, 108.0f);
                            uint8_t env = rf5cEnv[chipId][ch];
                            uint8_t pan = rf5cPan[chipId][ch];
                            int panMax = std::max(pan & 0x0F, (pan >> 4) & 0x0F);
                            if (panMax == 0) panMax = 15;
                            float vol = ((float)panMax * (float)env) / (15.0f * 255.0f);
                            if (vol < 0.1f) vol = 0.85f;
                            NoteOn(baseCh + ch, curSample, midi, std::clamp(vol, 0.1f, 1.0f));
                        }
                    }
                }
            }
            break;
        }

        // Game Boy DMG write (0xB3 rr dd)
        case 0xB3: {
            if (filePos + 3 > dataSize) break;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (gbBase >= 0 && reg < 64) {
                gbRegs[reg] = data;
                // Ch 1 (0x10..0x14)
                if (reg == 0x14 && (data & 0x80)) {
                    uint16_t period = ((uint16_t)(data & 7) << 8) | gbRegs[0x13];
                    if (period < 2048) {
                        float freq = 131072.0f / (float)(2048 - period);
                        NoteOn(gbBase + 0, curSample, FreqToMidi(freq), 0.85f);
                    }
                } else if (reg == 0x12 && (data >> 4) == 0) {
                    NoteOff(gbBase + 0, curSample);
                }
                // Ch 2 (0x16..0x19)
                else if (reg == 0x19 && (data & 0x80)) {
                    uint16_t period = ((uint16_t)(data & 7) << 8) | gbRegs[0x18];
                    if (period < 2048) {
                        float freq = 131072.0f / (float)(2048 - period);
                        NoteOn(gbBase + 1, curSample, FreqToMidi(freq), 0.85f);
                    }
                } else if (reg == 0x17 && (data >> 4) == 0) {
                    NoteOff(gbBase + 1, curSample);
                }
                // Ch 3 Wave (0x1A..0x1E)
                else if (reg == 0x1E && (data & 0x80)) {
                    uint16_t period = ((uint16_t)(data & 7) << 8) | gbRegs[0x1D];
                    if (period < 2048) {
                        float freq = 65536.0f / (float)(2048 - period);
                        NoteOn(gbBase + 2, curSample, FreqToMidi(freq), 0.85f);
                    }
                } else if (reg == 0x1A && !(data & 0x80)) {
                    NoteOff(gbBase + 2, curSample);
                }
                // Ch 4 Noise (0x20..0x23)
                else if (reg == 0x23 && (data & 0x80)) {
                    uint8_t vol = gbRegs[0x21] >> 4;
                    uint8_t s = gbRegs[0x22] >> 4;
                    uint8_t r = gbRegs[0x22] & 7;
                    float div = (r == 0) ? 0.5f : (float)r;
                    float freq = 524288.0f / (div * (float)(1 << (s + 1)));
                    if (vol > 0) {
                        NoteOn(gbBase + 3, curSample, std::clamp(FreqToMidi(freq), 24.0f, 96.0f), (float)vol / 15.0f);
                    }
                } else if (reg == 0x21 && (data >> 4) == 0) {
                    NoteOff(gbBase + 3, curSample);
                }
                // Master sound on/off (0x26)
                else if (reg == 0x26 && !(data & 0x80)) {
                    for (int c = 0; c < 4; ++c) NoteOff(gbBase + c, curSample);
                }
            }
            break;
        }

        // NES APU write (0xB4 rr dd)
        case 0xB4: {
            if (filePos + 3 > dataSize) break;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (nesBase >= 0 && reg < 0x20) {
                nesRegs[reg] = data;

                // Pulse 1 (regs 0..3)
                if (reg <= 0x03) {
                    uint16_t timer = ((uint16_t)(nesRegs[0x03] & 7) << 8) | nesRegs[0x02];
                    uint8_t vol = nesRegs[0x00] & 0x0F;
                    if (timer >= 8 && vol > 0 && (nesRegs[0x15] & 0x01)) {
                        float freq = clkNES / (16.0f * (float)(timer + 1));
                        NoteOn(nesBase + 0, curSample, FreqToMidi(freq), (float)vol / 15.0f);
                    } else if (vol == 0 || !(nesRegs[0x15] & 0x01)) {
                        NoteOff(nesBase + 0, curSample);
                    }
                }
                // Pulse 2 (regs 4..7)
                else if (reg >= 0x04 && reg <= 0x07) {
                    uint16_t timer = ((uint16_t)(nesRegs[0x07] & 7) << 8) | nesRegs[0x06];
                    uint8_t vol = nesRegs[0x04] & 0x0F;
                    if (timer >= 8 && vol > 0 && (nesRegs[0x15] & 0x02)) {
                        float freq = clkNES / (16.0f * (float)(timer + 1));
                        NoteOn(nesBase + 1, curSample, FreqToMidi(freq), (float)vol / 15.0f);
                    } else if (vol == 0 || !(nesRegs[0x15] & 0x02)) {
                        NoteOff(nesBase + 1, curSample);
                    }
                }
                // Triangle (regs 8..11)
                else if (reg >= 0x08 && reg <= 0x0B) {
                    uint16_t timer = ((uint16_t)(nesRegs[0x0B] & 7) << 8) | nesRegs[0x0A];
                    uint8_t linear = nesRegs[0x08] & 0x7F;
                    if (timer >= 2 && linear > 0 && (nesRegs[0x15] & 0x04)) {
                        float freq = clkNES / (32.0f * (float)(timer + 1));
                        NoteOn(nesBase + 2, curSample, FreqToMidi(freq), 0.9f);
                    } else if (linear == 0 || !(nesRegs[0x15] & 0x04)) {
                        NoteOff(nesBase + 2, curSample);
                    }
                }
                // Noise (regs 12..14)
                else if (reg == 0x0E || reg == 0x0C) {
                    uint8_t vol = nesRegs[0x0C] & 0x0F;
                    uint8_t period = nesRegs[0x0E] & 0x0F;
                    if (vol > 0 && (nesRegs[0x15] & 0x08)) {
                        static const uint16_t nesNoisePeriod[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
                        float freq = clkNES / (16.0f * (float)nesNoisePeriod[period]);
                        float midi = std::clamp(FreqToMidi(freq), 24.0f, 96.0f);
                        NoteOn(nesBase + 3, curSample, midi, (float)vol / 15.0f);
                    } else if (vol == 0) {
                        NoteOff(nesBase + 3, curSample);
                    }
                }
                // DMC / DPCM (regs 16..19, 21)
                else if (reg == 0x11) {
                    if (data > 0 && (nesRegs[0x15] & 0x10)) {
                        NoteOn(nesBase + 4, curSample, 36.0f, (float)data / 127.0f);
                    } else {
                        NoteOff(nesBase + 4, curSample);
                    }
                }
                else if (reg == 0x10) {
                    if (nesRegs[0x15] & 0x10) {
                        static const uint16_t nesDmcRate[16] = {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54};
                        float freq = clkNES / (float)nesDmcRate[data & 0x0F];
                        NoteOn(nesBase + 4, curSample, std::clamp(FreqToMidi(freq), 28.0f, 60.0f), 0.8f);
                    }
                }
                // Status register 0x15 (Channel enables)
                else if (reg == 0x15) {
                    if (!(data & 0x01)) NoteOff(nesBase + 0, curSample);
                    if (!(data & 0x02)) NoteOff(nesBase + 1, curSample);
                    if (!(data & 0x04)) NoteOff(nesBase + 2, curSample);
                    if (!(data & 0x08)) NoteOff(nesBase + 3, curSample);
                    if (!(data & 0x10)) NoteOff(nesBase + 4, curSample);
                    else if (data & 0x10) {
                        static const uint16_t nesDmcRate[16] = {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54};
                        float freq = clkNES / (float)nesDmcRate[nesRegs[0x10] & 0x0F];
                        NoteOn(nesBase + 4, curSample, std::clamp(FreqToMidi(freq), 28.0f, 60.0f), 0.8f);
                    }
                }
            } else if (reg >= 0x20) {
                // FDS registers
                uint8_t ofs = reg & 0x7F;
                nesFdsRegs[ofs] = data;
                int fdsCh = (nesFdsCh >= 0) ? nesFdsCh : (nesBase >= 0 ? (nesBase + 5) : -1);
                if (fdsCh >= 0 && fdsCh < (int)m_channels.size()) {
                    if (ofs == 0x20 || ofs == 0x22 || ofs == 0x23) {
                        uint8_t vol = nesFdsRegs[0x20] & 0x3F;
                        bool halt = (nesFdsRegs[0x23] & 0x80) != 0;
                        uint16_t freqVal = ((uint16_t)(nesFdsRegs[0x23] & 0x0F) << 8) | nesFdsRegs[0x22];
                        if (!halt && vol > 0 && freqVal > 0) {
                            float freq = (clkNES * (float)freqVal) / 4194304.0f;
                            float midi = FreqToMidi(freq);
                            NoteOn(fdsCh, curSample, midi, std::clamp((float)vol / 32.0f, 0.2f, 1.0f));
                        } else if (halt || vol == 0) {
                            NoteOff(fdsCh, curSample);
                        }
                    }
                }
            }
            break;
        }

        // MultiPCM write (0xB5 aa dd)
        case 0xB5: {
            if (filePos + 3 > dataSize) break;
            uint8_t offset = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (multiPcmBase >= 0) {
                if (offset == 1) {
                    static const int VALUE_TO_CHANNEL[32] = {
                        0, 1, 2, 3, 4, 5, 6, -1,
                        7, 8, 9, 10, 11, 12, 13, -1,
                        14, 15, 16, 17, 18, 19, 20, -1,
                        21, 22, 23, 24, 25, 26, 27, -1
                    };
                    multiPcmSlot = VALUE_TO_CHANNEL[data & 0x1F];
                } else if (offset == 2) {
                    multiPcmReg = (data > 7) ? 7 : data;
                } else if (offset == 0) {
                    if (multiPcmSlot >= 0 && multiPcmSlot < 28) {
                        int ch = multiPcmSlot;
                        int reg = multiPcmReg;
                        multiPcmRegs[ch][reg] = data;

                        if (reg == 4) {
                            if (data & 0x80) { // KeyOn
                                uint8_t r2 = multiPcmRegs[ch][2];
                                uint8_t r3 = multiPcmRegs[ch][3];
                                int octRaw = r3 >> 4;
                                int oct = (octRaw - 1) & 0x0F;
                                float octVal = (oct & 0x8) ? (float)(oct - 16) : (float)oct;
                                int pitch = ((r3 & 0x0F) << 6) | (r2 >> 2);
                                float pitchOctave = octVal + (float)pitch / 1024.0f;
                                float midi = std::clamp(60.0f + pitchOctave * 12.0f, 12.0f, 120.0f);
                                NoteOn(multiPcmBase + ch, curSample, midi, 0.85f);
                            } else { // KeyOff
                                NoteOff(multiPcmBase + ch, curSample);
                            }
                        } else if (reg == 2 || reg == 3) {
                            if (multiPcmBase + ch < (int)m_openNotes.size() && m_openNotes[multiPcmBase + ch].active) {
                                uint8_t r2 = multiPcmRegs[ch][2];
                                uint8_t r3 = multiPcmRegs[ch][3];
                                int octRaw = r3 >> 4;
                                int oct = (octRaw - 1) & 0x0F;
                                float octVal = (oct & 0x8) ? (float)(oct - 16) : (float)oct;
                                int pitch = ((r3 & 0x0F) << 6) | (r2 >> 2);
                                float pitchOctave = octVal + (float)pitch / 1024.0f;
                                float midi = std::clamp(60.0f + pitchOctave * 12.0f, 12.0f, 120.0f);
                                NoteOn(multiPcmBase + ch, curSample, midi, 0.85f);
                            }
                        }
                    }
                }
            }
            break;
        }

        // OKIM6295 write (0xB8 rr dd)
        case 0xB8: {
            if (filePos + 3 > dataSize) break;
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (okiBase >= 0) {
                if (data & 0x80) {
                    int ch = (data >> 3) & 3;
                    NoteOn(okiBase + ch, curSample, 48.0f, 0.85f);
                } else {
                    int chMask = (data >> 3) & 0x0F;
                    for (int ch = 0; ch < 4; ++ch) {
                        if (chMask & (1 << ch)) NoteOff(okiBase + ch, curSample);
                    }
                }
            }
            break;
        }

        // HuC6280 / C6280 write (0xB9 rr dd)
        case 0xB9: {
            if (filePos + 3 > dataSize) break;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (pceBase >= 0) {
                if (reg == 0x00) {
                    pceCurCh = data & 0x07;
                } else if (pceCurCh < 6) {
                    pceRegs[pceCurCh][reg & 0x0F] = data;
                    if (reg == 0x02 || reg == 0x03 || reg == 0x04) {
                        uint16_t period = ((uint16_t)(pceRegs[pceCurCh][3] & 0x0F) << 8) | pceRegs[pceCurCh][2];
                        uint8_t ctrl = pceRegs[pceCurCh][4];
                        bool chOn = (ctrl & 0x80) != 0;
                        uint8_t vol = ctrl & 0x1F;
                        int globalCh = pceBase + pceCurCh;
                        if (chOn && vol > 0 && period >= 2) {
                            float freq = 3579545.0f / (32.0f * (float)(period + 1));
                            NoteOn(globalCh, curSample, FreqToMidi(freq), (float)vol / 31.0f);
                        } else if (!chOn || vol == 0) {
                            NoteOff(globalCh, curSample);
                        }
                    }
                }
            }
            break;
        }

        // SAA1099 write (0xBD rr dd)
        case 0xBD: {
            if (filePos + 3 > dataSize) break;
            uint8_t reg = vgmData[filePos + 1];
            uint8_t data = vgmData[filePos + 2];
            filePos += 3;

            if (saaBase >= 0 && reg < 32) {
                saaRegs[reg] = data;
                if (reg >= 0x08 && reg <= 0x0D) {
                    saaFreq[reg - 0x08] = data;
                } else if (reg >= 0x10 && reg <= 0x12) {
                    int chPair = (reg - 0x10) * 2;
                    saaOctave[chPair] = data & 0x07;
                    saaOctave[chPair + 1] = (data >> 4) & 0x07;
                }
                for (int ch = 0; ch < 6; ++ch) {
                    bool enabled = (saaRegs[0x14] & (1 << ch)) != 0;
                    uint8_t ampL = saaRegs[ch] & 0x0F;
                    uint8_t ampR = (saaRegs[ch] >> 4) & 0x0F;
                    uint8_t maxAmp = std::max(ampL, ampR);
                    int globalCh = saaBase + ch;
                    if (enabled && maxAmp > 0 && saaFreq[ch] < 255) {
                        float freq = (8000000.0f * (float)(1 << saaOctave[ch])) / (512.0f * (float)(511 - (saaFreq[ch] * 2)));
                        NoteOn(globalCh, curSample, FreqToMidi(freq), (float)maxAmp / 15.0f);
                    } else if (!enabled || maxAmp == 0) {
                        NoteOff(globalCh, curSample);
                    }
                }
            }
            break;
        }

        // SegaPCM write (0xC0 aaaa dd)
        case 0xC0: {
            if (filePos + 4 > dataSize) break;
            uint16_t addr = ReadLE16(&vgmData[filePos + 1]);
            uint8_t data = vgmData[filePos + 3];
            filePos += 4;

            if (segaPcmBase >= 0 && addr < 0x0100) {
                int ch = (addr & 0x7F) / 8;
                int reg = addr & 0x07;
                if (ch >= 0 && ch < 16) {
                    if (addr >= 0x80) {
                        segaPcmHighRegs[ch][reg] = data;
                    } else {
                        segaPcmLowRegs[ch][reg] = data;
                    }

                    // High reg 6: Bit 0 is Channel Disable (1 = disabled/stop, 0 = active/playing)
                    bool disabled = (segaPcmHighRegs[ch][6] & 0x01) != 0;
                    uint8_t pitch = segaPcmLowRegs[ch][7];
                    uint8_t volL = segaPcmLowRegs[ch][2] & 0x7F;
                    uint8_t volR = segaPcmLowRegs[ch][3] & 0x7F;
                    uint8_t vol = std::max(volL, volR);
                    int globalCh = segaPcmBase + ch;

                    if (!disabled && vol > 0 && pitch > 0) {
                        float freq = 31250.0f * ((float)pitch / 256.0f);
                        float midi = FreqToMidi(freq);
                        if (midi < 12.0f || midi > 120.0f) midi = 60.0f;
                        NoteOn(globalCh, curSample, midi, (float)vol / 127.0f);
                    } else {
                        NoteOff(globalCh, curSample);
                    }
                }
            }
            break;
        }

        // QSound write (0xC4 mm ll dd)
        case 0xC4: {
            if (filePos + 4 > dataSize) break;
            uint8_t addr = vgmData[filePos + 1];
            uint8_t dhi = vgmData[filePos + 2];
            uint8_t dlo = vgmData[filePos + 3];
            filePos += 4;

            if (qsoundBase >= 0 && addr < 16) {
                uint16_t pitch = ((uint16_t)dhi << 8) | dlo;
                int globalCh = qsoundBase + addr;
                if (pitch > 0) {
                    float freq = ((float)pitch * 4000000.0f) / 16777216.0f;
                    float midi = FreqToMidi(freq);
                    if (midi < 12.0f || midi > 120.0f) midi = 60.0f;
                    NoteOn(globalCh, curSample, midi, 0.85f);
                } else {
                    NoteOff(globalCh, curSample);
                }
            }
            break;
        }

        // K051649 / SCC write (0xD2 pp rr dd)
        case 0xD2: {
            if (filePos + 4 > dataSize) break;
            uint8_t port = vgmData[filePos + 1];
            uint8_t reg = vgmData[filePos + 2];
            uint8_t data = vgmData[filePos + 3];
            filePos += 4;

            if (sccBase >= 0) {
                if (port == 1 && reg < 10) {
                    int ch = reg / 2;
                    if (reg & 1) sccFreq[ch] = (sccFreq[ch] & 0x00FF) | ((uint16_t)(data & 0x0F) << 8);
                    else sccFreq[ch] = (sccFreq[ch] & 0x0F00) | data;
                    if (sccVol[ch] > 0 && sccFreq[ch] >= 9) {
                        float freq = 3579545.0f / (32.0f * (float)(sccFreq[ch] + 1));
                        NoteOn(sccBase + ch, curSample, FreqToMidi(freq), (float)sccVol[ch] / 15.0f);
                    }
                } else if (port == 2 && reg < 5) {
                    int ch = reg;
                    sccVol[ch] = data & 0x0F;
                    if (sccVol[ch] > 0 && sccFreq[ch] >= 9) {
                        float freq = 3579545.0f / (32.0f * (float)(sccFreq[ch] + 1));
                        NoteOn(sccBase + ch, curSample, FreqToMidi(freq), (float)sccVol[ch] / 15.0f);
                    } else if (sccVol[ch] == 0) {
                        NoteOff(sccBase + ch, curSample);
                    }
                } else if (port == 3) {
                    for (int ch = 0; ch < 5; ++ch) {
                        if (!(data & (1 << ch))) NoteOff(sccBase + ch, curSample);
                    }
                }
            }
            break;
        }

        // Sample Delays 0x70..0x7F (wait 1..16 samples)
        default: {
            if (cmd >= 0x70 && cmd <= 0x7F) {
                curSample += (cmd & 0x0F) + 1;
                filePos += 1;
            }
            // YM2612 DAC write + delay (0x80..0x8F)
            else if (cmd >= 0x80 && cmd <= 0x8F) {
                curSample += (cmd & 0x0F);
                filePos += 1;
            }
            // Data Block (0x67)
            else if (cmd == 0x67) {
                if (filePos + 7 > dataSize) { filePos = dataSize; break; }
                uint32_t bLen = ReadLE32(&vgmData[filePos + 3]) & 0x7FFFFFFF;
                filePos += 7 + bLen;
            }
            // PCM RAM write (0x68)
            else if (cmd == 0x68) {
                filePos += 12;
            }
            // DAC Control commands (0x90..0x95)
            else if (cmd >= 0x90 && cmd <= 0x95) {
                static const int dacLens[6] = {5, 5, 6, 11, 2, 5};
                filePos += dacLens[cmd - 0x90];
            }
            // Other commands lengths
            else if (cmd >= 0x40 && cmd <= 0x4E) filePos += 3;
            else if (cmd == 0x4F) filePos += 2;
            else if (cmd >= 0xA0 && cmd <= 0xBF) filePos += 3;
            else if (cmd >= 0xC0 && cmd <= 0xDF) filePos += 4;
            else if (cmd >= 0xE0) filePos += 5;
            else {
                filePos += 1;
            }
            break;
        }
        }
    }

    // Flush any remaining active notes
    FlushOpenNotes(std::max(curSample, totalSamples));

    if (m_minMidiNote > m_maxMidiNote) {
        m_minMidiNote = 36; // C2
        m_maxMidiNote = 84; // C6
    } else {
        // Expand range with 2-semitone padding for visual comfort
        m_minMidiNote = std::max(0, m_minMidiNote - 2);
        m_maxMidiNote = std::min(127, m_maxMidiNote + 2);
    }

    if (m_totalSamples == 0) {
        m_totalSamples = curSample;
    }

    // Sort notes by startSample
    std::sort(m_notes.begin(), m_notes.end(), [](const TrackedNote& a, const TrackedNote& b) {
        return a.startSample < b.startSample;
    });

    // Populate per-channel note lists
    m_channelNotes.resize(m_channels.size());
    for (const auto& note : m_notes) {
        if (note.channelId >= 0 && note.channelId < (int)m_channels.size()) {
            m_channelNotes[note.channelId].push_back(note);
        }
    }

    std::cout << "[NoteTracker] Processed VGM: " << m_channels.size() << " channels, "
              << m_notes.size() << " notes tracked. Pitch range: "
              << m_minMidiNote << " to " << m_maxMidiNote << std::endl;

    return true;
}

void NoteTracker::QueryNotesInWindow(uint32_t startSample, uint32_t endSample, std::vector<const TrackedNote*>& outNotes) const {
    outNotes.clear();
    if (m_notes.empty()) return;

    // Binary search to find notes that might be visible
    // Notes can start before startSample and still end after startSample,
    // so we search per channel
    for (size_t ch = 0; ch < m_channelNotes.size(); ++ch) {
        if (!m_channels[ch].visible) continue;

        const auto& cNotes = m_channelNotes[ch];
        if (cNotes.empty()) continue;

        // Find first note with endSample >= startSample
        auto it = std::lower_bound(cNotes.begin(), cNotes.end(), startSample,
            [](const TrackedNote& n, uint32_t t) {
                return n.endSample < t;
            });

        while (it != cNotes.end() && it->startSample <= endSample) {
            outNotes.push_back(&(*it));
            ++it;
        }
    }
}

void NoteTracker::UpdateLiveState(uint32_t curSample, float pitchShiftSemitones) {
    for (size_t ch = 0; ch < m_channels.size(); ++ch) {
        TrackedChannel& tc = m_channels[ch];
        tc.active = false;
        tc.curVolume = 0.0f;
        std::snprintf(tc.curNoteName, sizeof(tc.curNoteName), "---");

        if (ch >= m_channelNotes.size()) continue;
        const auto& cNotes = m_channelNotes[ch];
        if (cNotes.empty()) continue;

        auto it = std::lower_bound(cNotes.begin(), cNotes.end(), curSample,
            [](const TrackedNote& n, uint32_t t) {
                return n.endSample < t;
            });

        if (it != cNotes.end() && it->startSample <= curSample && curSample <= it->endSample) {
            tc.active = true;
            float shiftedMidi = it->midiNote + pitchShiftSemitones;
            tc.curMidiNote = shiftedMidi;
            tc.curVolume = it->volume;
            if (std::abs(pitchShiftSemitones) >= 0.01f) {
                MidiToNoteName(shiftedMidi, tc.curNoteName, sizeof(tc.curNoteName));
            } else {
                std::strncpy(tc.curNoteName, it->noteName, sizeof(tc.curNoteName));
            }
        }
    }
}

} // namespace vgmstudio
