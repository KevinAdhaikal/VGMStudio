#include "vgm_patch_extractor.hpp"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>

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

static std::string FormatTimestamp(uint32_t samples) {
    double secTotal = (double)samples / 44100.0;
    int min = (int)(secTotal / 60.0);
    int sec = (int)secTotal % 60;
    int cs = (int)((secTotal - (double)(min * 60 + sec)) * 100.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d.%02d", min, sec, cs);
    return std::string(buf);
}

VgmPatchExtractor::VgmPatchExtractor() = default;
VgmPatchExtractor::~VgmPatchExtractor() = default;

void VgmPatchExtractor::Clear() {
    m_presets.clear();
    m_allChannels.clear();
}

void VgmPatchExtractor::RecordChannel(const std::string& ch) {
    if (ch.empty()) return;
    if (std::find(m_allChannels.begin(), m_allChannels.end(), ch) == m_allChannels.end()) {
        m_allChannels.push_back(ch);
    }
}

bool VgmPatchExtractor::IsFmCarrier(int algorithm, int opIdx) {
    // 4-operator algorithms: opIdx 0=OP1, 1=OP2, 2=OP3, 3=OP4
    switch (algorithm) {
        case 0:
        case 1:
        case 2:
        case 3:
            return (opIdx == 3);
        case 4:
            return (opIdx == 1 || opIdx == 3);
        case 5:
        case 6:
            return (opIdx >= 1);
        case 7:
        default:
            return true;
    }
}

bool VgmPatchExtractor::CompareFm(const FmPresetData& a, const FmPresetData& b) {
    if (a.algorithm != b.algorithm || a.feedback != b.feedback) return false;
    for (int k = 0; k < 4; ++k) {
        if (a.ops[k].mult != b.ops[k].mult) return false;
        if (a.ops[k].ar   != b.ops[k].ar)   return false;
        if (a.ops[k].dr   != b.ops[k].dr)   return false;
        if (a.ops[k].sr   != b.ops[k].sr)   return false;
        if (a.ops[k].sl   != b.ops[k].sl)   return false;
        if (a.ops[k].rr   != b.ops[k].rr)   return false;
        if (a.ops[k].dt   != b.ops[k].dt)   return false;
        // Non-carrier operators modulate timbre: their TL must match exactly.
        // Carrier operator TL defines note velocity/volume; matching timbre allows grouping note velocity variations.
        if (!IsFmCarrier(a.algorithm, k)) {
            if (a.ops[k].tl != b.ops[k].tl) return false;
        }
    }
    return true;
}

bool VgmPatchExtractor::CompareSquare(const SquarePresetData& a, const SquarePresetData& b) {
    return (a.dutyIndex == b.dutyIndex && 
            a.constantVol == b.constantVol &&
            a.volume == b.volume &&
            a.sweepEnabled == b.sweepEnabled &&
            a.sweepShift == b.sweepShift &&
            a.sweepPeriod == b.sweepPeriod &&
            a.sweepNegate == b.sweepNegate);
}

bool VgmPatchExtractor::CompareTriangle(const TrianglePresetData& a, const TrianglePresetData& b) {
    return (a.linearCounter == b.linearCounter && a.lengthHalt == b.lengthHalt);
}

bool VgmPatchExtractor::CompareNoise(const NoisePresetData& a, const NoisePresetData& b) {
    return (a.mode == b.mode && a.volume == b.volume && a.period == b.period);
}

bool VgmPatchExtractor::CompareWavetable(const WavetablePresetData& a, const WavetablePresetData& b) {
    if (a.samples.size() != b.samples.size()) return false;
    for (size_t i = 0; i < a.samples.size(); ++i) {
        if (std::fabs(a.samples[i] - b.samples[i]) > 0.05f) return false;
    }
    return true;
}

bool VgmPatchExtractor::ExtractFromVgm(const uint8_t* vgmData, size_t vgmSize) {
    Clear();
    if (!vgmData || vgmSize < 0x40) return false;
    if (std::memcmp(vgmData, "Vgm ", 4) != 0) return false;

    uint32_t fileVer = ReadLE32(&vgmData[0x08]);
    uint32_t dataOfs = (fileVer >= 0x150) ? ReadRelOfs(vgmData, 0x34) : 0x40;
    if (!dataOfs) dataOfs = 0x40;
    if (dataOfs >= vgmSize) return false;

    uint32_t eofOfs = ReadRelOfs(vgmData, 0x04);
    if (!eofOfs || eofOfs > vgmSize) eofOfs = (uint32_t)vgmSize;

    // Chip state registers
    uint8_t ym2612Regs[2][256] = {{0}};
    uint8_t ym2151Regs[256] = {0};
    uint8_t ym2203Regs[256] = {0};
    uint8_t nesRegs[32] = {0};
    uint8_t dmgRegs[64] = {0};
    uint8_t dmgWaveRam[16] = {0};
    uint8_t pceRegs[16] = {0};
    uint8_t pceWave[6][32] = {{0}};
    int pceCurCh = 0;
    int pceWaveIdx[6] = {0};
    int ym2612Count = 0;
    int ym2151Count = 0;
    int ym2203Count = 0;
    int nesCount = 0;
    int pceCount = 0;
    int dmgCount = 0;
    std::map<std::string, int> channelPatchCount;

    const int dtTable[8] = { 0, 1, 2, 3, 0, -1, -2, -3 };
    const int opOfs2612[4] = { 0x00, 0x08, 0x04, 0x0C }; // OP1, OP3, OP2, OP4
    const int opOfs2151[4] = { 0x00, 0x08, 0x10, 0x18 }; // M1, C1, M2, C2

    size_t pos = dataOfs;
    uint32_t curSample = 0;

    while (pos < eofOfs && pos < vgmSize) {
        uint8_t cmd = vgmData[pos++];

        if (cmd == 0x66) { // End of sound data
            break;
        }

        // Delays
        if (cmd == 0x61) {
            if (pos + 2 <= vgmSize) {
                curSample += ReadLE16(&vgmData[pos]);
                pos += 2;
            }
        } else if (cmd == 0x62) {
            curSample += 735;
        } else if (cmd == 0x63) {
            curSample += 882;
        } else if ((cmd & 0xF0) == 0x70) {
            curSample += (cmd & 0x0F) + 1;
        } else if ((cmd & 0xF0) == 0x80) {
            curSample += (cmd & 0x0F);
        }

        // Data block
        else if (cmd == 0x67) {
            if (pos + 6 <= vgmSize) {
                pos += 2; // 0x66 + type
                uint32_t bSize = ReadLE32(&vgmData[pos]);
                pos += 4 + bSize;
            }
        }

        // YM2612 Port 0 & Port 1
        else if (cmd == 0x52 || cmd == 0x53) {
            if (pos + 2 <= vgmSize) {
                int bank = (cmd == 0x53) ? 1 : 0;
                uint8_t reg = vgmData[pos++];
                uint8_t val = vgmData[pos++];
                ym2612Regs[bank][reg] = val;

                // Check Key-On event (Port 0, Reg 0x28)
                if (bank == 0 && reg == 0x28) {
                    int rawCh = val & 0x07;
                    int slots = (val >> 4) & 0x0F;
                    if (rawCh != 3 && rawCh <= 6 && slots > 0) {
                        int ch = (rawCh < 3) ? rawCh : (rawCh - 1);
                        int chBank = ch / 3;
                        int chInBank = ch % 3;
                        bool dacOn = (ym2612Regs[0][0x2B] & 0x80) != 0;

                        if (!(ch == 5 && dacOn)) {
                            FmPresetData fd;
                            uint8_t rB0 = ym2612Regs[chBank][0xB0 + chInBank];
                            uint8_t rB4 = ym2612Regs[chBank][0xB4 + chInBank];
                            fd.algorithm = rB0 & 0x07;
                            fd.feedback = (rB0 >> 3) & 0x07;
                            fd.ams = (rB4 >> 4) & 0x03;
                            fd.pms = rB4 & 0x07;

                            std::map<std::string, uint8_t> rawRegs;
                            char rBuf[48];
                            std::snprintf(rBuf, sizeof(rBuf), "P%d 0xB%d (FB/Alg)", chBank, chInBank); rawRegs[rBuf] = rB0;
                            std::snprintf(rBuf, sizeof(rBuf), "P%d 0xB%d (Pan/AMS/PMS)", chBank, 4 + chInBank); rawRegs[rBuf] = rB4;

                            bool hasActiveOp = false;
                            for (int k = 0; k < 4; ++k) {
                                int r = opOfs2612[k] + chInBank;
                                fd.ops[k].mult = ym2612Regs[chBank][0x30 + r] & 0x0F;
                                fd.ops[k].dt   = dtTable[(ym2612Regs[chBank][0x30 + r] >> 4) & 0x07];
                                fd.ops[k].tl   = ym2612Regs[chBank][0x40 + r] & 0x7F;
                                fd.ops[k].rs   = (ym2612Regs[chBank][0x50 + r] >> 6) & 0x03;
                                fd.ops[k].ar   = ym2612Regs[chBank][0x50 + r] & 0x1F;
                                fd.ops[k].am   = (ym2612Regs[chBank][0x60 + r] >> 7) & 0x01;
                                fd.ops[k].dr   = ym2612Regs[chBank][0x60 + r] & 0x1F;
                                fd.ops[k].sr   = ym2612Regs[chBank][0x70 + r] & 0x1F;
                                fd.ops[k].sl   = (ym2612Regs[chBank][0x80 + r] >> 4) & 0x0F;
                                fd.ops[k].rr   = ym2612Regs[chBank][0x80 + r] & 0x0F;
                                fd.ops[k].ssg  = ym2612Regs[chBank][0x90 + r] & 0x0F;

                                std::string opName = "OP" + std::to_string(k + 1);
                                std::snprintf(rBuf, sizeof(rBuf), "P%d 0x%02X (%s DT/MULT)", chBank, 0x30 + r, opName.c_str()); rawRegs[rBuf] = ym2612Regs[chBank][0x30 + r];
                                std::snprintf(rBuf, sizeof(rBuf), "P%d 0x%02X (%s TL)", chBank, 0x40 + r, opName.c_str());      rawRegs[rBuf] = ym2612Regs[chBank][0x40 + r];
                                std::snprintf(rBuf, sizeof(rBuf), "P%d 0x%02X (%s RS/AR)", chBank, 0x50 + r, opName.c_str());   rawRegs[rBuf] = ym2612Regs[chBank][0x50 + r];
                                std::snprintf(rBuf, sizeof(rBuf), "P%d 0x%02X (%s AM/D1R)", chBank, 0x60 + r, opName.c_str());  rawRegs[rBuf] = ym2612Regs[chBank][0x60 + r];
                                std::snprintf(rBuf, sizeof(rBuf), "P%d 0x%02X (%s D2R)", chBank, 0x70 + r, opName.c_str());     rawRegs[rBuf] = ym2612Regs[chBank][0x70 + r];
                                std::snprintf(rBuf, sizeof(rBuf), "P%d 0x%02X (%s D1L/RR)", chBank, 0x80 + r, opName.c_str());  rawRegs[rBuf] = ym2612Regs[chBank][0x80 + r];
                                std::snprintf(rBuf, sizeof(rBuf), "P%d 0x%02X (%s SSG-EG)", chBank, 0x90 + r, opName.c_str());  rawRegs[rBuf] = ym2612Regs[chBank][0x90 + r];

                                if (fd.ops[k].ar > 0) hasActiveOp = true;
                            }

                            if (hasActiveOp) {
                                std::string chName = "FM " + std::to_string(ch + 1);
                                RecordChannel(chName);
                                bool found = false;
                                for (auto& p : m_presets) {
                                    if (p.chipName == "YM2612" && p.channelName == chName && CompareFm(p.fm, fd)) {
                                        p.useCount++;
                                        p.AddChannelUse(chName);
                                        // Keep master (loudest carrier TL)
                                        for (int k = 0; k < 4; ++k) {
                                            if (IsFmCarrier(p.fm.algorithm, k) && fd.ops[k].tl < p.fm.ops[k].tl) {
                                                p.fm.ops[k].tl = fd.ops[k].tl;
                                            }
                                        }
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    int pIdx = ++channelPatchCount[chName];
                                    VgmPreset p;
                                    p.id = (int)m_presets.size() + 1;
                                    p.chipName = "YM2612";
                                    p.channelName = chName;
                                    p.channelIdx = ch;
                                    p.AddChannelUse(chName);
                                    p.type = PresetType::FM_4OP;
                                    p.firstSamplePos = curSample;
                                    p.timestampStr = FormatTimestamp(curSample);
                                    p.useCount = 1;
                                    p.fm = fd;
                                    p.rawRegisters = rawRegs;
                                    char nameBuf[64];
                                    std::snprintf(nameBuf, sizeof(nameBuf), "%s - Patch %d [Alg %d, FB %d]",
                                                  chName.c_str(), pIdx, fd.algorithm, fd.feedback);
                                    p.name = nameBuf;
                                    m_presets.push_back(p);
                                }
                            }
                        }
                    }
                }
            }
        }

        // YM2151 (OPM)
        else if (cmd == 0x54) {
            if (pos + 2 <= vgmSize) {
                uint8_t reg = vgmData[pos++];
                uint8_t val = vgmData[pos++];
                ym2151Regs[reg] = val;

                // Reg 0x08: Key On/Off
                if (reg == 0x08) {
                    int ch = val & 0x07;
                    int slots = (val >> 3) & 0x0F;
                    if (slots > 0) {
                        FmPresetData fd;
                        uint8_t r20 = ym2151Regs[0x20 + ch];
                        uint8_t r38 = ym2151Regs[0x38 + ch];
                        fd.algorithm = r20 & 0x07;
                        fd.feedback = (r20 >> 3) & 0x07;
                        fd.ams = r38 & 0x03;
                        fd.pms = (r38 >> 4) & 0x07;

                        std::map<std::string, uint8_t> rawRegs;
                        char rBuf[48];
                        std::snprintf(rBuf, sizeof(rBuf), "0x20+%d (RL/FB/Alg)", ch); rawRegs[rBuf] = r20;
                        std::snprintf(rBuf, sizeof(rBuf), "0x38+%d (PMS/AMS)", ch); rawRegs[rBuf] = r38;

                        bool hasActiveOp = false;
                        for (int k = 0; k < 4; ++k) {
                            int r = opOfs2151[k] + ch;
                            fd.ops[k].mult = ym2151Regs[0x40 + r] & 0x0F;
                            fd.ops[k].dt   = dtTable[(ym2151Regs[0x40 + r] >> 4) & 0x07];
                            fd.ops[k].tl   = ym2151Regs[0x60 + r] & 0x7F;
                            fd.ops[k].rs   = (ym2151Regs[0x80 + r] >> 6) & 0x03;
                            fd.ops[k].ar   = ym2151Regs[0x80 + r] & 0x1F;
                            fd.ops[k].am   = (ym2151Regs[0xA0 + r] >> 7) & 0x01;
                            fd.ops[k].dr   = ym2151Regs[0xA0 + r] & 0x1F;
                            fd.ops[k].sr   = ym2151Regs[0xC0 + r] & 0x1F;
                            fd.ops[k].sl   = (ym2151Regs[0xE0 + r] >> 4) & 0x0F;
                            fd.ops[k].rr   = ym2151Regs[0xE0 + r] & 0x0F;

                            std::string opName = "OP" + std::to_string(k + 1);
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s DT1/MULT)", 0x40 + r, opName.c_str()); rawRegs[rBuf] = ym2151Regs[0x40 + r];
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s TL)", 0x60 + r, opName.c_str());       rawRegs[rBuf] = ym2151Regs[0x60 + r];
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s KS/AR)", 0x80 + r, opName.c_str());    rawRegs[rBuf] = ym2151Regs[0x80 + r];
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s AMS/D1R)", 0xA0 + r, opName.c_str());  rawRegs[rBuf] = ym2151Regs[0xA0 + r];
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s DT2/D2R)", 0xC0 + r, opName.c_str());  rawRegs[rBuf] = ym2151Regs[0xC0 + r];
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s D1L/RR)", 0xE0 + r, opName.c_str());   rawRegs[rBuf] = ym2151Regs[0xE0 + r];

                            if (fd.ops[k].ar > 0) hasActiveOp = true;
                        }

                        if (hasActiveOp) {
                            std::string chName = "Ch " + std::to_string(ch + 1);
                            RecordChannel(chName);
                            bool found = false;
                            for (auto& p : m_presets) {
                                if (p.chipName == "YM2151" && p.channelName == chName && CompareFm(p.fm, fd)) {
                                    p.useCount++;
                                    p.AddChannelUse(chName);
                                    for (int k = 0; k < 4; ++k) {
                                        if (IsFmCarrier(p.fm.algorithm, k) && fd.ops[k].tl < p.fm.ops[k].tl) {
                                            p.fm.ops[k].tl = fd.ops[k].tl;
                                        }
                                    }
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                int pIdx = ++channelPatchCount[chName];
                                VgmPreset p;
                                p.id = (int)m_presets.size() + 1;
                                p.chipName = "YM2151";
                                p.channelName = chName;
                                p.channelIdx = ch;
                                p.AddChannelUse(chName);
                                p.type = PresetType::FM_4OP;
                                p.firstSamplePos = curSample;
                                p.timestampStr = FormatTimestamp(curSample);
                                p.useCount = 1;
                                p.fm = fd;
                                p.rawRegisters = rawRegs;
                                char nameBuf[64];
                                std::snprintf(nameBuf, sizeof(nameBuf), "%s - Patch %d [Alg %d, FB %d]",
                                              chName.c_str(), pIdx, fd.algorithm, fd.feedback);
                                p.name = nameBuf;
                                m_presets.push_back(p);
                            }
                        }
                    }
                }
            }
        }

        // YM2203 (OPN)
        else if (cmd == 0x55) {
            if (pos + 2 <= vgmSize) {
                uint8_t reg = vgmData[pos++];
                uint8_t val = vgmData[pos++];
                ym2203Regs[reg] = val;

                if (reg == 0x28) {
                    int ch = val & 0x03;
                    int slots = (val >> 4) & 0x0F;
                    if (ch < 3 && slots > 0) {
                        FmPresetData fd;
                        uint8_t rB0 = ym2203Regs[0xB0 + ch];
                        uint8_t rB4 = ym2203Regs[0xB4 + ch];
                        fd.algorithm = rB0 & 0x07;
                        fd.feedback = (rB0 >> 3) & 0x07;
                        fd.ams = (rB4 >> 4) & 0x03;
                        fd.pms = rB4 & 0x07;

                        std::map<std::string, uint8_t> rawRegs;
                        char rBuf[48];
                        std::snprintf(rBuf, sizeof(rBuf), "0xB0+%d (FB/Alg)", ch); rawRegs[rBuf] = rB0;
                        std::snprintf(rBuf, sizeof(rBuf), "0xB4+%d (AMS/PMS)", ch); rawRegs[rBuf] = rB4;

                        bool hasActiveOp = false;
                        for (int k = 0; k < 4; ++k) {
                            int r = opOfs2612[k] + ch;
                            fd.ops[k].mult = ym2203Regs[0x30 + r] & 0x0F;
                            fd.ops[k].dt   = dtTable[(ym2203Regs[0x30 + r] >> 4) & 0x07];
                            fd.ops[k].tl   = ym2203Regs[0x40 + r] & 0x7F;
                            fd.ops[k].rs   = (ym2203Regs[0x50 + r] >> 6) & 0x03;
                            fd.ops[k].ar   = ym2203Regs[0x50 + r] & 0x1F;
                            fd.ops[k].am   = (ym2203Regs[0x60 + r] >> 7) & 0x01;
                            fd.ops[k].dr   = ym2203Regs[0x60 + r] & 0x1F;
                            fd.ops[k].sr   = ym2203Regs[0x70 + r] & 0x1F;
                            fd.ops[k].sl   = (ym2203Regs[0x80 + r] >> 4) & 0x0F;
                            fd.ops[k].rr   = ym2203Regs[0x80 + r] & 0x0F;

                            std::string opName = "OP" + std::to_string(k + 1);
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s DT/MULT)", 0x30 + r, opName.c_str()); rawRegs[rBuf] = ym2203Regs[0x30 + r];
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s TL)", 0x40 + r, opName.c_str());      rawRegs[rBuf] = ym2203Regs[0x40 + r];
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s RS/AR)", 0x50 + r, opName.c_str());   rawRegs[rBuf] = ym2203Regs[0x50 + r];
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s AM/D1R)", 0x60 + r, opName.c_str());  rawRegs[rBuf] = ym2203Regs[0x60 + r];
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s D2R)", 0x70 + r, opName.c_str());     rawRegs[rBuf] = ym2203Regs[0x70 + r];
                            std::snprintf(rBuf, sizeof(rBuf), "0x%02X (%s D1L/RR)", 0x80 + r, opName.c_str());  rawRegs[rBuf] = ym2203Regs[0x80 + r];

                            if (fd.ops[k].ar > 0) hasActiveOp = true;
                        }

                        if (hasActiveOp) {
                            std::string chName = "FM " + std::to_string(ch + 1);
                            RecordChannel(chName);
                            bool found = false;
                            for (auto& p : m_presets) {
                                if (p.chipName == "YM2203" && p.channelName == chName && CompareFm(p.fm, fd)) {
                                    p.useCount++;
                                    p.AddChannelUse(chName);
                                    for (int k = 0; k < 4; ++k) {
                                        if (IsFmCarrier(p.fm.algorithm, k) && fd.ops[k].tl < p.fm.ops[k].tl) {
                                            p.fm.ops[k].tl = fd.ops[k].tl;
                                        }
                                    }
                                    found = true;
                                    break;
                                }
                            }
                            if (!found) {
                                int pIdx = ++channelPatchCount[chName];
                                VgmPreset p;
                                p.id = (int)m_presets.size() + 1;
                                p.chipName = "YM2203";
                                p.channelName = chName;
                                p.channelIdx = ch;
                                p.AddChannelUse(chName);
                                p.type = PresetType::FM_4OP;
                                p.firstSamplePos = curSample;
                                p.timestampStr = FormatTimestamp(curSample);
                                p.useCount = 1;
                                p.fm = fd;
                                p.rawRegisters = rawRegs;
                                char nameBuf[64];
                                std::snprintf(nameBuf, sizeof(nameBuf), "%s - Patch %d [Alg %d, FB %d]",
                                              chName.c_str(), pIdx, fd.algorithm, fd.feedback);
                                p.name = nameBuf;
                                m_presets.push_back(p);
                            }
                        }
                    }
                }
            }
        }

        // NES APU (0xB4 aa dd)
        else if (cmd == 0xB4) {
            if (pos + 2 <= vgmSize) {
                uint8_t reg = vgmData[pos++];
                uint8_t val = vgmData[pos++];
                nesRegs[reg & 0x1F] = val;

                // Square 1 Trigger ($4003)
                if (reg == 0x03) {
                    SquarePresetData sp;
                    int duty = (nesRegs[0x00] >> 6) & 3;
                    const float dTable[4] = { 0.125f, 0.25f, 0.50f, 0.75f };
                    sp.dutyIndex = duty;
                    sp.dutyCycle = dTable[duty];
                    sp.envLoop = (nesRegs[0x00] & 0x20) != 0;
                    sp.constantVol = (nesRegs[0x00] & 0x10) != 0;
                    sp.volume = nesRegs[0x00] & 0x0F;
                    sp.sweepEnabled = (nesRegs[0x01] & 0x80) != 0;
                    sp.sweepPeriod = (nesRegs[0x01] >> 4) & 0x07;
                    sp.sweepNegate = (nesRegs[0x01] & 0x08) != 0;
                    sp.sweepShift = nesRegs[0x01] & 0x07;

                    std::string chName = "Square 1";
                    RecordChannel(chName);
                    bool found = false;
                    for (auto& p : m_presets) {
                        if (p.chipName == "NES APU" && p.channelName == chName && p.type == PresetType::SQUARE && CompareSquare(p.square, sp)) {
                            p.useCount++;
                            p.AddChannelUse(chName);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        int pIdx = ++channelPatchCount[chName];
                        VgmPreset p;
                        p.id = (int)m_presets.size() + 1;
                        p.chipName = "NES APU";
                        p.channelName = chName;
                        p.channelIdx = 0;
                        p.AddChannelUse(chName);
                        p.type = PresetType::SQUARE;
                        p.firstSamplePos = curSample;
                        p.timestampStr = FormatTimestamp(curSample);
                        p.useCount = 1;
                        p.square = sp;
                        p.rawRegisters["$4000 (Duty/Env/Vol)"] = nesRegs[0x00];
                        p.rawRegisters["$4001 (Sweep)"]        = nesRegs[0x01];
                        p.rawRegisters["$4002 (Timer Low)"]    = nesRegs[0x02];
                        p.rawRegisters["$4003 (Length/Hi)"]    = nesRegs[0x03];
                        const char* dNames[4] = { "12.5% Pulse", "25% Pulse", "50% Pulse", "75% Pulse" };
                        p.name = chName + " - Patch " + std::to_string(pIdx) + " [" + dNames[duty] + ", Vol " + std::to_string(sp.volume) + "]";
                        m_presets.push_back(p);
                    }
                }
                // Square 2 Trigger ($4007)
                else if (reg == 0x07) {
                    SquarePresetData sp;
                    int duty = (nesRegs[0x04] >> 6) & 3;
                    const float dTable[4] = { 0.125f, 0.25f, 0.50f, 0.75f };
                    sp.dutyIndex = duty;
                    sp.dutyCycle = dTable[duty];
                    sp.envLoop = (nesRegs[0x04] & 0x20) != 0;
                    sp.constantVol = (nesRegs[0x04] & 0x10) != 0;
                    sp.volume = nesRegs[0x04] & 0x0F;
                    sp.sweepEnabled = (nesRegs[0x05] & 0x80) != 0;
                    sp.sweepPeriod = (nesRegs[0x05] >> 4) & 0x07;
                    sp.sweepNegate = (nesRegs[0x05] & 0x08) != 0;
                    sp.sweepShift = nesRegs[0x05] & 0x07;

                    std::string chName = "Square 2";
                    RecordChannel(chName);
                    bool found = false;
                    for (auto& p : m_presets) {
                        if (p.chipName == "NES APU" && p.channelName == chName && p.type == PresetType::SQUARE && CompareSquare(p.square, sp)) {
                            p.useCount++;
                            p.AddChannelUse(chName);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        int pIdx = ++channelPatchCount[chName];
                        VgmPreset p;
                        p.id = (int)m_presets.size() + 1;
                        p.chipName = "NES APU";
                        p.channelName = chName;
                        p.channelIdx = 1;
                        p.AddChannelUse(chName);
                        p.type = PresetType::SQUARE;
                        p.firstSamplePos = curSample;
                        p.timestampStr = FormatTimestamp(curSample);
                        p.useCount = 1;
                        p.square = sp;
                        p.rawRegisters["$4004 (Duty/Env/Vol)"] = nesRegs[0x04];
                        p.rawRegisters["$4005 (Sweep)"]        = nesRegs[0x05];
                        p.rawRegisters["$4006 (Timer Low)"]    = nesRegs[0x06];
                        p.rawRegisters["$4007 (Length/Hi)"]    = nesRegs[0x07];
                        const char* dNames[4] = { "12.5% Pulse", "25% Pulse", "50% Pulse", "75% Pulse" };
                        p.name = chName + " - Patch " + std::to_string(pIdx) + " [" + dNames[duty] + ", Vol " + std::to_string(sp.volume) + "]";
                        m_presets.push_back(p);
                    }
                }
                // Triangle Trigger ($400B)
                else if (reg == 0x0B) {
                    TrianglePresetData tp;
                    tp.lengthHalt = (nesRegs[0x08] & 0x80) != 0;
                    tp.linearCounter = nesRegs[0x08] & 0x7F;

                    std::string chName = "Triangle";
                    RecordChannel(chName);
                    bool found = false;
                    for (auto& p : m_presets) {
                        if (p.chipName == "NES APU" && p.channelName == chName && p.type == PresetType::TRIANGLE && CompareTriangle(p.triangle, tp)) {
                            p.useCount++;
                            p.AddChannelUse(chName);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        int pIdx = ++channelPatchCount[chName];
                        VgmPreset p;
                        p.id = (int)m_presets.size() + 1;
                        p.chipName = "NES APU";
                        p.channelName = chName;
                        p.channelIdx = 2;
                        p.AddChannelUse(chName);
                        p.type = PresetType::TRIANGLE;
                        p.firstSamplePos = curSample;
                        p.timestampStr = FormatTimestamp(curSample);
                        p.useCount = 1;
                        p.triangle = tp;
                        p.rawRegisters["$4008 (Linear/Halt)"] = nesRegs[0x08];
                        p.rawRegisters["$400A (Timer Low)"]   = nesRegs[0x0A];
                        p.rawRegisters["$400B (Length/Hi)"]   = nesRegs[0x0B];
                        p.name = chName + " - Patch " + std::to_string(pIdx) + " [Linear " + std::to_string(tp.linearCounter) + "]";
                        m_presets.push_back(p);
                    }
                }
                // Noise Trigger ($400F)
                else if (reg == 0x0F) {
                    NoisePresetData np;
                    np.mode = (nesRegs[0x0E] & 0x80) ? 1 : 0;
                    np.constantVol = (nesRegs[0x0C] & 0x10) != 0;
                    np.volume = nesRegs[0x0C] & 0x0F;
                    np.period = nesRegs[0x0E] & 0x0F;

                    std::string chName = "Noise";
                    RecordChannel(chName);
                    bool found = false;
                    for (auto& p : m_presets) {
                        if (p.chipName == "NES APU" && p.channelName == chName && p.type == PresetType::NOISE && CompareNoise(p.noise, np)) {
                            p.useCount++;
                            p.AddChannelUse(chName);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        int pIdx = ++channelPatchCount[chName];
                        VgmPreset p;
                        p.id = (int)m_presets.size() + 1;
                        p.chipName = "NES APU";
                        p.channelName = chName;
                        p.channelIdx = 3;
                        p.AddChannelUse(chName);
                        p.type = PresetType::NOISE;
                        p.firstSamplePos = curSample;
                        p.timestampStr = FormatTimestamp(curSample);
                        p.useCount = 1;
                        p.noise = np;
                        p.rawRegisters["$400C (Vol/Env)"]     = nesRegs[0x0C];
                        p.rawRegisters["$400E (Mode/Period)"] = nesRegs[0x0E];
                        p.rawRegisters["$400F (Length/Hi)"]   = nesRegs[0x0F];
                        p.name = chName + " - Patch " + std::to_string(pIdx) + " [" + std::string(np.mode ? "Metallic" : "White LFSR") + ", Vol " + std::to_string(np.volume) + "]";
                        m_presets.push_back(p);
                    }
                }
            }
        }

        // HuC6280 (PCE / TurboGrafx)
        else if (cmd == 0xB9) {
            if (pos + 2 <= vgmSize) {
                uint8_t reg = vgmData[pos++];
                uint8_t val = vgmData[pos++];
                if (reg == 0x00) {
                    pceCurCh = val & 0x07;
                    if (pceCurCh > 5) pceCurCh = 5;
                } else if (reg == 0x06) {
                    int wIdx = pceWaveIdx[pceCurCh] % 32;
                    pceWave[pceCurCh][wIdx] = val & 0x1F;
                    pceWaveIdx[pceCurCh]++;
                } else if (reg == 0x04) {
                    // DDA / Channel Enable
                    if (val & 0x80) {
                        WavetablePresetData wp;
                        wp.bitDepth = 5;
                        wp.volume = val & 0x1F;
                        wp.samples.resize(32);
                        for (int i = 0; i < 32; ++i) {
                            wp.samples[i] = ((float)pceWave[pceCurCh][i] / 15.5f) - 1.0f;
                        }
                        std::string chName = "Ch " + std::to_string(pceCurCh + 1);
                        RecordChannel(chName);
                        bool found = false;
                        for (auto& p : m_presets) {
                            if (p.chipName == "HuC6280" && p.channelName == chName && CompareWavetable(p.wavetable, wp)) {
                                p.useCount++;
                                p.AddChannelUse(chName);
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            int pIdx = ++channelPatchCount[chName];
                            VgmPreset p;
                            p.id = (int)m_presets.size() + 1;
                            p.chipName = "HuC6280";
                            p.channelName = chName;
                            p.channelIdx = pceCurCh;
                            p.AddChannelUse(chName);
                            p.type = PresetType::WAVETABLE;
                            p.firstSamplePos = curSample;
                            p.timestampStr = FormatTimestamp(curSample);
                            p.useCount = 1;
                            p.wavetable = wp;
                            p.rawRegisters["0x04 (DDA/Vol)"]   = val;
                            p.rawRegisters["0x06 (Waveform)"]  = 32;
                            p.name = chName + " - Wavetable " + std::to_string(pIdx) + " [32-Step RAM, Vol " + std::to_string(wp.volume) + "]";
                            m_presets.push_back(p);
                        }
                    }
                }
            }
        }

        // SN76496 PSG (0x50 dd)
        else if (cmd == 0x50) {
            if (pos < vgmSize) {
                uint8_t val = vgmData[pos++];
                if ((val & 0xE0) == 0xE0) { // Noise control
                    NoisePresetData np;
                    np.mode = (val & 0x04) ? 0 : 1; // 0 = White, 1 = Periodic
                    np.period = val & 0x03;
                    np.volume = 15;
                    std::string chName = "Noise";
                    RecordChannel(chName);
                    bool found = false;
                    for (auto& p : m_presets) {
                        if (p.chipName == "SN76496" && p.channelName == chName && p.type == PresetType::NOISE && CompareNoise(p.noise, np)) {
                            p.useCount++;
                            p.AddChannelUse(chName);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        int pIdx = ++channelPatchCount[chName];
                        VgmPreset p;
                        p.id = (int)m_presets.size() + 1;
                        p.chipName = "SN76496";
                        p.channelName = chName;
                        p.channelIdx = 3;
                        p.AddChannelUse(chName);
                        p.type = PresetType::NOISE;
                        p.firstSamplePos = curSample;
                        p.timestampStr = FormatTimestamp(curSample);
                        p.useCount = 1;
                        p.noise = np;
                        p.rawRegisters["0x50 (Noise Ctrl)"] = val;
                        p.name = chName + " - Patch " + std::to_string(pIdx) + " [" + std::string(np.mode ? "Periodic Pulse" : "White Noise") + "]";
                        m_presets.push_back(p);
                    }
                }
            }
        }

        // Generic skip for 1-byte, 2-byte, 3-byte commands
        else if (cmd >= 0x30 && cmd <= 0x4F) {
            pos += 1;
        } else if (cmd >= 0x56 && cmd <= 0x5F) {
            pos += 2;
        } else if (cmd >= 0xA0 && cmd <= 0xBF) {
            pos += 2;
        } else if (cmd >= 0xC0 && cmd <= 0xDF) {
            pos += 3;
        } else if (cmd >= 0xE0) {
            pos += 4;
        }
    }

    std::sort(m_allChannels.begin(), m_allChannels.end());

    return true;
}

bool VgmPatchExtractor::ExportTFI(const VgmPreset& preset, const std::string& path) {
    if (preset.type != PresetType::FM_4OP) return false;
    uint8_t dataTFI[42] = {0};
    unsigned pp = 0;
    dataTFI[pp++] = (uint8_t)preset.fm.algorithm;
    dataTFI[pp++] = (uint8_t)preset.fm.feedback;
    for (int k = 0; k < 4; ++k) {
        dataTFI[pp++] = (uint8_t)preset.fm.ops[k].mult;
        dataTFI[pp++] = (uint8_t)(preset.fm.ops[k].dt + 3);
        dataTFI[pp++] = (uint8_t)preset.fm.ops[k].tl;
        dataTFI[pp++] = (uint8_t)preset.fm.ops[k].rs;
        dataTFI[pp++] = (uint8_t)preset.fm.ops[k].ar;
        dataTFI[pp++] = (uint8_t)preset.fm.ops[k].dr;
        dataTFI[pp++] = (uint8_t)preset.fm.ops[k].sr;
        dataTFI[pp++] = (uint8_t)preset.fm.ops[k].rr;
        dataTFI[pp++] = (uint8_t)preset.fm.ops[k].sl;
        dataTFI[pp++] = (uint8_t)preset.fm.ops[k].ssg;
    }
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(dataTFI), sizeof(dataTFI));
    return true;
}

bool VgmPatchExtractor::ExportVGI(const VgmPreset& preset, const std::string& path) {
    if (preset.type != PresetType::FM_4OP) return false;
    uint8_t dataVGI[43] = {0};
    unsigned pp = 0;
    dataVGI[pp++] = (uint8_t)preset.fm.algorithm;
    dataVGI[pp++] = (uint8_t)preset.fm.feedback;
    dataVGI[pp++] = (uint8_t)(preset.fm.pms | (preset.fm.ams << 4));
    for (int k = 0; k < 4; ++k) {
        dataVGI[pp++] = (uint8_t)preset.fm.ops[k].mult;
        dataVGI[pp++] = (uint8_t)(preset.fm.ops[k].dt + 3);
        dataVGI[pp++] = (uint8_t)preset.fm.ops[k].tl;
        dataVGI[pp++] = (uint8_t)preset.fm.ops[k].rs;
        dataVGI[pp++] = (uint8_t)preset.fm.ops[k].ar;
        dataVGI[pp++] = (uint8_t)(preset.fm.ops[k].dr | (preset.fm.ops[k].am << 7));
        dataVGI[pp++] = (uint8_t)preset.fm.ops[k].sr;
        dataVGI[pp++] = (uint8_t)preset.fm.ops[k].rr;
        dataVGI[pp++] = (uint8_t)preset.fm.ops[k].sl;
        dataVGI[pp++] = (uint8_t)preset.fm.ops[k].ssg;
    }
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;
    ofs.write(reinterpret_cast<const char*>(dataVGI), sizeof(dataVGI));
    return true;
}

std::string VgmPatchExtractor::FormatPresetText(const VgmPreset& preset) {
    std::ostringstream ss;
    ss << "; VGM Preset Dump\n";
    ss << "; Name: " << preset.name << "\n";
    ss << "; Chip: " << preset.chipName << "\n";
    ss << "; First Seen: " << preset.timestampStr << "\n";
    ss << "; Trigger Count: " << preset.useCount << "\n";
    ss << "; Used by Channels: " << preset.GetChannelsDetailString() << "\n\n";

    if (preset.type == PresetType::FM_4OP) {
        ss << "[FM Parameters]\n";
        ss << "Algorithm = " << preset.fm.algorithm << "\n";
        ss << "Feedback  = " << preset.fm.feedback << "\n";
        ss << "PMS       = " << preset.fm.pms << "\n";
        ss << "AMS       = " << preset.fm.ams << "\n\n";

        ss << "; OP  MULT  TL   AR  D1R D2R  DL  RR  DT  RS  AM\n";
        for (int i = 0; i < 4; ++i) {
            char line[128];
            const auto& o = preset.fm.ops[i];
            std::snprintf(line, sizeof(line), "OP%d  %4d  %3d  %3d  %3d %3d  %3d %3d  %2d  %2d  %2d\n",
                          i + 1, o.mult, o.tl, o.ar, o.dr, o.sr, o.sl, o.rr, o.dt, o.rs, o.am ? 1 : 0);
            ss << line;
        }
    } else if (preset.type == PresetType::SQUARE) {
        ss << "[Square / Pulse Parameters]\n";
        ss << "Duty Cycle = " << (preset.square.dutyCycle * 100.0f) << "%\n";
        ss << "Volume     = " << preset.square.volume << "\n";
        ss << "Env Loop   = " << (preset.square.envLoop ? "Yes" : "No") << "\n";
        ss << "Sweep      = " << (preset.square.sweepEnabled ? "Enabled" : "Disabled") << "\n";
        if (preset.square.sweepEnabled) {
            ss << "Sweep Shift  = " << preset.square.sweepShift << "\n";
            ss << "Sweep Period = " << preset.square.sweepPeriod << "\n";
            ss << "Sweep Negate = " << (preset.square.sweepNegate ? "True" : "False") << "\n";
        }
    } else if (preset.type == PresetType::TRIANGLE) {
        ss << "[Triangle Parameters]\n";
        ss << "Linear Counter = " << preset.triangle.linearCounter << "\n";
        ss << "Length Halt    = " << (preset.triangle.lengthHalt ? "True" : "False") << "\n";
    } else if (preset.type == PresetType::NOISE) {
        ss << "[Noise Parameters]\n";
        ss << "Mode   = " << (preset.noise.mode ? "Short LFSR (Metallic)" : "Long LFSR (White Noise)") << "\n";
        ss << "Volume = " << preset.noise.volume << "\n";
        ss << "Period = " << preset.noise.period << "\n";
    } else if (preset.type == PresetType::WAVETABLE) {
        ss << "[Wavetable Data (32 Steps)]\n";
        for (size_t i = 0; i < preset.wavetable.samples.size(); ++i) {
            ss << preset.wavetable.samples[i];
            if (i + 1 < preset.wavetable.samples.size()) ss << ", ";
            if ((i + 1) % 8 == 0) ss << "\n";
        }
    }

    if (!preset.rawRegisters.empty()) {
        ss << "\n[Captured Raw VGM Hardware Registers]\n";
        for (const auto& kv : preset.rawRegisters) {
            char rLine[64];
            std::snprintf(rLine, sizeof(rLine), "%-28s = 0x%02X (%d)\n", kv.first.c_str(), kv.second, kv.second);
            ss << rLine;
        }
    }

    return ss.str();
}

} // namespace vgmstudio
