#include "vgm_disassembly.hpp"
#include <cstring>
#include <cstdio>
#include <set>
#include <algorithm>

namespace vgmstudio {

static inline uint32_t ReadLE16(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static inline uint32_t ReadLE32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint32_t ReadRelOfs(const uint8_t* p, uint32_t baseOfs) {
    uint32_t rel = ReadLE32(&p[baseOfs]);
    return rel ? (baseOfs + rel) : 0;
}

static const char* YM2151_NoteName(uint8_t noteCode) {
    switch (noteCode) {
        case 0:  return "C#";
        case 1:  return "D";
        case 2:  return "D#";
        case 4:  return "E";
        case 5:  return "F";
        case 6:  return "F#";
        case 8:  return "G";
        case 9:  return "G#";
        case 10: return "A";
        case 12: return "A#";
        case 13: return "B";
        case 14: return "C";
        default: return "?";
    }
}

VgmDisassembly::VgmDisassembly() = default;
VgmDisassembly::~VgmDisassembly() = default;

void VgmDisassembly::Clear() {
    m_instructions.clear();
    m_detectedChips.clear();
    m_detectedChannels.clear();
    m_dataStartOfs = 0x40;
    m_dataEndOfs = 0;
    m_totalSamples = 0;
}

int VgmDisassembly::FindInstructionIndexAtSample(uint32_t sample) const {
    if (m_instructions.empty()) return -1;
    auto it = std::upper_bound(m_instructions.begin(), m_instructions.end(), sample,
        [](uint32_t val, const VgmInstruction& inst) {
            return val < inst.samplePos;
        });
    if (it == m_instructions.begin()) return 0;
    return (int)std::distance(m_instructions.begin(), it - 1);
}

int VgmDisassembly::FindInstructionIndexAtOffset(uint32_t offset) const {
    if (m_instructions.empty()) return -1;
    auto it = std::upper_bound(m_instructions.begin(), m_instructions.end(), offset,
        [](uint32_t val, const VgmInstruction& inst) {
            return val < inst.fileOffset;
        });
    if (it == m_instructions.begin()) return -1;
    int idx = (int)std::distance(m_instructions.begin(), it - 1);
    const auto& inst = m_instructions[idx];
    if (offset >= inst.fileOffset && offset < inst.fileOffset + inst.length) {
        return idx;
    }
    return -1;
}

bool VgmDisassembly::Disassemble(const uint8_t* vgmData, size_t dataSize) {
    Clear();

    if (!vgmData || dataSize < 0x40) return false;
    if (std::memcmp(vgmData, "Vgm ", 4) != 0) return false;

    uint32_t fileVer = ReadLE32(&vgmData[0x08]);
    uint32_t dataOfs = (fileVer >= 0x150) ? ReadRelOfs(vgmData, 0x34) : 0x40;
    if (!dataOfs) dataOfs = 0x40;
    if (dataOfs >= dataSize) return false;

    uint32_t eofOfs = ReadRelOfs(vgmData, 0x04);
    if (!eofOfs || eofOfs > dataSize) eofOfs = (uint32_t)dataSize;

    m_totalSamples = ReadLE32(&vgmData[0x18]);
    m_dataStartOfs = dataOfs;
    m_dataEndOfs = eofOfs;

    m_instructions.reserve(std::min((size_t)65536, dataSize / 3));
    std::set<std::string> uniqueChips;
    std::set<std::string> uniqueChannels;

    size_t filePos = dataOfs;
    uint32_t curSample = 0;
    uint32_t instIdx = 0;
    int rf5cDisasmCurCh = 0;

    while (filePos < eofOfs && filePos < dataSize) {
        uint8_t cmd = vgmData[filePos];
        VgmInstruction inst;
        inst.index = instIdx++;
        inst.fileOffset = (uint32_t)filePos;
        inst.samplePos = curSample;
        inst.cmd = cmd;
        inst.length = 1;

        if (cmd == 0x66) {
            // End of sound data
            inst.length = 1;
            std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "66");
            std::snprintf(inst.chip, sizeof(inst.chip), "End of Sound");
            std::snprintf(inst.desc, sizeof(inst.desc), "End of Sound Data (Loop/Finish)");
            inst.category = InstCategory::Other;
            m_instructions.push_back(inst);
            filePos++;
            break;
        }

        switch (cmd) {
        // --- 0x30..0x3F: Dual chip / Stereo ---
        case 0x30: { // SN76489 2nd chip
            inst.length = 2;
            if (filePos + 2 <= dataSize) {
                uint8_t d = vgmData[filePos + 1];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "30 %02X", d);
                std::snprintf(inst.chip, sizeof(inst.chip), "SN76489 #2");
                std::snprintf(inst.desc, sizeof(inst.desc), "Write 0x%02X", d);
                inst.category = InstCategory::PSG_Square;
                uniqueChips.insert("SN76489 #2");
            }
            break;
        }
        case 0x31: { // AY8910 stereo mask
            inst.length = 2;
            if (filePos + 2 <= dataSize) {
                uint8_t d = vgmData[filePos + 1];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "31 %02X", d);
                std::snprintf(inst.chip, sizeof(inst.chip), "AY-3-8910");
                std::snprintf(inst.desc, sizeof(inst.desc), "Stereo Mask = 0x%02X", d);
                inst.category = InstCategory::PSG_Square;
                uniqueChips.insert("AY-3-8910");
            }
            break;
        }
        case 0x3F: { // GG stereo 2nd chip
            inst.length = 2;
            if (filePos + 2 <= dataSize) {
                uint8_t d = vgmData[filePos + 1];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "3F %02X", d);
                std::snprintf(inst.chip, sizeof(inst.chip), "Game Gear #2");
                std::snprintf(inst.desc, sizeof(inst.desc), "Stereo Mask = 0x%02X", d);
                inst.category = InstCategory::PSG_Square;
                uniqueChips.insert("Game Gear #2");
            }
            break;
        }
        case 0x4F: { // Game Gear PSG Stereo
            inst.length = 2;
            if (filePos + 2 <= dataSize) {
                uint8_t d = vgmData[filePos + 1];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "4F %02X", d);
                std::snprintf(inst.chip, sizeof(inst.chip), "Game Gear");
                std::snprintf(inst.desc, sizeof(inst.desc), "Stereo Mask = 0x%02X [L:%s R:%s]", d, 
                    (d & 0xF0) ? "Active" : "Off", (d & 0x0F) ? "Active" : "Off");
                inst.category = InstCategory::PSG_Square;
                uniqueChips.insert("Game Gear");
            }
            break;
        }

        // --- 0x50: SN76489 / PSG ---
        case 0x50: {
            inst.length = 2;
            if (filePos + 2 <= dataSize) {
                uint8_t d = vgmData[filePos + 1];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "50 %02X", d);
                std::snprintf(inst.chip, sizeof(inst.chip), "SN76489");
                inst.category = InstCategory::PSG_Square;
                uniqueChips.insert("SN76489");

                if (d & 0x80) {
                    int ch = (d >> 5) & 3;
                    if (d & 0x10) {
                        int atten = d & 0x0F;
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Volume = %d (%s)", 
                                      ch + 1, atten, (atten == 15 ? "Muted" : (atten == 0 ? "Max / 0dB" : "Active")));
                        if (atten == 15) inst.isKeyOff = true;
                        else inst.isKeyOn = true;
                    } else {
                        if (ch < 3) {
                            std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Tone Period Low = 0x%X", ch + 1, d & 0x0F);
                        } else {
                            std::snprintf(inst.desc, sizeof(inst.desc), "Noise Control = 0x%X (%s)", d & 0x07, (d & 4) ? "White" : "Periodic");
                        }
                    }
                } else {
                    std::snprintf(inst.desc, sizeof(inst.desc), "Tone Period High = 0x%X", d & 0x3F);
                }
            }
            break;
        }

        // --- 0x51: YM2413 ---
        case 0x51: {
            inst.length = 3;
            if (filePos + 3 <= dataSize) {
                uint8_t reg = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "51 %02X %02X", reg, val);
                std::snprintf(inst.chip, sizeof(inst.chip), "YM2413");
                inst.category = InstCategory::FM;
                uniqueChips.insert("YM2413");

                if (reg >= 0x10 && reg <= 0x18) {
                    int ch = reg - 0x10 + 1;
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d F-Num Low = 0x%02X", ch, val);
                } else if (reg >= 0x20 && reg <= 0x28) {
                    int ch = reg - 0x20 + 1;
                    bool kon = (val & 0x10) != 0;
                    int blk = (val >> 1) & 7;
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d %s [Blk %d, F-NumH %d]", ch, kon ? "Key On" : "Key Off", blk, val & 1);
                    if (kon) inst.isKeyOn = true; else inst.isKeyOff = true;
                } else if (reg >= 0x30 && reg <= 0x38) {
                    int ch = reg - 0x30 + 1;
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Inst=%d, Vol=%d", ch, (val >> 4) & 0x0F, val & 0x0F);
                } else if (reg == 0x0E) {
                    std::snprintf(inst.desc, sizeof(inst.desc), "Rhythm Control = 0x%02X", val);
                } else {
                    std::snprintf(inst.desc, sizeof(inst.desc), "Reg 0x%02X = 0x%02X", reg, val);
                }
            }
            break;
        }

        // --- 0x52 / 0x53: YM2612 Port 0 / Port 1 ---
        case 0x52:
        case 0x53: {
            inst.length = 3;
            if (filePos + 3 <= dataSize) {
                uint8_t port = (cmd == 0x53) ? 1 : 0;
                uint8_t reg = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "%02X %02X %02X", cmd, reg, val);
                std::snprintf(inst.chip, sizeof(inst.chip), port ? "YM2612 P1" : "YM2612 P0");
                inst.category = InstCategory::FM;
                uniqueChips.insert("YM2612");

                if (port == 0 && reg == 0x28) {
                    int ch = val & 0x03;
                    if (val & 0x04) ch += 3;
                    uint8_t ops = (val >> 4) & 0x0F;
                    if (ops != 0) {
                        inst.isKeyOn = true;
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Key On [Ops:%s%s%s%s]", ch + 1,
                                      (ops & 1) ? " 1" : "", (ops & 2) ? " 2" : "", (ops & 4) ? " 3" : "", (ops & 8) ? " 4" : "");
                    } else {
                        inst.isKeyOff = true;
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Key Off", ch + 1);
                    }
                } else if (port == 0 && reg == 0x2A) {
                    std::snprintf(inst.desc, sizeof(inst.desc), "DAC Out = 0x%02X (%d)", val, (int8_t)val);
                } else if (port == 0 && reg == 0x2B) {
                    std::snprintf(inst.desc, sizeof(inst.desc), "DAC Enable = %s", (val & 0x80) ? "ON" : "OFF");
                } else if (reg >= 0xA0 && reg <= 0xA2) {
                    int ch = (reg - 0xA0) + (port ? 4 : 1);
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d F-Num Low = 0x%02X", ch, val);
                } else if (reg >= 0xA4 && reg <= 0xA6) {
                    int ch = (reg - 0xA4) + (port ? 4 : 1);
                    int blk = (val >> 3) & 7;
                    int fnh = val & 7;
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Block %d, F-Num High 0x%X", ch, blk, fnh);
                } else if (reg >= 0xB0 && reg <= 0xB2) {
                    int ch = (reg - 0xB0) + (port ? 4 : 1);
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d FB=%d, Alg=%d", ch, (val >> 3) & 7, val & 7);
                } else if (reg >= 0xB4 && reg <= 0xB6) {
                    int ch = (reg - 0xB4) + (port ? 4 : 1);
                    const char* pan = ((val & 0xC0) == 0xC0) ? "L+R" : ((val & 0x80) ? "L" : ((val & 0x40) ? "R" : "Mute"));
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Pan: %s (AMS:%d, FMS:%d)", ch, pan, (val >> 4) & 3, val & 7);
                } else if (reg >= 0x40 && reg <= 0x4F) {
                    int ch = (reg & 3) + (port ? 4 : 1);
                    int op = ((reg >> 2) & 3) + 1;
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Op %d TL = %d", ch, op, val & 0x7F);
                } else {
                    std::snprintf(inst.desc, sizeof(inst.desc), "Reg 0x%02X = 0x%02X", reg, val);
                }
            }
            break;
        }

        // --- 0x54: YM2151 (OPM) ---
        case 0x54: {
            inst.length = 3;
            if (filePos + 3 <= dataSize) {
                uint8_t reg = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "54 %02X %02X", reg, val);
                std::snprintf(inst.chip, sizeof(inst.chip), "YM2151");
                inst.category = InstCategory::FM;
                uniqueChips.insert("YM2151");

                if (reg == 0x08) {
                    int ch = (val & 0x07) + 1;
                    uint8_t ops = (val >> 3) & 0x0F;
                    if (ops != 0) {
                        inst.isKeyOn = true;
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Key On [Ops:%s%s%s%s]", ch,
                                      (ops & 1) ? " M1" : "", (ops & 2) ? " C1" : "", (ops & 4) ? " M2" : "", (ops & 8) ? " C2" : "");
                    } else {
                        inst.isKeyOff = true;
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Key Off", ch);
                    }
                } else if (reg >= 0x20 && reg <= 0x27) {
                    int ch = (reg - 0x20) + 1;
                    const char* pan = ((val & 0xC0) == 0xC0) ? "L+R" : ((val & 0x80) ? "L" : ((val & 0x40) ? "R" : "Mute"));
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Pan: %s, FB:%d, Alg:%d", ch, pan, (val >> 3) & 7, val & 7);
                } else if (reg >= 0x28 && reg <= 0x2F) {
                    int ch = (reg - 0x28) + 1;
                    int oct = (val >> 4) & 7;
                    const char* nName = YM2151_NoteName(val & 0x0F);
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Key Code 0x%02X (%s%d)", ch, val, nName, oct);
                } else if (reg >= 0x30 && reg <= 0x37) {
                    int ch = (reg - 0x30) + 1;
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Key Fraction = %d", ch, val >> 2);
                } else if (reg >= 0x60 && reg <= 0x7F) {
                    int ch = (reg & 7) + 1;
                    int op = ((reg - 0x60) >> 3) + 1;
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Op %d TL = %d", ch, op, val & 0x7F);
                } else if (reg == 0x0F) {
                    std::snprintf(inst.desc, sizeof(inst.desc), "Noise Freq=%d, NE=%d", val & 0x1F, (val >> 7) & 1);
                } else {
                    std::snprintf(inst.desc, sizeof(inst.desc), "Reg 0x%02X = 0x%02X", reg, val);
                }
            }
            break;
        }

        // --- 0x55: YM2203 (OPN + SSG) ---
        case 0x55: {
            inst.length = 3;
            if (filePos + 3 <= dataSize) {
                uint8_t reg = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "55 %02X %02X", reg, val);
                std::snprintf(inst.chip, sizeof(inst.chip), "YM2203");
                inst.category = (reg < 0x10) ? InstCategory::PSG_Square : InstCategory::FM;
                uniqueChips.insert("YM2203");

                if (reg < 0x06) {
                    char ch = 'A' + (reg / 2);
                    std::snprintf(inst.desc, sizeof(inst.desc), "SSG Ch %c Period %s = 0x%02X", ch, (reg & 1) ? "High" : "Low", val);
                } else if (reg >= 0x08 && reg <= 0x0A) {
                    char ch = 'A' + (reg - 0x08);
                    std::snprintf(inst.desc, sizeof(inst.desc), "SSG Ch %c Volume = %d", ch, val & 0x1F);
                } else if (reg == 0x28) {
                    int ch = (val & 3) + 1;
                    uint8_t ops = (val >> 4) & 0x0F;
                    if (ops != 0) {
                        inst.isKeyOn = true;
                        std::snprintf(inst.desc, sizeof(inst.desc), "FM Ch %d Key On [Ops:%s%s%s%s]", ch,
                                      (ops & 1) ? " 1" : "", (ops & 2) ? " 2" : "", (ops & 4) ? " 3" : "", (ops & 8) ? " 4" : "");
                    } else {
                        inst.isKeyOff = true;
                        std::snprintf(inst.desc, sizeof(inst.desc), "FM Ch %d Key Off", ch);
                    }
                } else {
                    std::snprintf(inst.desc, sizeof(inst.desc), "Reg 0x%02X = 0x%02X", reg, val);
                }
            }
            break;
        }

        // --- 0x56/0x57: YM2608, 0x58/0x59: YM2610, 0x5A/0x5B/0x5C: OPL, 0x5E/0x5F: OPL3 ---
        case 0x56: case 0x57: case 0x58: case 0x59:
        case 0x5A: case 0x5B: case 0x5C: case 0x5D:
        case 0x5E: case 0x5F: {
            inst.length = 3;
            if (filePos + 3 <= dataSize) {
                uint8_t reg = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                const char* chipName = "FM Chip";
                if (cmd == 0x56 || cmd == 0x57) chipName = "YM2608";
                else if (cmd == 0x58 || cmd == 0x59) chipName = "YM2610";
                else if (cmd == 0x5A) chipName = "YM3812";
                else if (cmd == 0x5B) chipName = "YM3526";
                else if (cmd == 0x5C) chipName = "Y8950";
                else if (cmd == 0x5D) chipName = "YMZ280B";
                else if (cmd == 0x5E || cmd == 0x5F) chipName = "YMF262";

                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "%02X %02X %02X", cmd, reg, val);
                std::snprintf(inst.chip, sizeof(inst.chip), "%s", chipName);
                std::snprintf(inst.desc, sizeof(inst.desc), "Reg 0x%02X = 0x%02X", reg, val);
                inst.category = InstCategory::FM;
                uniqueChips.insert(chipName);
            }
            break;
        }

        // --- 0x61: Wait N samples ---
        case 0x61: {
            inst.length = 3;
            if (filePos + 3 <= dataSize) {
                uint16_t waitSmpl = ReadLE16(&vgmData[filePos + 1]);
                float ms = (waitSmpl / 44100.0f) * 1000.0f;
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "61 %02X %02X", vgmData[filePos + 1], vgmData[filePos + 2]);
                std::snprintf(inst.chip, sizeof(inst.chip), "Wait");
                std::snprintf(inst.desc, sizeof(inst.desc), "Wait %u samples (%.2f ms)", waitSmpl, ms);
                inst.category = InstCategory::Delays;
                inst.isWait = true;
                curSample += waitSmpl;
            }
            break;
        }

        // --- 0x62: Wait 735 samples (60 Hz NTSC) ---
        case 0x62: {
            inst.length = 1;
            std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "62");
            std::snprintf(inst.chip, sizeof(inst.chip), "Wait 60Hz");
            std::snprintf(inst.desc, sizeof(inst.desc), "Wait 735 samples (1/60s NTSC, 16.67 ms)");
            inst.category = InstCategory::Delays;
            inst.isWait = true;
            curSample += 735;
            break;
        }

        // --- 0x63: Wait 882 samples (50 Hz PAL) ---
        case 0x63: {
            inst.length = 1;
            std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "63");
            std::snprintf(inst.chip, sizeof(inst.chip), "Wait 50Hz");
            std::snprintf(inst.desc, sizeof(inst.desc), "Wait 882 samples (1/50s PAL, 20.00 ms)");
            inst.category = InstCategory::Delays;
            inst.isWait = true;
            curSample += 882;
            break;
        }

        // --- 0x67: Data Block ---
        case 0x67: {
            if (filePos + 7 <= dataSize) {
                uint8_t blkType = vgmData[filePos + 2];
                uint32_t blkSize = ReadLE32(&vgmData[filePos + 3]);
                inst.length = 7 + blkSize;
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "67 66 %02X ..", blkType);
                std::snprintf(inst.chip, sizeof(inst.chip), "Data Block");
                std::snprintf(inst.desc, sizeof(inst.desc), "Type 0x%02X, Size: %u bytes", blkType, blkSize);
                inst.category = InstCategory::DataBlock;
                uniqueChips.insert("Data Block");
            } else {
                inst.length = 1;
            }
            break;
        }

        // --- 0x68: PCM RAM write ---
        case 0x68: {
            inst.length = 12;
            if (filePos + 12 <= dataSize) {
                uint8_t bType = vgmData[filePos + 2];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "68 66 %02X ..", bType);
                std::snprintf(inst.chip, sizeof(inst.chip), "PCM RAM");
                std::snprintf(inst.channel, sizeof(inst.channel), "RAM");
                std::snprintf(inst.desc, sizeof(inst.desc), "PCM RAM Transfer (Type 0x%02X, 12B)", bType);
                inst.category = InstCategory::DataBlock;
                uniqueChips.insert("PCM RAM");
            } else {
                inst.length = 1;
            }
            break;
        }

        // --- 0x70..0x7F: Short wait ---
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B:
        case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
            inst.length = 1;
            uint32_t waitSmpl = (cmd & 0x0F) + 1;
            std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "%02X", cmd);
            std::snprintf(inst.chip, sizeof(inst.chip), "Wait");
            std::snprintf(inst.desc, sizeof(inst.desc), "Wait %u sample%s (%.3f ms)", 
                          waitSmpl, (waitSmpl > 1 ? "s" : ""), (waitSmpl / 44100.0f) * 1000.0f);
            inst.category = InstCategory::Delays;
            inst.isWait = true;
            curSample += waitSmpl;
            break;
        }

        // --- 0x80..0x8F: YM2612 PCM Write & Wait ---
        case 0x80: case 0x81: case 0x82: case 0x83:
        case 0x84: case 0x85: case 0x86: case 0x87:
        case 0x88: case 0x89: case 0x8A: case 0x8B:
        case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
            inst.length = 1;
            uint32_t waitSmpl = cmd & 0x0F;
            std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "%02X", cmd);
            std::snprintf(inst.chip, sizeof(inst.chip), "YM2612 DAC");
            std::snprintf(inst.desc, sizeof(inst.desc), "DAC Stream Sample & Wait %u samples", waitSmpl);
            inst.category = InstCategory::FM;
            uniqueChips.insert("YM2612 DAC");
            if (waitSmpl > 0) curSample += waitSmpl;
            break;
        }

        // --- 0x90..0x95: DAC Stream Control ---
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: {
            inst.category = InstCategory::Other;
            std::snprintf(inst.chip, sizeof(inst.chip), "DAC Control");
            uniqueChips.insert("DAC Control");
            if (cmd == 0x90 && filePos + 5 <= dataSize) {
                inst.length = 5;
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "90 %02X %02X %02X %02X", 
                              vgmData[filePos+1], vgmData[filePos+2], vgmData[filePos+3], vgmData[filePos+4]);
                std::snprintf(inst.desc, sizeof(inst.desc), "Setup Stream %d", vgmData[filePos+1]);
            } else if (cmd == 0x91 && filePos + 5 <= dataSize) {
                inst.length = 5;
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "91 %02X %02X ..", vgmData[filePos+1], vgmData[filePos+2]);
                std::snprintf(inst.desc, sizeof(inst.desc), "Set Data Stream %d", vgmData[filePos+1]);
            } else if (cmd == 0x92 && filePos + 6 <= dataSize) {
                inst.length = 6;
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "92 %02X %02X ..", vgmData[filePos+1], vgmData[filePos+2]);
                std::snprintf(inst.desc, sizeof(inst.desc), "Set Stream %d Freq", vgmData[filePos+1]);
            } else if (cmd == 0x93 && filePos + 11 <= dataSize) {
                inst.length = 11;
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "93 %02X ..", vgmData[filePos+1]);
                std::snprintf(inst.desc, sizeof(inst.desc), "Play Data Stream %d", vgmData[filePos+1]);
            } else if (cmd == 0x94 && filePos + 2 <= dataSize) {
                inst.length = 2;
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "94 %02X", vgmData[filePos+1]);
                std::snprintf(inst.desc, sizeof(inst.desc), "Stop Stream %d", vgmData[filePos+1]);
            } else if (cmd == 0x95 && filePos + 5 <= dataSize) {
                inst.length = 5;
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "95 %02X ..", vgmData[filePos+1]);
                std::snprintf(inst.desc, sizeof(inst.desc), "Play Stream %d Block", vgmData[filePos+1]);
            } else {
                inst.length = 1;
            }
            break;
        }

        // --- 0xA0: AY-3-8910 ---
        case 0xA0: {
            inst.length = 3;
            if (filePos + 3 <= dataSize) {
                uint8_t reg = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "A0 %02X %02X", reg, val);
                std::snprintf(inst.chip, sizeof(inst.chip), "AY-3-8910");
                inst.category = InstCategory::PSG_Square;
                uniqueChips.insert("AY-3-8910");

                if (reg <= 0x05) {
                    char ch = 'A' + (reg / 2);
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %c Period %s = 0x%02X", ch, (reg & 1) ? "High" : "Low", val);
                } else if (reg >= 0x08 && reg <= 0x0A) {
                    char ch = 'A' + (reg - 0x08);
                    std::snprintf(inst.desc, sizeof(inst.desc), "Ch %c Volume = %d", ch, val & 0x1F);
                } else {
                    std::snprintf(inst.desc, sizeof(inst.desc), "Reg 0x%02X = 0x%02X", reg, val);
                }
            }
            break;
        }

        // --- 0xB0 / 0xB1: RF5C68 / RF5C164 ---
        case 0xB0:
        case 0xB1: {
            inst.length = 3;
            if (filePos + 3 <= dataSize) {
                uint8_t regRaw = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                uint8_t reg = regRaw & 0x7F;
                int chipId = (regRaw & 0x80) ? 1 : 0;
                const char* chipName = (cmd == 0xB1) ? "RF5C164" : "RF5C68";
                std::string chipStr = (chipId > 0) ? (std::string(chipName) + " #2") : chipName;

                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "%02X %02X %02X", cmd, regRaw, val);
                std::snprintf(inst.chip, sizeof(inst.chip), "%s", chipStr.c_str());
                inst.category = InstCategory::SoundChips;
                uniqueChips.insert(chipStr);

                if (reg == 0x07) {
                    if (val & 0x40) {
                        rf5cDisasmCurCh = val & 7;
                        std::snprintf(inst.desc, sizeof(inst.desc), "Channel Select: Ch %d", rf5cDisasmCurCh + 1);
                        std::snprintf(inst.channel, sizeof(inst.channel), "Ch %d", rf5cDisasmCurCh + 1);
                    } else {
                        std::snprintf(inst.desc, sizeof(inst.desc), "RAM Bank Select = %d", val & 0x0F);
                        std::snprintf(inst.channel, sizeof(inst.channel), "RAM Bank");
                    }
                } else if (reg == 0x08) {
                    std::snprintf(inst.desc, sizeof(inst.desc), "Channel On/Off Mask = 0x%02X", val);
                    std::snprintf(inst.channel, sizeof(inst.channel), "Control");
                    if (val == 0xFF) inst.isKeyOff = true;
                    else inst.isKeyOn = true;
                } else {
                    int chNum = rf5cDisasmCurCh + 1;
                    std::snprintf(inst.channel, sizeof(inst.channel), "Ch %d", chNum);
                    if (reg == 0x00) {
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Envelope = %d", chNum, val);
                    } else if (reg == 0x01) {
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Pan = 0x%02X (L:%d R:%d)", chNum, val, val & 0x0F, (val >> 4) & 0x0F);
                    } else if (reg == 0x02) {
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Freq Step Low = 0x%02X", chNum, val);
                    } else if (reg == 0x03) {
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Freq Step High = 0x%02X", chNum, val);
                    } else if (reg == 0x04) {
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Loop Low = 0x%02X", chNum, val);
                    } else if (reg == 0x05) {
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Loop High = 0x%02X", chNum, val);
                    } else if (reg == 0x06) {
                        std::snprintf(inst.desc, sizeof(inst.desc), "Ch %d Start Addr = 0x%02X", chNum, val);
                    } else {
                        std::snprintf(inst.desc, sizeof(inst.desc), "Reg 0x%02X = 0x%02X", reg, val);
                    }
                }
            }
            break;
        }

        // --- 0xB2..0xBF: Various 2-byte operand chip writes ---
        case 0xB2: case 0xB3: case 0xB4: case 0xB5:
        case 0xB6: case 0xB7: case 0xB8: case 0xB9: case 0xBA: case 0xBB:
        case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
            inst.length = 3;
            if (filePos + 3 <= dataSize) {
                uint8_t reg = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                const char* chipName = "Chip";
                if (cmd == 0xB3) { chipName = "GB DMG"; inst.category = InstCategory::PSG_Square; }
                else if (cmd == 0xB4) { chipName = "NES APU"; inst.category = InstCategory::PSG_Square; }
                else if (cmd == 0xB5) { chipName = "MultiPCM"; inst.category = InstCategory::Other; }
                else if (cmd == 0xB8) { chipName = "OKIM6295"; inst.category = InstCategory::Other; }
                else if (cmd == 0xB9) { chipName = "HuC6280"; inst.category = InstCategory::PSG_Square; }
                else if (cmd == 0xBA) { chipName = "K053260"; inst.category = InstCategory::Other; }
                else if (cmd == 0xBD) { chipName = "SAA1099"; inst.category = InstCategory::PSG_Square; }
                else { chipName = "Sound Chip"; inst.category = InstCategory::SoundChips; }

                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "%02X %02X %02X", cmd, reg, val);
                std::snprintf(inst.chip, sizeof(inst.chip), "%s", chipName);
                std::snprintf(inst.desc, sizeof(inst.desc), "Reg 0x%02X = 0x%02X", reg, val);
                uniqueChips.insert(chipName);
            }
            break;
        }

        // --- 0xC0..0xDF: Various 3-byte operand chip writes ---
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4:
        case 0xC9: case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xD4: {
            inst.length = 4;
            if (filePos + 4 <= dataSize) {
                uint8_t p1 = vgmData[filePos + 1];
                uint8_t p2 = vgmData[filePos + 2];
                uint8_t p3 = vgmData[filePos + 3];
                const char* chipName = "Chip";
                if (cmd == 0xC0) chipName = "SegaPCM";
                else if (cmd == 0xC1) chipName = "RF5C68";
                else if (cmd == 0xC2) chipName = "RF5C164";
                else if (cmd == 0xC4) chipName = "QSound";
                else if (cmd == 0xD0) chipName = "YMF278B";
                else if (cmd == 0xD1) chipName = "YMF271";

                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "%02X %02X %02X %02X", cmd, p1, p2, p3);
                std::snprintf(inst.chip, sizeof(inst.chip), "%s", chipName);
                if (cmd == 0xC1 || cmd == 0xC2) {
                    uint16_t mOfs = (uint16_t)p1 | ((uint16_t)p2 << 8);
                    std::snprintf(inst.desc, sizeof(inst.desc), "RAM [0x%04X] = 0x%02X", mOfs, p3);
                    std::snprintf(inst.channel, sizeof(inst.channel), "RAM");
                } else {
                    std::snprintf(inst.desc, sizeof(inst.desc), "Data 0x%02X 0x%02X 0x%02X", p1, p2, p3);
                }
                inst.category = InstCategory::SoundChips;
                uniqueChips.insert(chipName);
            }
            break;
        }

        // --- 0xE0: YM2612 PCM Seek ---
        case 0xE0: {
            inst.length = 5;
            if (filePos + 5 <= dataSize) {
                uint32_t seekOfs = ReadLE32(&vgmData[filePos + 1]);
                std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "E0 %02X %02X ..", vgmData[filePos + 1], vgmData[filePos + 2]);
                std::snprintf(inst.chip, sizeof(inst.chip), "YM2612 PCM");
                std::snprintf(inst.desc, sizeof(inst.desc), "PCM Data Offset = 0x%08X", seekOfs);
                inst.category = InstCategory::FM;
                uniqueChips.insert("YM2612 PCM");
            }
            break;
        }

        default: {
            // Generic command fallback
            inst.length = 1;
            std::snprintf(inst.hexBytes, sizeof(inst.hexBytes), "%02X", cmd);
            std::snprintf(inst.chip, sizeof(inst.chip), "0x%02X", cmd);
            std::snprintf(inst.desc, sizeof(inst.desc), "Opcode 0x%02X", cmd);
            inst.category = InstCategory::Other;
            break;
        }
        }

        if (inst.length == 0) inst.length = 1;

        // Auto-assign inst.channel if not explicitly set
        if (inst.channel[0] == '\0') {
            if (inst.isWait) {
                std::snprintf(inst.channel, sizeof(inst.channel), "Timing");
            } else if (std::strncmp(inst.desc, "Ch ", 3) == 0) {
                int chNum = 0;
                if (std::sscanf(inst.desc, "Ch %d", &chNum) == 1) {
                    std::snprintf(inst.channel, sizeof(inst.channel), "Ch %d", chNum);
                } else {
                    std::snprintf(inst.channel, sizeof(inst.channel), "Ch");
                }
            } else if (std::strncmp(inst.desc, "Pulse 1", 7) == 0 || std::strstr(inst.desc, "Pulse 1")) {
                std::snprintf(inst.channel, sizeof(inst.channel), "Pulse 1");
            } else if (std::strncmp(inst.desc, "Pulse 2", 7) == 0 || std::strstr(inst.desc, "Pulse 2")) {
                std::snprintf(inst.channel, sizeof(inst.channel), "Pulse 2");
            } else if (std::strstr(inst.desc, "Triangle")) {
                std::snprintf(inst.channel, sizeof(inst.channel), "Triangle");
            } else if (std::strstr(inst.desc, "Noise")) {
                std::snprintf(inst.channel, sizeof(inst.channel), "Noise");
            } else if (std::strstr(inst.desc, "DMC")) {
                std::snprintf(inst.channel, sizeof(inst.channel), "DMC");
            } else if (std::strstr(inst.desc, "DAC")) {
                std::snprintf(inst.channel, sizeof(inst.channel), "DAC");
            } else if (std::strstr(inst.desc, "PCM")) {
                std::snprintf(inst.channel, sizeof(inst.channel), "PCM");
            } else if (inst.category == InstCategory::DataBlock) {
                std::snprintf(inst.channel, sizeof(inst.channel), "Data Block");
            } else {
                std::snprintf(inst.channel, sizeof(inst.channel), "Global");
            }
        }

        if (inst.channel[0] != '\0' && std::strcmp(inst.channel, "Timing") != 0) {
            uniqueChannels.insert(inst.channel);
        }

        m_instructions.push_back(inst);
        filePos += inst.length;
    }

    for (const auto& ch : uniqueChips) {
        m_detectedChips.push_back(ch);
    }
    for (const auto& chn : uniqueChannels) {
        m_detectedChannels.push_back(chn);
    }

    return !m_instructions.empty();
}

} // namespace vgmstudio
