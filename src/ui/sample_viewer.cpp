#include "sample_viewer.hpp"
#include "icons.hpp"
#include <imgui.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <algorithm>
#include <set>
#include <map>
#include <filesystem>

namespace vgmstudio {

static inline uint16_t ReadLE16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t ReadLE24(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static inline uint32_t ReadLE32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ----------------------------------------------------------------------------
// Decoders for various PCM formats
// ----------------------------------------------------------------------------

static std::vector<float> DecodeOkiAdpcm(const uint8_t* data, size_t length) {
    static const int index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
    static int diff_table[49][16];
    static bool tables_init = false;

    if (!tables_init) {
        static const int nbl2bit[16][4] = {
            { 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
            { 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
            {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
            {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
        };
        for (int step = 0; step <= 48; ++step) {
            int stepval = (int)std::floor(16.0 * std::pow(11.0 / 10.0, (double)step));
            for (int nib = 0; nib < 16; ++nib) {
                diff_table[step][nib] = nbl2bit[nib][0] * (
                    stepval * nbl2bit[nib][1] +
                    (stepval / 2) * nbl2bit[nib][2] +
                    (stepval / 4) * nbl2bit[nib][3] +
                    (stepval / 8)
                );
            }
        }
        tables_init = true;
    }

    std::vector<float> pcm;
    pcm.reserve(length * 2);

    int signal = -2;
    int step = 0;

    auto clockNibble = [&](uint8_t nibble) {
        signal += diff_table[step][nibble & 15];
        if (signal > 2047) signal = 2047;
        else if (signal < -2048) signal = -2048;

        step += index_shift[nibble & 7];
        if (step > 48) step = 48;
        else if (step < 0) step = 0;

        pcm.push_back((float)signal / 2048.0f);
    };

    for (size_t i = 0; i < length; ++i) {
        uint8_t b = data[i];
        clockNibble((b >> 4) & 0x0F);
        clockNibble(b & 0x0F);
    }
    return pcm;
}

static std::vector<float> DecodeRf5cSignMag(const uint8_t* data, size_t length) {
    std::vector<float> pcm;
    pcm.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        uint8_t b = data[i];
        if (b == 0xFF) {
            pcm.push_back(0.0f);
        } else if (b & 0x80) {
            pcm.push_back(-((float)(b & 0x7F) / 128.0f));
        } else {
            pcm.push_back((float)(b & 0x7F) / 128.0f);
        }
    }
    return pcm;
}

static std::vector<float> Decode8BitUnsigned(const uint8_t* data, size_t length) {
    std::vector<float> pcm;
    pcm.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        pcm.push_back(((float)data[i] - 128.0f) / 128.0f);
    }
    return pcm;
}

static std::vector<float> Decode8BitSigned(const uint8_t* data, size_t length) {
    std::vector<float> pcm;
    pcm.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        pcm.push_back((float)((int8_t)data[i]) / 128.0f);
    }
    return pcm;
}

static std::vector<float> DecodeNesDpcm(const uint8_t* data, size_t length) {
    std::vector<float> pcm;
    pcm.reserve(length * 8);
    int curLevel = 0;
    for (size_t i = 0; i < length; ++i) {
        uint8_t b = data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (b & (1 << bit)) {
                if (curLevel <= 125) curLevel += 2;
            } else {
                if (curLevel >= 2) curLevel -= 2;
            }
            pcm.push_back(((float)curLevel - 64.0f) / 64.0f);
        }
    }
    return pcm;
}

static bool DrawMasterStyleFader(const char* label, float* pValue, float vMin, float vMax, float width = 110.0f, float height = 24.0f) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(width, height));
    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();
    bool changed = false;

    float centerY = pos.y + height * 0.5f;

    // Speaker icon area: left 20 pixels
    float iconAreaW = 20.0f;
    ImVec2 iconCenter(pos.x + 10.0f, centerY);

    // Clicking directly on speaker icon toggles mute / unmute
    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::GetIO().MousePos.x < pos.x + iconAreaW) {
        if (*pValue > 0.001f) *pValue = 0.0f;
        else *pValue = 1.0f;
        changed = true;
    }

    // Fader track starts after the speaker icon area
    float trackX0 = pos.x + iconAreaW + 4.0f;
    float trackX1 = pos.x + width - 6.0f;
    float trackW = trackX1 - trackX0;

    if (isActive && ImGui::GetIO().MousePos.x >= pos.x + iconAreaW) {
        float mouseX = ImGui::GetIO().MousePos.x;
        float norm = (mouseX - trackX0) / trackW;
        norm = std::clamp(norm, 0.0f, 1.0f);
        *pValue = vMin + norm * (vMax - vMin);
        changed = true;
    }
    if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *pValue = 1.0f;
        changed = true;
    }
    if (isHovered || isActive) {
        ImGui::SetTooltip("Volume: %.0f%% (Click speaker to mute, Double-click to reset)", *pValue * 100.0f);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Draw speaker icon, perfectly vertically centered on centerY
    bool iconHovered = isHovered && (ImGui::GetIO().MousePos.x < pos.x + iconAreaW);
    ImU32 spkCol = (*pValue <= 0.001f) ? IM_COL32(220, 80, 80, 255)
                 : (iconHovered ? IM_COL32(0, 215, 240, 255) : IM_COL32(160, 185, 215, 255));
    icons::DrawVolume(dl, iconCenter, 14.0f, spkCol, *pValue);

    // Rail track matching Master Mixer
    float railH = 4.0f;
    dl->AddRectFilled(ImVec2(trackX0, centerY - railH * 0.5f), ImVec2(trackX1, centerY + railH * 0.5f), IM_COL32(10, 12, 16, 255), 2.0f);
    dl->AddRect(ImVec2(trackX0, centerY - railH * 0.5f), ImVec2(trackX1, centerY + railH * 0.5f), IM_COL32(32, 40, 54, 255), 2.0f);

    float norm = (*pValue - vMin) / (vMax - vMin);
    norm = std::clamp(norm, 0.0f, 1.0f);
    float capX = trackX0 + norm * trackW;

    // Active track fill
    if (norm > 0.0f) {
        dl->AddRectFilled(ImVec2(trackX0, centerY - railH * 0.5f + 1.0f), ImVec2(capX, centerY + railH * 0.5f - 1.0f), IM_COL32(0, 180, 200, 220), 1.0f);
    }

    // Metallic cap handle matching Master Mixer
    float capW = 10.0f;
    float capH = 16.0f;
    ImVec2 capMin(capX - capW * 0.5f, centerY - capH * 0.5f);
    ImVec2 capMax(capX + capW * 0.5f, centerY + capH * 0.5f);
    ImU32 capBg = isActive ? IM_COL32(45, 55, 72, 255) : (isHovered ? IM_COL32(40, 48, 62, 255) : IM_COL32(28, 35, 46, 255));
    dl->AddRectFilled(capMin, capMax, capBg, 2.0f);
    dl->AddRect(capMin, capMax, isActive ? IM_COL32(0, 210, 225, 255) : (isHovered ? IM_COL32(65, 85, 115, 255) : IM_COL32(50, 62, 80, 255)), 2.0f, 0, 1.2f);
    // Vertical neon cyan notch
    dl->AddLine(ImVec2(capX, capMin.y + 2.5f), ImVec2(capX, capMax.y - 2.5f), IM_COL32(0, 210, 225, 255), 1.2f);

    return changed;
}

// ----------------------------------------------------------------------------
// SampleViewer Implementation
// ----------------------------------------------------------------------------

SampleViewer::SampleViewer() {
    m_chipFilterOptions = { "All Chips" };
    m_leftPaneWidth = 360.0f;
    m_wasPlaying = false;
}

SampleViewer::~SampleViewer() = default;

void SampleViewer::RescanSamples(const AudioEngine& audioEngine) {
    const uint8_t* data = audioEngine.GetVgmData();
    size_t dataSize = audioEngine.GetVgmDataSize();

    if (data == m_lastDataPtr && dataSize == m_lastDataSize) {
        return;
    }

    const_cast<AudioEngine&>(audioEngine).StopPreviewSample();
    m_lastDataPtr = data;
    m_lastDataSize = dataSize;
    m_samples.clear();
    m_slices.clear();
    m_selectedIndex = -1;
    m_selectedSliceIndex = -1;
    m_cursorNorm = 0.0f;
    m_wasPlaying = false;
    m_mouseTesting = false;
    m_activeTestKey = -1;

    if (!data || dataSize < 0x40) {
        return;
    }

    ExtractSamples(data, dataSize);
    AnalyzeSongPcmSlices(data, dataSize);

    // Build unique chips filter list
    std::set<std::string> uniqueChips;
    for (const auto& item : m_samples) {
        uniqueChips.insert(item.chip);
    }
    for (const auto& item : m_slices) {
        uniqueChips.insert(item.chip);
    }
    m_chipFilterOptions.clear();
    m_chipFilterOptions.push_back("All Chips");
    for (const auto& ch : uniqueChips) {
        m_chipFilterOptions.push_back(ch);
    }
    m_chipFilterIdx = 0;

    if (!m_slices.empty()) {
        m_selectedSliceIndex = 0;
        m_viewMode = 0; // Default to Song PCM Slices view
        m_playbackSampleRate = m_slices[0].defaultSampleRate;
    } else if (!m_samples.empty()) {
        m_selectedIndex = 0;
        m_viewMode = 1; // Default to Raw Banks
        m_playbackSampleRate = m_samples[0].defaultSampleRate;
    }
}

void SampleViewer::ExtractSamples(const uint8_t* vgmData, size_t dataSize) {
    if (dataSize < 0x40) return;

    m_samples.clear();
    m_multiPcmRom.clear();
    m_rf5cRam.clear();

    uint32_t version = ReadLE32(&vgmData[0x08]);
    uint32_t vgmDataOfs = (version >= 0x150) ? (0x34 + ReadLE32(&vgmData[0x34])) : 0x40;
    if (vgmDataOfs < 0x40 || vgmDataOfs >= dataSize) {
        vgmDataOfs = 0x40;
    }

    // Header chip clocks for identification
    uint32_t rf5c68Clk  = (version >= 0x151 && dataSize >= 0x6C) ? ReadLE32(&vgmData[0x68]) : 0;
    uint32_t rf5c164Clk = (version >= 0x151 && dataSize >= 0x70) ? ReadLE32(&vgmData[0x6C]) : 0;
    uint32_t segaPcmClk = (version >= 0x151 && dataSize >= 0x3C) ? ReadLE32(&vgmData[0x38]) : 0;
    uint32_t oki6295Clk = (version >= 0x160 && dataSize >= 0xA0) ? ReadLE32(&vgmData[0x9C]) : 0;
    int nextId = 1;

    // 1. Scan and assemble MultiPCM ROM if 0x89 data blocks exist
    uint32_t multiTotSize = 0;
    bool hasMultiPcm = false;
    {
        size_t p = vgmDataOfs;
        while (p < dataSize) {
            uint8_t cmd = vgmData[p];
            if (cmd == 0x66) break;
            if (cmd == 0x67) {
                if (p + 7 > dataSize) break;
                uint8_t dblkType = vgmData[p + 2];
                uint32_t dblkLen = ReadLE32(&vgmData[p + 3]) & 0x7FFFFFFF;
                if (dblkType == 0x89 && dblkLen >= 8 && p + 7 + dblkLen <= dataSize) {
                    hasMultiPcm = true;
                    uint32_t totSz = ReadLE32(&vgmData[p + 7]);
                    if (totSz > multiTotSize) multiTotSize = totSz;
                }
                p += 7 + dblkLen;
                continue;
            }
            if (cmd == 0x68) p += 12;
            else if (cmd == 0x4F || cmd == 0x50) p += 2;
            else if ((cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF)) p += 3;
            else if (cmd >= 0xC0 && cmd <= 0xDF) p += 4;
            else if (cmd >= 0xE0) p += 5;
            else if (cmd == 0x61) p += 3;
            else p += 1;
        }
    }
    if (hasMultiPcm) {
        if (multiTotSize == 0) multiTotSize = 0x400000;
        m_multiPcmRom.assign(multiTotSize, 0x00);
        size_t p = vgmDataOfs;
        while (p < dataSize) {
            uint8_t cmd = vgmData[p];
            if (cmd == 0x66) break;
            if (cmd == 0x67) {
                if (p + 7 > dataSize) break;
                uint8_t dblkType = vgmData[p + 2];
                uint32_t dblkLen = ReadLE32(&vgmData[p + 3]) & 0x7FFFFFFF;
                if (dblkType == 0x89 && dblkLen >= 8 && p + 7 + dblkLen <= dataSize) {
                    uint32_t rOfs = ReadLE32(&vgmData[p + 11]);
                    size_t chunkLen = dblkLen - 8;
                    if (rOfs + chunkLen <= m_multiPcmRom.size()) {
                        memcpy(&m_multiPcmRom[rOfs], &vgmData[p + 15], chunkLen);
                    }
                }
                p += 7 + dblkLen;
                continue;
            }
            if (cmd == 0x68) p += 12;
            else if (cmd == 0x4F || cmd == 0x50) p += 2;
            else if ((cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF)) p += 3;
            else if (cmd >= 0xC0 && cmd <= 0xDF) p += 4;
            else if (cmd >= 0xE0) p += 5;
            else if (cmd == 0x61) p += 3;
            else p += 1;
        }

        PcmSampleItem multiItem;
        multiItem.id = nextId++;
        multiItem.chip = "MultiPCM";
        multiItem.formatName = "8-bit Signed PCM";
        multiItem.dblkType = 0x89;
        multiItem.fileOffset = (uint32_t)vgmDataOfs;
        multiItem.romOfs = 0;
        multiItem.rawSize = (uint32_t)m_multiPcmRom.size();
        multiItem.defaultSampleRate = 16000;
        multiItem.notes = "ROM Sample Bank (" + std::to_string(m_multiPcmRom.size() / 1024) + " KB)";
        multiItem.decodedPcm = Decode8BitSigned(m_multiPcmRom.data(), m_multiPcmRom.size());
        multiItem.durationSec = (float)multiItem.decodedPcm.size() / 16000.0f;
        multiItem.name = multiItem.chip + " #" + std::to_string(multiItem.id);
        float maxVal = 0.0f;
        for (float s : multiItem.decodedPcm) {
            float a = std::abs(s);
            if (a > maxVal) maxVal = a;
        }
        multiItem.peakDb = (maxVal > 0.00001f) ? (20.0f * std::log10(maxVal)) : -96.0f;
        m_samples.push_back(std::move(multiItem));
    }

    // 2. Scan and assemble RF5C RAM if RF5C is present
    bool hasRf5c = (rf5c68Clk > 0 || rf5c164Clk > 0);
    {
        size_t p = vgmDataOfs;
        while (p < dataSize && !hasRf5c) {
            uint8_t cmd = vgmData[p];
            if (cmd == 0x66) break;
            if (cmd == 0xB0 || cmd == 0xB1 || cmd == 0xC1 || cmd == 0xC2 || cmd == 0x68) { hasRf5c = true; break; }
            if (cmd == 0x67) {
                if (p + 3 <= dataSize && (vgmData[p + 2] == 0xC0 || vgmData[p + 2] == 0xC1 || vgmData[p + 2] == 0x01 || vgmData[p + 2] == 0x02)) { hasRf5c = true; break; }
                uint32_t dblkLen = (p + 7 <= dataSize) ? (ReadLE32(&vgmData[p + 3]) & 0x7FFFFFFF) : 0;
                p += 7 + dblkLen;
                continue;
            }
            if (cmd == 0x68) { hasRf5c = true; break; }
            else if (cmd == 0x4F || cmd == 0x50) p += 2;
            else if ((cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF)) p += 3;
            else if (cmd >= 0xC0 && cmd <= 0xDF) p += 4;
            else if (cmd >= 0xE0) p += 5;
            else if (cmd == 0x61) p += 3;
            else p += 1;
        }
    }
    if (hasRf5c) {
        // Preload 0x67 data blocks 0x01 (RF5C68 PCM) and 0x02 (RF5C164 PCM)
        std::vector<uint8_t> rf5cPcmBlock[2]; // [0] = 0x01, [1] = 0x02
        {
            size_t p = vgmDataOfs;
            while (p < dataSize) {
                uint8_t cmd = vgmData[p];
                if (cmd == 0x66) break;
                if (cmd == 0x67) {
                    if (p + 7 > dataSize) break;
                    uint8_t btype = vgmData[p + 2];
                    uint32_t blen = ReadLE32(&vgmData[p + 3]) & 0x7FFFFFFF;
                    if ((btype == 0x01 || btype == 0x02) && p + 7 + blen <= dataSize) {
                        int idx = (btype == 0x01) ? 0 : 1;
                        rf5cPcmBlock[idx].insert(rf5cPcmBlock[idx].end(), &vgmData[p + 7], &vgmData[p + 7 + blen]);
                    }
                    p += 7 + blen;
                    continue;
                }
                if (cmd == 0x68) p += 12;
                else if (cmd == 0x4F || cmd == 0x50) p += 2;
                else if ((cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF)) p += 3;
                else if (cmd >= 0xC0 && cmd <= 0xDF) p += 4;
                else if (cmd >= 0xE0) p += 5;
                else if (cmd == 0x61) p += 3;
                else p += 1;
            }
        }

        m_rf5cRam.assign(0x10000, 0xFF);
        uint8_t rfBank = 0;
        std::vector<uint8_t> chStreams[8];
        size_t p = vgmDataOfs;
        while (p < dataSize) {
            uint8_t cmd = vgmData[p];
            if (cmd == 0x66) break;
            if (cmd == 0xB0 || cmd == 0xB1) {
                if (p + 3 <= dataSize) {
                    uint8_t reg = vgmData[p + 1];
                    uint8_t val = vgmData[p + 2];
                    if (reg == 0x07 && !(val & 0x40)) rfBank = val & 0x0F;
                }
                p += 3;
                continue;
            }
            if (cmd == 0x67) {
                if (p + 7 > dataSize) break;
                uint8_t dblkType = vgmData[p + 2];
                uint32_t dblkLen = ReadLE32(&vgmData[p + 3]) & 0x7FFFFFFF;
                if ((dblkType == 0xC0 || dblkType == 0xC1) && dblkLen > 2 && p + 7 + dblkLen <= dataSize) {
                    const uint8_t* pl = &vgmData[p + 7];
                    uint16_t addr = pl[0] | ((uint16_t)pl[1] << 8);
                    uint32_t realAddr = ((uint32_t)rfBank << 12) | addr;
                    size_t cLen = dblkLen - 2;
                    if (realAddr + cLen <= m_rf5cRam.size()) {
                        memcpy(&m_rf5cRam[realAddr], pl + 2, cLen);
                    }
                }
                p += 7 + dblkLen;
                continue;
            }
            if (cmd == 0x68) {
                if (p + 12 <= dataSize) {
                    uint8_t btype = vgmData[p + 2] & 0x7F;
                    uint32_t src = ReadLE24(&vgmData[p + 3]);
                    uint32_t dst = ReadLE24(&vgmData[p + 6]) | ((uint32_t)rfBank << 12);
                    uint32_t slen = ReadLE24(&vgmData[p + 9]);
                    if (btype == 0x01 || btype == 0x02) {
                        int idx = (btype == 0x01) ? 0 : 1;
                        const auto& rom = rf5cPcmBlock[idx];
                        if (src < rom.size()) {
                            size_t copyLen = std::min((size_t)slen, rom.size() - src);
                            if (dst < m_rf5cRam.size()) {
                                copyLen = std::min(copyLen, m_rf5cRam.size() - dst);
                                memcpy(&m_rf5cRam[dst], &rom[src], copyLen);
                            }
                            int ch = dst / 0x2000;
                            if (ch >= 0 && ch < 8 && slen <= 1024 && src >= 0x1000) {
                                chStreams[ch].insert(chStreams[ch].end(), &rom[src], &rom[src + copyLen]);
                            }
                        }
                    }
                }
                p += 12;
                continue;
            }
            if (cmd == 0xC1 || cmd == 0xC2) {
                if (p + 4 <= dataSize) {
                    uint16_t addr = (vgmData[p + 1] | ((uint16_t)vgmData[p + 2] << 8)) & 0x0FFF;
                    uint8_t val = vgmData[p + 3];
                    uint32_t realAddr = ((uint32_t)rfBank << 12) | addr;
                    if (realAddr < m_rf5cRam.size()) {
                        m_rf5cRam[realAddr] = val;
                    }
                }
                p += 4;
                continue;
            }
            if (cmd == 0x4F || cmd == 0x50) p += 2;
            else if ((cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF)) p += 3;
            else if (cmd >= 0xC0 && cmd <= 0xDF) p += 4;
            else if (cmd >= 0xE0) p += 5;
            else if (cmd == 0x61) p += 3;
            else p += 1;
        }

        size_t nonSilent = 0;
        for (uint8_t b : m_rf5cRam) {
            if (b != 0x00 && b != 0xFF) nonSilent++;
        }
        if (nonSilent >= 32) {
            PcmSampleItem rfItem;
            rfItem.id = nextId++;
            rfItem.chip = (rf5c164Clk > 0) ? "RF5C164" : "RF5C68";
            rfItem.formatName = "8-bit Sign-Magnitude";
            rfItem.dblkType = 0xC1;
            rfItem.fileOffset = (uint32_t)vgmDataOfs;
            rfItem.romOfs = 0;
            rfItem.rawSize = 65536;
            rfItem.defaultSampleRate = 16000;
            rfItem.notes = "64 KB RAM Bank";
            rfItem.decodedPcm = DecodeRf5cSignMag(m_rf5cRam.data(), 65536);
            rfItem.durationSec = (float)rfItem.decodedPcm.size() / 16000.0f;
            rfItem.name = rfItem.chip + " #" + std::to_string(rfItem.id);
            float maxVal = 0.0f;
            for (float s : rfItem.decodedPcm) {
                float a = std::abs(s);
                if (a > maxVal) maxVal = a;
            }
            rfItem.peakDb = (maxVal > 0.00001f) ? (20.0f * std::log10(maxVal)) : -96.0f;
            m_samples.push_back(std::move(rfItem));
        }

        for (int c = 0; c < 8; ++c) {
            if (chStreams[c].size() > 8192) {
                PcmSampleItem vItem;
                vItem.id = nextId++;
                vItem.chip = (rf5c164Clk > 0) ? "RF5C164" : "RF5C68";
                vItem.formatName = "8-bit Sign-Magnitude";
                vItem.dblkType = 0x68;
                vItem.fileOffset = (uint32_t)vgmDataOfs;
                vItem.romOfs = 0;
                vItem.rawSize = (uint32_t)chStreams[c].size();
                vItem.defaultSampleRate = 16000;
                vItem.notes = "Voice " + std::to_string(c + 1) + " (Ch " + std::to_string(c + 1) + ") Stream";
                vItem.decodedPcm = DecodeRf5cSignMag(chStreams[c].data(), chStreams[c].size());
                vItem.durationSec = (float)vItem.decodedPcm.size() / 16000.0f;
                vItem.name = vItem.chip + " #" + std::to_string(vItem.id) + " - Voice " + std::to_string(c + 1);
                float maxVal = 0.0f;
                for (float s : vItem.decodedPcm) {
                    float a = std::abs(s);
                    if (a > maxVal) maxVal = a;
                }
                vItem.peakDb = (maxVal > 0.00001f) ? (20.0f * std::log10(maxVal)) : -96.0f;
                m_samples.push_back(std::move(vItem));
            }
        }
    }

    size_t filePos = vgmDataOfs;

    while (filePos < dataSize) {
        uint8_t cmd = vgmData[filePos];

        if (cmd == 0x66) {
            break; // End of sound data
        }

        if (cmd == 0x67) {
            if (filePos + 7 > dataSize) break;
            uint8_t dblkType = vgmData[filePos + 2];
            uint32_t dblkLenRaw = ReadLE32(&vgmData[filePos + 3]);
            uint32_t dblkLen = dblkLenRaw & 0x7FFFFFFF;

            if (filePos + 7 + dblkLen > dataSize) {
                dblkLen = (uint32_t)(dataSize - (filePos + 7));
            }

            const uint8_t* payload = &vgmData[filePos + 7];

            PcmSampleItem item;
            item.id = nextId;
            item.dblkType = dblkType;
            item.fileOffset = (uint32_t)filePos;
            item.rawSize = dblkLen;

            const uint8_t* samplePayload = payload;
            size_t sampleLen = dblkLen;
            uint32_t romOfs = 0;
            if (dblkType >= 0x80 && dblkType <= 0xBF) {
                if (dblkLen > 8) {
                    romOfs = ReadLE32(payload + 4);
                    samplePayload = payload + 8;
                    sampleLen = dblkLen - 8;
                } else {
                    sampleLen = 0;
                }
            }
            item.romOfs = romOfs;

            bool isSupported = false;

            switch (dblkType) {
                case 0x00: // YM2612 PCM
                    item.chip = "YM2612";
                    item.formatName = "8-bit Unsigned PCM";
                    item.defaultSampleRate = 22050;
                    item.notes = "Direct DAC Stream";
                    item.decodedPcm = Decode8BitUnsigned(payload, dblkLen);
                    isSupported = true;
                    break;

                case 0x01: // RF5C68 PCM
                case 0x02: // RF5C164 PCM
                    item.chip = (dblkType == 0x02 || rf5c164Clk > 0) ? "RF5C164" : "RF5C68";
                    item.formatName = "8-bit Sign-Magnitude";
                    item.defaultSampleRate = 16000;
                    item.notes = "PCM Sample Bank";
                    item.decodedPcm = DecodeRf5cSignMag(payload, dblkLen);
                    isSupported = true;
                    break;

                case 0x04: // OKIM6258
                    item.chip = "OKIM6258";
                    item.formatName = "OKI ADPCM (4-bit)";
                    item.defaultSampleRate = 15600;
                    item.notes = "ADPCM Stream";
                    item.decodedPcm = DecodeOkiAdpcm(payload, dblkLen);
                    isSupported = true;
                    break;

                case 0x07: // NES APU DPCM
                    item.chip = "NES APU";
                    item.formatName = "1-bit DPCM";
                    item.defaultSampleRate = 33140;
                    item.notes = "Delta Modulation DPCM";
                    item.decodedPcm = DecodeNesDpcm(payload, dblkLen);
                    isSupported = true;
                    break;

                case 0x80: // SegaPCM
                    item.chip = "SegaPCM";
                    item.formatName = "8-bit Linear PCM";
                    item.defaultSampleRate = 15625;
                    {
                        char nBuf[64];
                        std::snprintf(nBuf, sizeof(nBuf), "ROM Offset: 0x%06X", romOfs);
                        item.notes = nBuf;
                    }
                    if (sampleLen > 0) {
                        item.decodedPcm = Decode8BitUnsigned(samplePayload, sampleLen);
                        isSupported = true;
                    }
                    break;

                case 0x89: // YMW258 (MultiPCM)
                    if (!m_multiPcmRom.empty()) {
                        break; // Already assembled into unified ROM bank
                    }
                    item.chip = "MultiPCM";
                    item.formatName = "8-bit Signed PCM";
                    item.defaultSampleRate = 16000;
                    item.romOfs = romOfs;
                    {
                        char nBuf[64];
                        if (romOfs == 0) {
                            std::snprintf(nBuf, sizeof(nBuf), "Instrument Header Table");
                        } else {
                            std::snprintf(nBuf, sizeof(nBuf), "ROM Offset: 0x%06X", romOfs);
                        }
                        item.notes = nBuf;
                    }
                    if (sampleLen > 0) {
                        item.decodedPcm = Decode8BitSigned(samplePayload, sampleLen);
                        isSupported = true;
                    }
                    break;

                case 0x8B: // OKIM6295
                    item.chip = "OKIM6295";
                    item.formatName = "OKI ADPCM (4-bit)";
                    item.defaultSampleRate = 8000;
                    {
                        char nBuf[64];
                        std::snprintf(nBuf, sizeof(nBuf), "ROM Offset: 0x%06X", romOfs);
                        item.notes = nBuf;
                    }
                    if (sampleLen > 0) {
                        item.decodedPcm = DecodeOkiAdpcm(samplePayload, sampleLen);
                        isSupported = true;
                    }
                    break;

                case 0x8C: // K054539
                    item.chip = "K054539";
                    item.formatName = "8-bit Signed PCM";
                    item.defaultSampleRate = 48000;
                    {
                        char nBuf[64];
                        std::snprintf(nBuf, sizeof(nBuf), "ROM Offset: 0x%06X", romOfs);
                        item.notes = nBuf;
                    }
                    if (sampleLen > 0) {
                        item.decodedPcm = Decode8BitSigned(samplePayload, sampleLen);
                        isSupported = true;
                    }
                    break;

                case 0x8D: // C140
                    item.chip = "C140";
                    item.formatName = "8-bit Signed PCM";
                    item.defaultSampleRate = 21300;
                    {
                        char nBuf[64];
                        std::snprintf(nBuf, sizeof(nBuf), "ROM Offset: 0x%06X", romOfs);
                        item.notes = nBuf;
                    }
                    if (sampleLen > 0) {
                        item.decodedPcm = Decode8BitSigned(samplePayload, sampleLen);
                        isSupported = true;
                    }
                    break;

                case 0x8E: // K053260
                    item.chip = "K053260";
                    item.formatName = "8-bit Signed PCM";
                    item.defaultSampleRate = 32000;
                    {
                        char nBuf[64];
                        std::snprintf(nBuf, sizeof(nBuf), "ROM Offset: 0x%06X", romOfs);
                        item.notes = nBuf;
                    }
                    if (sampleLen > 0) {
                        item.decodedPcm = Decode8BitSigned(samplePayload, sampleLen);
                        isSupported = true;
                    }
                    break;

                case 0x8F: // QSound
                    item.chip = "QSound";
                    item.formatName = "8-bit Signed PCM";
                    item.defaultSampleRate = 24000;
                    {
                        char nBuf[64];
                        std::snprintf(nBuf, sizeof(nBuf), "ROM Offset: 0x%06X", romOfs);
                        item.notes = nBuf;
                    }
                    if (sampleLen > 0) {
                        item.decodedPcm = Decode8BitSigned(samplePayload, sampleLen);
                        isSupported = true;
                    }
                    break;

                case 0x92: // C352
                    item.chip = "C352";
                    item.formatName = "8-bit Signed PCM";
                    item.defaultSampleRate = 24000;
                    {
                        char nBuf[64];
                        std::snprintf(nBuf, sizeof(nBuf), "ROM Offset: 0x%06X", romOfs);
                        item.notes = nBuf;
                    }
                    if (sampleLen > 0) {
                        item.decodedPcm = Decode8BitSigned(samplePayload, sampleLen);
                        isSupported = true;
                    }
                    break;

                case 0xC0: // RF5C68 RAM
                case 0xC1: { // RF5C164 RAM
                    if (!m_rf5cRam.empty()) {
                        break; // Already assembled into unified RF5C RAM bank
                    }
                    if (dblkLen > 2) {
                        uint16_t addr = payload[0] | ((uint16_t)payload[1] << 8);
                        const uint8_t* ramData = payload + 2;
                        size_t ramLen = dblkLen - 2;

                        // Only include RAM chunks that contain non-trivial audio (not entirely 0x00 or 0xFF)
                        size_t nonSilent = 0;
                        for (size_t k = 0; k < ramLen; ++k) {
                            if (ramData[k] != 0x00 && ramData[k] != 0xFF) {
                                nonSilent++;
                            }
                        }

                        if (nonSilent >= 32) {
                            item.chip = (dblkType == 0xC1 || rf5c164Clk > 0) ? "RF5C164" : "RF5C68";
                            item.formatName = "8-bit Sign-Magnitude";
                            item.defaultSampleRate = 16000;
                            char noteBuf[64];
                            std::snprintf(noteBuf, sizeof(noteBuf), "RAM Write at 0x%04X", addr);
                            item.notes = noteBuf;
                            item.decodedPcm = DecodeRf5cSignMag(ramData, ramLen);
                            isSupported = true;
                        }
                    }
                    break;
                }

                case 0xC2: // NES DPCM RAM
                    if (dblkLen > 2) {
                        item.chip = "NES APU";
                        item.formatName = "1-bit DPCM";
                        item.defaultSampleRate = 33140;
                        item.notes = "RAM DPCM Transfer";
                        item.decodedPcm = DecodeNesDpcm(payload + 2, dblkLen - 2);
                        isSupported = true;
                    }
                    break;

                default:
                    if (dblkType >= 0x80 && dblkType <= 0xBF) {
                        char chipName[32];
                        std::snprintf(chipName, sizeof(chipName), "PCM ROM (0x%02X)", dblkType);
                        item.chip = chipName;
                        item.formatName = "8-bit Signed PCM";
                        item.defaultSampleRate = 16000;
                        item.notes = "Generic Sample ROM";
                        item.decodedPcm = Decode8BitSigned(payload, dblkLen);
                        isSupported = true;
                    }
                    break;
            }

            if (isSupported && !item.decodedPcm.empty()) {
                item.name = item.chip + " #" + std::to_string(item.id);
                item.durationSec = (float)item.decodedPcm.size() / (float)item.defaultSampleRate;

                // Compute peak level
                float maxVal = 0.0f;
                for (float s : item.decodedPcm) {
                    float a = std::abs(s);
                    if (a > maxVal) maxVal = a;
                }
                if (maxVal > 0.00001f) {
                    item.peakDb = 20.0f * std::log10(maxVal);
                } else {
                    item.peakDb = -96.0f;
                }

                m_samples.push_back(std::move(item));
                nextId++;
            }

            filePos += 7 + dblkLen;
            continue;
        }

        // Advance past other VGM commands
        if (cmd == 0x68) {
            filePos += 12;
        } else if (cmd == 0x4F || cmd == 0x50) {
            filePos += 2;
        } else if ((cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF)) {
            filePos += 3;
        } else if (cmd >= 0xD0 && cmd <= 0xDF) {
            filePos += 4;
        } else if (cmd >= 0xE0) {
            filePos += 5;
        } else if (cmd == 0x61) {
            filePos += 3;
        } else if (cmd == 0x62 || cmd == 0x63 || cmd == 0x64) {
            filePos += 1;
        } else if (cmd >= 0x70 && cmd <= 0x8F) {
            filePos += 1;
        } else {
            filePos += 1;
        }
    }
}

void SampleViewer::AnalyzeSongPcmSlices(const uint8_t* vgmData, size_t dataSize) {
    m_slices.clear();
    m_selectedSliceIndex = -1;
    if (!vgmData || dataSize < 0x40 || m_samples.empty()) return;

    uint32_t version = ReadLE32(&vgmData[0x08]);
    uint32_t vgmDataOfs = (version >= 0x150) ? (0x34 + ReadLE32(&vgmData[0x34])) : 0x40;
    if (vgmDataOfs < 0x40 || vgmDataOfs >= dataSize) vgmDataOfs = 0x40;

    size_t filePos = vgmDataOfs;
    uint32_t curSample = 0;

    struct RawSliceTrigger {
        std::string chip;
        std::string channel;
        uint32_t timeSample = 0;
        uint32_t offset = 0;
        uint32_t length = 0;
        uint32_t sampleRate = 16000;
        std::vector<float> capturedPcm;

        RawSliceTrigger() = default;
        RawSliceTrigger(std::string chp, std::string chn, uint32_t ts, uint32_t ofs, uint32_t len, uint32_t sr, std::vector<float> pcm = {})
            : chip(std::move(chp)), channel(std::move(chn)), timeSample(ts), offset(ofs), length(len), sampleRate(sr), capturedPcm(std::move(pcm)) {}
    };
    std::vector<RawSliceTrigger> rawTriggers;

    // YM2612 state
    uint32_t ymDacOffset = 0;
    bool ymDacActive = false;
    uint32_t ymDacStartSample = 0;
    uint32_t ymDacLength = 0;
    uint32_t ymDacLastSample = 0;

    auto flushYmDac = [&]() {
        if (ymDacActive && ymDacLength > 0) {
            rawTriggers.push_back({ "YM2612", "Ch 6 / DAC", ymDacStartSample, ymDacOffset, ymDacLength, 22050, {} });
        }
        ymDacActive = false;
        ymDacLength = 0;
    };

    // Chip clocks for slice analysis
    uint32_t rf5c164Clk = (version >= 0x151 && dataSize >= 0x70) ? ReadLE32(&vgmData[0x6C]) : 0;

    // SegaPCM state
    uint8_t segaLowRegs[16][8] = {{0}};
    uint8_t segaHighRegs[16][8] = {{0}};
    for (int c = 0; c < 16; ++c) segaHighRegs[c][6] = 1; // start disabled

    // RF5C realtime simulation state
    std::vector<uint8_t> rf5cPcmBlock[2]; // [0] = 0x01, [1] = 0x02
    {
        size_t p = vgmDataOfs;
        while (p < dataSize) {
            uint8_t cmd = vgmData[p];
            if (cmd == 0x66) break;
            if (cmd == 0x67) {
                if (p + 7 > dataSize) break;
                uint8_t btype = vgmData[p + 2];
                uint32_t blen = ReadLE32(&vgmData[p + 3]) & 0x7FFFFFFF;
                if ((btype == 0x01 || btype == 0x02) && p + 7 + blen <= dataSize) {
                    int idx = (btype == 0x01) ? 0 : 1;
                    rf5cPcmBlock[idx].insert(rf5cPcmBlock[idx].end(), &vgmData[p + 7], &vgmData[p + 7 + blen]);
                }
                p += 7 + blen;
                continue;
            }
            if (cmd == 0x68) p += 12;
            else if (cmd == 0x4F || cmd == 0x50) p += 2;
            else if ((cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF)) p += 3;
            else if (cmd >= 0xC0 && cmd <= 0xDF) p += 4;
            else if (cmd >= 0xE0) p += 5;
            else if (cmd == 0x61) p += 3;
            else p += 1;
        }
    }
    std::vector<uint8_t> rf5cSimRam(0x10000, 0xFF);
    uint8_t rfBank = 0;
    int rf5cCurCh = 0;
    uint8_t rf5cStart[8] = {0};
    uint16_t rf5cLoop[8] = {0};
    uint16_t rf5cStep[8] = {0};
    bool rf5cEnabled[8] = {false};

    // MultiPCM state
    int multiSlot = 0;
    int multiReg = 0;
    uint8_t multiSlotRegs[28][8] = {{0}};
    uint32_t multiBank0 = 0;
    uint32_t multiBank1 = 0;
    bool multiSegaBanking = false;

    // NES APU state
    uint8_t dmcRegs[32] = {0};

    // DAC Stream state
    struct DacStreamState {
        uint8_t chipType = 0;
        uint32_t sampleRate = 22050;
    } dacStreams[8];

    while (filePos < dataSize) {
        uint8_t cmd = vgmData[filePos];

        if (cmd == 0x66) break;

        if (cmd == 0x67) {
            if (filePos + 7 > dataSize) break;
            uint8_t dblkType = vgmData[filePos + 2];
            uint32_t dblkLen = ReadLE32(&vgmData[filePos + 3]) & 0x7FFFFFFF;
            if ((dblkType == 0xC0 || dblkType == 0xC1) && dblkLen > 2 && filePos + 7 + dblkLen <= dataSize) {
                const uint8_t* pl = &vgmData[filePos + 7];
                uint16_t addr = pl[0] | ((uint16_t)pl[1] << 8);
                uint32_t realAddr = ((uint32_t)rfBank << 12) | addr;
                size_t cLen = dblkLen - 2;
                if (realAddr + cLen <= rf5cSimRam.size()) {
                    memcpy(&rf5cSimRam[realAddr], pl + 2, cLen);
                }
            }
            filePos += 7 + dblkLen;
            continue;
        }

        // YM2612 DAC bank seek (0xE0 dddddddd)
        if (cmd == 0xE0) {
            if (filePos + 5 <= dataSize) {
                flushYmDac();
                ymDacOffset = ReadLE32(&vgmData[filePos + 1]);
            }
            filePos += 5;
            continue;
        }

        // YM2612 DAC write + delay (0x80..0x8F)
        if (cmd >= 0x80 && cmd <= 0x8F) {
            uint32_t delay = (cmd & 0x0F);
            if (!ymDacActive) {
                ymDacActive = true;
                ymDacStartSample = curSample;
                ymDacLength = 0;
            }
            ymDacLength++;
            ymDacLastSample = curSample;
            curSample += delay;
            filePos += 1;
            continue;
        }

        if (ymDacActive && (curSample - ymDacLastSample > 2000)) {
            flushYmDac();
        }

        // DAC Stream Control (0x90..0x95)
        if (cmd == 0x90) {
            if (filePos + 5 <= dataSize) {
                uint8_t sId = vgmData[filePos + 1] & 0x07;
                dacStreams[sId].chipType = vgmData[filePos + 2];
            }
            filePos += 5;
            continue;
        }
        if (cmd == 0x92) {
            if (filePos + 6 <= dataSize) {
                uint8_t sId = vgmData[filePos + 1] & 0x07;
                dacStreams[sId].sampleRate = ReadLE32(&vgmData[filePos + 2]);
            }
            filePos += 6;
            continue;
        }
        if (cmd == 0x93) {
            if (filePos + 11 <= dataSize) {
                uint8_t sId = vgmData[filePos + 1] & 0x07;
                uint32_t ofs = ReadLE32(&vgmData[filePos + 2]);
                uint32_t slen = ReadLE32(&vgmData[filePos + 7]);
                rawTriggers.push_back({
                    "DAC Stream",
                    "Stream " + std::to_string(sId + 1),
                    curSample,
                    ofs,
                    slen,
                    dacStreams[sId].sampleRate > 0 ? dacStreams[sId].sampleRate : 22050
                });
            }
            filePos += 11;
            continue;
        }
        if (cmd == 0x91) { filePos += 5; continue; }
        if (cmd == 0x94) { filePos += 2; continue; }
        if (cmd == 0x95) { filePos += 5; continue; }

        // SegaPCM write (0xC0 aaaa dd)
        if (cmd == 0xC0) {
            if (filePos + 4 <= dataSize) {
                uint16_t addr = vgmData[filePos + 1] | ((uint16_t)vgmData[filePos + 2] << 8);
                uint8_t val = vgmData[filePos + 3];
                if (addr < 0x0100) {
                    int ch = (addr >> 3) & 0x0F;
                    int reg = addr & 0x07;
                    if (addr < 0x80) {
                        segaLowRegs[ch][reg] = val;
                    } else {
                        uint8_t oldFlags = segaHighRegs[ch][6];
                        segaHighRegs[ch][reg] = val;
                        if (reg == 6 && (oldFlags & 1) != 0 && (val & 1) == 0) {
                            uint32_t startAddr = (uint32_t)segaHighRegs[ch][4] | ((uint32_t)segaHighRegs[ch][5] << 8);
                            uint32_t endAddr = ((uint32_t)segaLowRegs[ch][6] + 1) << 8;
                            uint32_t slen = (endAddr > startAddr) ? (endAddr - startAddr) : 256;
                            rawTriggers.push_back({
                                "SegaPCM",
                                "PCM " + std::to_string(ch + 1),
                                curSample,
                                startAddr,
                                slen,
                                15625
                            });
                        }
                    }
                }
            }
            filePos += 4;
            continue;
        }

        // RF5C68 (0xB0) / RF5C164 (0xB1)
        if (cmd == 0xB0 || cmd == 0xB1) {
            if (filePos + 3 <= dataSize) {
                uint8_t reg = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                std::string chipName = (cmd == 0xB1 || rf5c164Clk > 0) ? "RF5C164" : "RF5C68";
                if (reg == 0x07) {
                    if (!(val & 0x40)) rfBank = val & 0x0F;
                    else rf5cCurCh = val & 0x07;
                } else if (reg == 0x08) {
                    for (int ch = 0; ch < 8; ++ch) {
                        bool newEn = !((val >> ch) & 1);
                        bool oldEn = rf5cEnabled[ch];
                        rf5cEnabled[ch] = newEn;
                        if (newEn && !oldEn) {
                            uint32_t startAddr = (uint32_t)rf5cStart[ch] << 8;
                            uint32_t endAddr = startAddr;
                            while (endAddr < rf5cSimRam.size() && rf5cSimRam[endAddr] != 0xFF) {
                                endAddr++;
                            }
                            uint32_t slen = (endAddr > startAddr) ? (endAddr - startAddr) : 256;
                            if (slen > 512) {
                                RawSliceTrigger trig;
                                trig.chip = chipName;
                                trig.channel = "Ch " + std::to_string(ch + 1);
                                trig.timeSample = curSample;
                                trig.offset = startAddr;
                                trig.length = slen;
                                trig.sampleRate = 16000;
                                trig.capturedPcm = DecodeRf5cSignMag(&rf5cSimRam[startAddr], slen);
                                rawTriggers.push_back(std::move(trig));
                            }
                        }
                    }
                } else if (reg <= 0x06) {
                    int ch = rf5cCurCh;
                    if (ch >= 0 && ch < 8) {
                        if (reg == 0x02) rf5cStep[ch] = (rf5cStep[ch] & 0xFF00) | val;
                        else if (reg == 0x03) rf5cStep[ch] = (rf5cStep[ch] & 0x00FF) | ((uint16_t)val << 8);
                        else if (reg == 0x04) rf5cLoop[ch] = (rf5cLoop[ch] & 0xFF00) | val;
                        else if (reg == 0x05) rf5cLoop[ch] = (rf5cLoop[ch] & 0x00FF) | ((uint16_t)val << 8);
                        else if (reg == 0x06) rf5cStart[ch] = val;
                    }
                }
            }
            filePos += 3;
            continue;
        }

        // RF5C memory write (0xC1 / 0xC2)
        if (cmd == 0xC1 || cmd == 0xC2) {
            if (filePos + 4 <= dataSize) {
                uint16_t addr = (vgmData[filePos + 1] | ((uint16_t)vgmData[filePos + 2] << 8)) & 0x0FFF;
                uint8_t val = vgmData[filePos + 3];
                uint32_t realAddr = ((uint32_t)rfBank << 12) | addr;
                if (realAddr < rf5cSimRam.size()) {
                    rf5cSimRam[realAddr] = val;
                }
            }
            filePos += 4;
            continue;
        }

        // MultiPCM banking (0xC3)
        if (cmd == 0xC3) {
            if (filePos + 4 <= dataSize) {
                uint8_t bmask = vgmData[filePos + 1] & 3;
                uint8_t d = vgmData[filePos + 2];
                multiSegaBanking = true;
                if (bmask == 3 && !(d & 8)) {
                    multiBank0 = (d / 16) << 20;
                    multiBank1 = multiBank0 | 0x80000;
                } else {
                    if (bmask & 2) multiBank0 = (d / 8) << 19;
                    if (bmask & 1) multiBank1 = (d / 8) << 19;
                }
            }
            filePos += 4;
            continue;
        }

        // MultiPCM register write (0xB5 aa dd)
        if (cmd == 0xB5) {
            if (filePos + 3 <= dataSize) {
                uint8_t offset = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                static const int VALUE_TO_CH[32] = {
                    0, 1, 2, 3, 4, 5, 6, -1,
                    7, 8, 9, 10, 11, 12, 13, -1,
                    14, 15, 16, 17, 18, 19, 20, -1,
                    21, 22, 23, 24, 25, 26, 27, -1
                };
                if (offset == 1) {
                    multiSlot = VALUE_TO_CH[val & 0x1F];
                } else if (offset == 2) {
                    multiReg = (val > 7) ? 7 : val;
                } else if (offset == 0) {
                    if (multiSlot >= 0 && multiSlot < 28) {
                        int ch = multiSlot;
                        int reg = multiReg;
                        multiSlotRegs[ch][reg] = val;
                        if (reg == 4 && (val & 0x80)) { // KeyOn
                            uint16_t sIdx = (uint16_t)multiSlotRegs[ch][1] | (((uint16_t)multiSlotRegs[ch][2] & 1) << 8);
                            size_t tAddr = (size_t)sIdx * 12;
                            if (tAddr + 12 <= m_multiPcmRom.size()) {
                                uint32_t start = ((uint32_t)m_multiPcmRom[tAddr] << 16) | ((uint32_t)m_multiPcmRom[tAddr + 1] << 8) | (uint32_t)m_multiPcmRom[tAddr + 2];
                                start &= 0x3FFFFF;
                                if (multiSegaBanking) {
                                    start &= 0x1FFFFF;
                                    if (start & 0x100000) {
                                        if (start & 0x080000) start = (start & 0x07FFFF) | multiBank1;
                                        else start = (start & 0x07FFFF) | multiBank0;
                                    }
                                }
                                uint16_t rawEnd = ((uint16_t)m_multiPcmRom[tAddr + 5] << 8) | (uint16_t)m_multiPcmRom[tAddr + 6];
                                uint32_t endLen = (0x10000 - rawEnd) & 0xFFFF;
                                if (endLen > 0) {
                                    rawTriggers.push_back({
                                        "MultiPCM",
                                        "Ch " + std::to_string(ch + 1),
                                        curSample,
                                        start,
                                        endLen,
                                        16000
                                    });
                                }
                            }
                        }
                    }
                } else if (offset == 0x10) {
                    multiSegaBanking = true;
                    multiBank0 = (val << 20);
                    multiBank1 = (val << 20) | 0x80000;
                } else if (offset == 0x11) {
                    multiSegaBanking = true;
                    multiBank0 = val << 19;
                } else if (offset == 0x12) {
                    multiSegaBanking = true;
                    multiBank1 = val << 19;
                }
            }
            filePos += 3;
            continue;
        }

        // NES APU write (0xB4 aa dd)
        if (cmd == 0xB4) {
            if (filePos + 3 <= dataSize) {
                uint8_t reg = vgmData[filePos + 1];
                uint8_t val = vgmData[filePos + 2];
                if (reg < 32) dmcRegs[reg] = val;
                if (reg == 0x15 && (val & 0x10)) {
                    uint32_t addr = (uint32_t)dmcRegs[0x12] * 64;
                    uint32_t slen = (uint32_t)dmcRegs[0x13] * 16 + 1;
                    rawTriggers.push_back({
                        "NES APU",
                        "DMC",
                        curSample,
                        addr,
                        slen,
                        33140
                    });
                }
            }
            filePos += 3;
            continue;
        }

        // Delays
        if (cmd == 0x61) {
            if (filePos + 3 <= dataSize) curSample += ReadLE16(&vgmData[filePos + 1]);
            filePos += 3;
            continue;
        }
        if (cmd == 0x62) { curSample += 735; filePos += 1; continue; }
        if (cmd == 0x63) { curSample += 882; filePos += 1; continue; }
        if (cmd >= 0x70 && cmd <= 0x7F) { curSample += (cmd & 0x0F) + 1; filePos += 1; continue; }

        if (cmd == 0x68) {
            if (filePos + 12 <= dataSize) {
                uint8_t btype = vgmData[filePos + 2] & 0x7F;
                uint32_t src = ReadLE24(&vgmData[filePos + 3]);
                uint32_t dst = ReadLE24(&vgmData[filePos + 6]) | ((uint32_t)rfBank << 12);
                uint32_t slen = ReadLE24(&vgmData[filePos + 9]);
                if (btype == 0x01 || btype == 0x02) {
                    int idx = (btype == 0x01) ? 0 : 1;
                    const auto& rom = rf5cPcmBlock[idx];
                    if (src < rom.size()) {
                        size_t cLen = std::min((size_t)slen, rom.size() - src);
                        if (dst < rf5cSimRam.size()) {
                            cLen = std::min(cLen, rf5cSimRam.size() - dst);
                            memcpy(&rf5cSimRam[dst], &rom[src], cLen);
                        }
                    }
                }
            }
            filePos += 12;
            continue;
        }
        if (cmd == 0x4F || cmd == 0x50) { filePos += 2; continue; }
        if ((cmd >= 0x51 && cmd <= 0x5F) || (cmd >= 0xA0 && cmd <= 0xBF)) { filePos += 3; continue; }
        if (cmd >= 0xD0 && cmd <= 0xDF) { filePos += 4; continue; }
        if (cmd >= 0xE0) { filePos += 5; continue; }
        filePos += 1;
    }

    flushYmDac();

    // Group and deduplicate identical slices
    struct SliceKey {
        std::string chip;
        uint32_t offset;
        uint32_t length;
        bool operator<(const SliceKey& o) const {
            if (chip != o.chip) return chip < o.chip;
            if (offset != o.offset) return offset < o.offset;
            return length < o.length;
        }
    };

    struct GroupedSlice {
        RawSliceTrigger firstTrigger;
        int count = 0;
        std::vector<float> occurrences;
    };

    std::map<SliceKey, GroupedSlice> grouped;
    std::vector<SliceKey> order;

    for (const auto& trig : rawTriggers) {
        SliceKey k { trig.chip, trig.offset, trig.length };
        auto it = grouped.find(k);
        if (it == grouped.end()) {
            GroupedSlice g;
            g.firstTrigger = trig;
            g.count = 1;
            g.occurrences.push_back((float)trig.timeSample / 44100.0f);
            grouped[k] = g;
            order.push_back(k);
        } else {
            it->second.count++;
            it->second.occurrences.push_back((float)trig.timeSample / 44100.0f);
        }
    }

    int nextSliceId = 1;
    for (const auto& k : order) {
        const auto& g = grouped[k];
        const auto& trig = g.firstTrigger;

        PcmSliceItem slice;
        slice.id = nextSliceId++;
        slice.chip = trig.chip;
        slice.channelName = trig.channel;
        slice.firstSamplePos = trig.timeSample;
        slice.songTimeSec = (float)trig.timeSample / 44100.0f;
        char timeBuf[32];
        int mins = (int)(slice.songTimeSec / 60.0f);
        float secs = std::fmod(slice.songTimeSec, 60.0f);
        std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%05.2f", mins, secs);
        slice.songTimeStr = timeBuf;
        slice.sliceOffset = trig.offset;
        slice.sliceLength = trig.length;
        slice.sliceEndOffset = trig.offset + trig.length;
        slice.triggerCount = g.count;
        slice.occurrencesSec = g.occurrences;
        slice.defaultSampleRate = trig.sampleRate;

        // Find parent sample in m_samples
        int matchedIdx = -1;
        for (size_t s = 0; s < m_samples.size(); ++s) {
            if (trig.chip == "RF5C164" || trig.chip == "RF5C68") {
                if ((m_samples[s].chip == "RF5C164" || m_samples[s].chip == "RF5C68") &&
                    m_samples[s].notes.find("RAM Bank") != std::string::npos) {
                    matchedIdx = (int)s;
                    break;
                }
            } else if (m_samples[s].chip == trig.chip || 
                (trig.chip == "DAC Stream" && m_samples[s].chip == "YM2612") ||
                m_samples[s].chip.find(trig.chip) != std::string::npos ||
                trig.chip.find(m_samples[s].chip) != std::string::npos) {
                matchedIdx = (int)s;
                break;
            }
        }
        if (matchedIdx == -1 && !m_samples.empty()) matchedIdx = 0;

        slice.parentSampleIndex = matchedIdx;
        if (matchedIdx >= 0 && matchedIdx < (int)m_samples.size()) {
            const auto& parent = m_samples[matchedIdx];
            slice.parentSampleId = parent.id;
            slice.defaultSampleRate = parent.defaultSampleRate;

            size_t sampleStart = 0;
            size_t sampleEnd = 0;

            if (slice.chip == "NES APU") {
                sampleStart = (size_t)slice.sliceOffset * 8;
                sampleEnd = (size_t)slice.sliceEndOffset * 8;
            } else if (slice.chip == "SegaPCM") {
                if (parent.romOfs > 0 && slice.sliceOffset >= parent.romOfs) {
                    sampleStart = slice.sliceOffset - parent.romOfs;
                } else if (slice.sliceOffset < parent.decodedPcm.size()) {
                    sampleStart = slice.sliceOffset;
                } else {
                    sampleStart = slice.sliceOffset % parent.decodedPcm.size();
                }
                sampleEnd = sampleStart + slice.sliceLength;
            } else {
                if (parent.romOfs > 0 && slice.sliceOffset >= parent.romOfs) {
                    sampleStart = slice.sliceOffset - parent.romOfs;
                } else {
                    sampleStart = slice.sliceOffset;
                }
                sampleEnd = sampleStart + slice.sliceLength;
            }

            slice.sampleStartInParent = sampleStart;
            slice.sampleEndInParent = sampleEnd;

            if (!trig.capturedPcm.empty()) {
                slice.decodedPcm = trig.capturedPcm;
            } else if (sampleStart < parent.decodedPcm.size()) {
                size_t actualEnd = std::min(sampleEnd, parent.decodedPcm.size());
                if (actualEnd > sampleStart) {
                    slice.decodedPcm.assign(parent.decodedPcm.begin() + sampleStart, parent.decodedPcm.begin() + actualEnd);
                }
            }
        }

        // Fallback decoding if slice.decodedPcm is empty
        if (slice.decodedPcm.empty()) {
            if (!trig.capturedPcm.empty()) {
                slice.decodedPcm = trig.capturedPcm;
            } else if (slice.chip == "MultiPCM" && slice.sliceOffset < m_multiPcmRom.size()) {
                size_t actualLen = std::min((size_t)slice.sliceLength, m_multiPcmRom.size() - slice.sliceOffset);
                slice.decodedPcm = Decode8BitSigned(&m_multiPcmRom[slice.sliceOffset], actualLen);
            } else if ((slice.chip == "RF5C164" || slice.chip == "RF5C68") && slice.sliceOffset < m_rf5cRam.size()) {
                size_t actualLen = std::min((size_t)slice.sliceLength, m_rf5cRam.size() - slice.sliceOffset);
                slice.decodedPcm = DecodeRf5cSignMag(&m_rf5cRam[slice.sliceOffset], actualLen);
            }
        }

        if (slice.decodedPcm.empty()) {
            slice.decodedPcm.push_back(0.0f);
        }

        slice.durationSec = (float)slice.decodedPcm.size() / (float)slice.defaultSampleRate;

        float maxVal = 0.0f;
        for (float s : slice.decodedPcm) {
            float a = std::abs(s);
            if (a > maxVal) maxVal = a;
        }
        slice.peakDb = (maxVal > 0.00001f) ? (20.0f * std::log10(maxVal)) : -96.0f;

        m_slices.push_back(std::move(slice));
    }
}

void SampleViewer::TriggerAuditionPcm(AudioEngine& audioEngine, const std::vector<float>& pcmData, uint32_t sampleRate, int midiNote) {
    if (pcmData.empty()) return;
    float semitoneOffset = (float)(midiNote - 60);
    float pitchRatio = std::pow(2.0f, semitoneOffset / 12.0f) * ((float)sampleRate / 44100.0f);
    audioEngine.SetPreviewSampleVolume(m_playbackVolume * m_testVolume);
    audioEngine.PlayPreviewSamplePitched(pcmData, pitchRatio, 0);
}

void SampleViewer::StopAuditionPcm(AudioEngine& audioEngine) {
    // Kept for future envelope release if needed
}

void SampleViewer::RenderAuditionPiano(AudioEngine& audioEngine, const std::vector<float>& pcmData, uint32_t sampleRate) {
    if (pcmData.empty()) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float totalW = ImGui::GetContentRegionAvail().x;

    ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.95f, 1.0f), "Key Testing:");
    ImGui::SameLine();
    ImGui::Text("Oct: %d", m_testOctave);
    ImGui::SameLine();
    if (ImGui::SmallButton("<##PcmOctDown") && m_testOctave > 1) m_testOctave--;
    ImGui::SameLine();
    if (ImGui::SmallButton(">##PcmOctUp") && m_testOctave < 6) m_testOctave++;
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::SetNextItemWidth(95.0f);
    ImGui::SliderFloat("##PcmVol", &m_testVolume, 0.1f, 1.2f, "Vol: %.2f");
    ImGui::SameLine();
    ImGui::TextDisabled("| Keys: [A-S-D-F white, W-E-T-Y black | Oct: Z/X]");

    const int numWhiteKeys = 14;
    float whiteKeyW = std::clamp((totalW - 20.0f) / (float)numWhiteKeys, 20.0f, 38.0f);
    float availH = ImGui::GetContentRegionAvail().y;
    float whiteKeyH = (availH < 75.0f) ? 46.0f : 58.0f;
    float blackKeyW = whiteKeyW * 0.62f;
    float blackKeyH = whiteKeyH * 0.62f;

    ImVec2 kbStart = ImGui::GetCursorScreenPos();
    kbStart.x += 4.0f;
    kbStart.y += 2.0f;

    static const int whiteKeySemitones[14] = { 0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23 };
    static const char* whiteKeyLabels[14] = { "C", "D", "E", "F", "G", "A", "B", "C", "D", "E", "F", "G", "A", "B" };
    static const int blackKeyAnchors[10] = { 0, 1, 3, 4, 5, 7, 8, 10, 11, 12 };
    static const int blackKeySemitones[10] = { 1, 3, 6, 8, 10, 13, 15, 18, 20, 22 };
    static const char* blackKeyLabels[10] = { "C#", "D#", "F#", "G#", "A#", "C#", "D#", "F#", "G#", "A#" };

    int hoveredKey = -1;
    int baseMidi = (m_testOctave + 1) * 12;

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool mouseDown = ImGui::GetIO().MouseDown[0];

    // Check black keys hover
    for (int b = 0; b < 10; ++b) {
        int wAnchor = blackKeyAnchors[b];
        float bkX = kbStart.x + (wAnchor + 1) * whiteKeyW - blackKeyW * 0.5f;
        ImVec2 bk0(bkX, kbStart.y);
        ImVec2 bk1(bkX + blackKeyW, kbStart.y + blackKeyH);
        if (mousePos.x >= bk0.x && mousePos.x <= bk1.x && mousePos.y >= bk0.y && mousePos.y <= bk1.y) {
            hoveredKey = blackKeySemitones[b];
            break;
        }
    }

    // Check white keys hover
    if (hoveredKey == -1) {
        for (int w = 0; w < 14; ++w) {
            float wkX = kbStart.x + w * whiteKeyW;
            ImVec2 wk0(wkX, kbStart.y);
            ImVec2 wk1(wkX + whiteKeyW, kbStart.y + whiteKeyH);
            if (mousePos.x >= wk0.x && mousePos.x <= wk1.x && mousePos.y >= wk0.y && mousePos.y <= wk1.y) {
                hoveredKey = whiteKeySemitones[w];
                break;
            }
        }
    }

    // Computer keyboard keys (A-S-D-F white notes, W-E-T-Y-U-O-P black notes)
    int kbKey = -1;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        // Natural white notes (Do-Re-Mi-Fa...)
        if (ImGui::IsKeyDown(ImGuiKey_A))           kbKey = 0;   // C
        else if (ImGui::IsKeyDown(ImGuiKey_W))      kbKey = 1;   // C#
        else if (ImGui::IsKeyDown(ImGuiKey_S))      kbKey = 2;   // D
        else if (ImGui::IsKeyDown(ImGuiKey_E))      kbKey = 3;   // D#
        else if (ImGui::IsKeyDown(ImGuiKey_D))      kbKey = 4;   // E
        else if (ImGui::IsKeyDown(ImGuiKey_F))      kbKey = 5;   // F
        else if (ImGui::IsKeyDown(ImGuiKey_T))      kbKey = 6;   // F#
        else if (ImGui::IsKeyDown(ImGuiKey_G))      kbKey = 7;   // G
        else if (ImGui::IsKeyDown(ImGuiKey_Y))      kbKey = 8;   // G#
        else if (ImGui::IsKeyDown(ImGuiKey_H))      kbKey = 9;   // A
        else if (ImGui::IsKeyDown(ImGuiKey_U))      kbKey = 10;  // A#
        else if (ImGui::IsKeyDown(ImGuiKey_J))      kbKey = 11;  // B
        else if (ImGui::IsKeyDown(ImGuiKey_K))      kbKey = 12;  // C+1
        else if (ImGui::IsKeyDown(ImGuiKey_O))      kbKey = 13;  // C#+1
        else if (ImGui::IsKeyDown(ImGuiKey_L))      kbKey = 14;  // D+1
        else if (ImGui::IsKeyDown(ImGuiKey_P))      kbKey = 15;  // D#+1
        else if (ImGui::IsKeyDown(ImGuiKey_Semicolon))  kbKey = 16;  // E+1
        else if (ImGui::IsKeyDown(ImGuiKey_Apostrophe)) kbKey = 17;  // F+1
        else if (ImGui::IsKeyDown(ImGuiKey_RightBracket)) kbKey = 18; // F#+1

        // Octave shifts
        if (ImGui::IsKeyPressed(ImGuiKey_Z) && m_testOctave > 1) m_testOctave--;
        if (ImGui::IsKeyPressed(ImGuiKey_X) && m_testOctave < 6) m_testOctave++;
    }

    int activeKey = -1;
    if (mouseDown && hoveredKey != -1) {
        activeKey = hoveredKey;
    } else if (kbKey != -1) {
        activeKey = kbKey;
    }

    if (activeKey != -1) {
        if (!m_mouseTesting || m_activeTestKey != activeKey) {
            m_mouseTesting = true;
            m_activeTestKey = activeKey;
            int testMidi = baseMidi + activeKey;
            m_lastTestedMidi = testMidi;
            TriggerAuditionPcm(audioEngine, pcmData, sampleRate, testMidi);
        }
    } else {
        if (m_mouseTesting) {
            m_mouseTesting = false;
            m_activeTestKey = -1;
        }
    }

    // Render White Keys
    for (int w = 0; w < 14; ++w) {
        float wkX = kbStart.x + w * whiteKeyW;
        ImVec2 wk0(wkX, kbStart.y);
        ImVec2 wk1(wkX + whiteKeyW - 1.0f, kbStart.y + whiteKeyH);
        int semi = whiteKeySemitones[w];
        bool isKeyActive = (activeKey == semi);

        ImU32 col = isKeyActive ? IM_COL32(0, 210, 240, 255) : IM_COL32(235, 240, 245, 255);
        dl->AddRectFilled(wk0, wk1, col, 2.0f);
        dl->AddRect(wk0, wk1, IM_COL32(30, 40, 50, 255), 2.0f);

        int oct = m_testOctave + (w >= 7 ? 1 : 0);
        char noteLabel[8];
        std::snprintf(noteLabel, sizeof(noteLabel), "%s%d", whiteKeyLabels[w], oct);
        ImVec2 labelSz = ImGui::CalcTextSize(noteLabel);
        ImVec2 labelPos(wkX + (whiteKeyW - labelSz.x) * 0.5f, kbStart.y + whiteKeyH - labelSz.y - 4.0f);
        dl->AddText(labelPos, isKeyActive ? IM_COL32(10, 10, 10, 255) : IM_COL32(70, 75, 85, 255), noteLabel);
    }

    // Render Black Keys
    for (int b = 0; b < 10; ++b) {
        int wAnchor = blackKeyAnchors[b];
        float bkX = kbStart.x + (wAnchor + 1) * whiteKeyW - blackKeyW * 0.5f;
        ImVec2 bk0(bkX, kbStart.y);
        ImVec2 bk1(bkX + blackKeyW, kbStart.y + blackKeyH);
        int semi = blackKeySemitones[b];
        bool isKeyActive = (activeKey == semi);

        ImU32 col = isKeyActive ? IM_COL32(0, 220, 255, 255) : IM_COL32(28, 32, 42, 255);
        dl->AddRectFilled(bk0, bk1, col, 2.0f);
        dl->AddRect(bk0, bk1, isKeyActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(15, 20, 25, 255), 2.0f);

        char bkLabel[8];
        std::snprintf(bkLabel, sizeof(bkLabel), "%s", blackKeyLabels[b]);
        ImVec2 labelSz = ImGui::CalcTextSize(bkLabel);
        ImVec2 labelPos(bkX + (blackKeyW - labelSz.x) * 0.5f, kbStart.y + blackKeyH - labelSz.y - 3.0f);
        dl->AddText(labelPos, isKeyActive ? IM_COL32(10, 10, 10, 255) : IM_COL32(160, 175, 195, 255), bkLabel);
    }

    ImGui::SetCursorScreenPos(kbStart);
    ImGui::InvisibleButton("##AuditionKeyboardHitbox", ImVec2(whiteKeyW * 14.0f, whiteKeyH));

    ImGui::Spacing();
    ImGui::TextDisabled("Audition: Click/drag keys above, or use computer keyboard [White: A-S-D-F-G-H-J-K-L | Black: W-E-T-Y-U-O-P | Oct: Z/X]");
}

bool SampleViewer::SaveWavFile(const std::string& outPath, const std::vector<float>& pcm, uint32_t sampleRate) {
    if (pcm.empty()) return false;

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) return false;

    uint32_t numSamples = (uint32_t)pcm.size();
    uint32_t dataSize = numSamples * 2; // 16-bit mono = 2 bytes per sample
    uint32_t fileSize = 36 + dataSize;
    uint32_t byteRate = sampleRate * 2;
    uint16_t blockAlign = 2;
    uint16_t bitsPerSample = 16;
    uint16_t numChannels = 1;
    uint16_t audioFormat = 1; // PCM
    uint32_t fmtSize = 16;

    // RIFF header
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&fileSize), 4);
    out.write("WAVE", 4);

    // fmt chunk
    out.write("fmt ", 4);
    out.write(reinterpret_cast<const char*>(&fmtSize), 4);
    out.write(reinterpret_cast<const char*>(&audioFormat), 2);
    out.write(reinterpret_cast<const char*>(&numChannels), 2);
    out.write(reinterpret_cast<const char*>(&sampleRate), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4);
    out.write(reinterpret_cast<const char*>(&blockAlign), 2);
    out.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    // data chunk
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataSize), 4);

    for (float s : pcm) {
        float clamped = std::clamp(s, -1.0f, 1.0f);
        int16_t sample16 = (int16_t)(clamped * 32767.0f);
        out.write(reinterpret_cast<const char*>(&sample16), 2);
    }

    out.close();
    return true;
}

void SampleViewer::Render(AudioEngine& audioEngine, bool* pOpen) {
    if (pOpen && !*pOpen) return;

    RescanSamples(audioEngine);

    static bool s_modeChecked = false;
    if (!s_modeChecked) {
        s_modeChecked = true;
        if (const char* vm = std::getenv("VGMSTUDIO_SAMPLE_VIEW_MODE")) {
            m_viewMode = std::atoi(vm);
            if (m_viewMode == 1 && !m_samples.empty() && m_selectedIndex < 0) {
                m_selectedIndex = 0;
            }
        }
    }

    static int focusCountdown = 0;
    if (m_focusRequested) {
        focusCountdown = 6;
        m_focusRequested = false;
    }
    if (focusCountdown > 0) {
        ImGui::SetNextWindowFocus();
        focusCountdown--;
    }

    ImGui::SetNextWindowSize(ImVec2(880, 480), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Sample Viewer", pOpen)) {
        ImGui::End();
        return;
    }

    if (m_samples.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No PCM samples found in the current song.");
        ImGui::Spacing();
        ImGui::TextWrapped("This song does not contain PCM audio data blocks (e.g. RF5C164, OKIM6295, SegaPCM, MultiPCM, YM2612 PCM).");
        ImGui::End();
        return;
    }

    // Update feedback toast timer
    if (m_toastTimer > 0.0f) {
        m_toastTimer -= ImGui::GetIO().DeltaTime;
        if (m_toastTimer <= 0.0f) {
            m_toastMessage.clear();
        }
    }

    // Top status / toast banner if present
    if (!m_toastMessage.empty()) {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%s", m_toastMessage.c_str());
        ImGui::Separator();
    }

    // --- Main Layout: Left pane (List) and Right pane (Waveform / Inspector) ---
    float contentWidth = ImGui::GetContentRegionAvail().x;
    float contentHeight = ImGui::GetContentRegionAvail().y;

    float minLeft = 200.0f;
    float maxLeft = contentWidth - 280.0f;
    if (maxLeft < minLeft) maxLeft = minLeft;
    m_leftPaneWidth = std::clamp(m_leftPaneWidth, minLeft, maxLeft);

    // LEFT PANE: Mode selector, Slice List / Sample List
    ImGui::BeginChild("SampleListPane", ImVec2(m_leftPaneWidth, contentHeight), true);
    {
        // View Mode tabs
        float tabW = (m_leftPaneWidth - 24.0f) * 0.5f;
        if (tabW < 80.0f) tabW = 80.0f;

        bool isSliceTab = (m_viewMode == 0);
        bool isRawTab = (m_viewMode == 1);

        if (isSliceTab) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.50f, 0.70f, 1.0f));
        char sliceBtnText[64];
        std::snprintf(sliceBtnText, sizeof(sliceBtnText), "Song Slices (%zu)", m_slices.size());
        if (ImGui::Button(sliceBtnText, ImVec2(tabW, 0.0f))) {
            if (m_viewMode != 0) {
                m_viewMode = 0;
                audioEngine.StopPreviewSample();
                m_cursorNorm = 0.0f;
                m_wasPlaying = false;
                if (!m_slices.empty() && m_selectedSliceIndex >= 0) {
                    m_playbackSampleRate = m_slices[m_selectedSliceIndex].defaultSampleRate;
                }
            }
        }
        if (isSliceTab) ImGui::PopStyleColor();

        ImGui::SameLine();

        if (isRawTab) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.50f, 0.70f, 1.0f));
        char rawBtnText[64];
        std::snprintf(rawBtnText, sizeof(rawBtnText), "Raw Banks (%zu)", m_samples.size());
        if (ImGui::Button(rawBtnText, ImVec2(tabW, 0.0f))) {
            if (m_viewMode != 1) {
                m_viewMode = 1;
                audioEngine.StopPreviewSample();
                m_cursorNorm = 0.0f;
                m_wasPlaying = false;
                if (!m_samples.empty() && m_selectedIndex >= 0) {
                    m_playbackSampleRate = m_samples[m_selectedIndex].defaultSampleRate;
                }
            }
        }
        if (isRawTab) ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (m_viewMode == 0) {
            // --- Song PCM Slices Table ---
            if (m_slices.empty()) {
                ImGui::TextDisabled("No slice seek commands found.");
                ImGui::TextWrapped("The current song plays entire sample banks without discrete seek/cut commands.");
            } else {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint("##SliceSearch", "Search cuts, time, chips...", m_sliceSearchFilter, sizeof(m_sliceSearchFilter));

                static const char* sortNames[] = { "Timeline (Time)", "Most Used", "Cut Offset" };
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::Combo("##SliceSort", &m_sliceSortMode, sortNames, 3);

                ImGui::Separator();

                std::vector<int> sortedIndices;
                for (int i = 0; i < (int)m_slices.size(); ++i) {
                    const auto& sl = m_slices[i];
                    if (m_sliceSearchFilter[0] != '\0') {
                        std::string q = m_sliceSearchFilter;
                        std::transform(q.begin(), q.end(), q.begin(), ::tolower);
                        std::string sText = sl.chip + " " + sl.channelName + " " + sl.songTimeStr + " " + std::to_string(sl.id);
                        std::transform(sText.begin(), sText.end(), sText.begin(), ::tolower);
                        if (sText.find(q) == std::string::npos) continue;
                    }
                    sortedIndices.push_back(i);
                }

                std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
                    const auto& sa = m_slices[a];
                    const auto& sb = m_slices[b];
                    if (m_sliceSortMode == 1) {
                        if (sa.triggerCount != sb.triggerCount) return sa.triggerCount > sb.triggerCount;
                        return sa.firstSamplePos < sb.firstSamplePos;
                    } else if (m_sliceSortMode == 2) {
                        if (sa.sliceOffset != sb.sliceOffset) return sa.sliceOffset < sb.sliceOffset;
                        return sa.firstSamplePos < sb.firstSamplePos;
                    } else {
                        return sa.firstSamplePos < sb.firstSamplePos;
                    }
                });

                ImGuiTableFlags tblFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | 
                                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
                if (ImGui::BeginTable("SliceTable", 4, tblFlags, ImVec2(0.0f, 0.0f))) {
                    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26.0f);
                    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 62.0f);
                    ImGui::TableSetupColumn("Cut Range", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Uses", ImGuiTableColumnFlags_WidthFixed, 42.0f);
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableHeadersRow();

                    for (int idx : sortedIndices) {
                        const auto& item = m_slices[idx];
                        ImGui::TableNextRow();
                        bool isSelected = (idx == m_selectedSliceIndex);

                        ImGui::TableSetColumnIndex(0);
                        char idLabel[32];
                        std::snprintf(idLabel, sizeof(idLabel), "%02d", item.id);
                        if (ImGui::Selectable(idLabel, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                            if (m_selectedSliceIndex != idx) {
                                audioEngine.StopPreviewSample();
                                m_selectedSliceIndex = idx;
                                m_playbackSampleRate = item.defaultSampleRate;
                                m_cursorNorm = 0.0f;
                                m_wasPlaying = false;
                            }
                        }

                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Cut #%d | %s (%s)\nFirst Trigger: %s (Sample %u)\nCut Range: 0x%04X..0x%04X (%u bytes)\nDuration: %.3f s | Triggers: %d times",
                                              item.id, item.chip.c_str(), item.channelName.c_str(),
                                              item.songTimeStr.c_str(), item.firstSamplePos,
                                              item.sliceOffset, item.sliceEndOffset, item.sliceLength,
                                              item.durationSec, item.triggerCount);
                        }

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.95f, 1.0f), "%s", item.songTimeStr.c_str());

                        ImGui::TableSetColumnIndex(2);
                        if (item.sliceLength >= 1024) {
                            ImGui::Text("0x%04X..0x%04X (%.1fK)", item.sliceOffset, item.sliceEndOffset, (float)item.sliceLength / 1024.0f);
                        } else {
                            ImGui::Text("0x%04X..0x%04X (%uB)", item.sliceOffset, item.sliceEndOffset, item.sliceLength);
                        }

                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextDisabled("%dx", item.triggerCount);
                    }
                    ImGui::EndTable();
                }
            }
        } else {
            // --- Raw Sample Banks Table ---
            if (m_chipFilterOptions.size() > 1) {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##ChipFilter", m_chipFilterOptions[m_chipFilterIdx].c_str())) {
                    for (size_t i = 0; i < m_chipFilterOptions.size(); ++i) {
                        bool isSelected = ((int)i == m_chipFilterIdx);
                        if (ImGui::Selectable(m_chipFilterOptions[i].c_str(), isSelected)) {
                            if (m_chipFilterIdx != (int)i) {
                                audioEngine.StopPreviewSample();
                                m_chipFilterIdx = (int)i;
                                m_cursorNorm = 0.0f;
                                m_wasPlaying = false;
                            }
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            ImGui::Separator();

            ImGuiTableFlags tblFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | 
                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
            if (ImGui::BeginTable("SampleTable", 4, tblFlags, ImVec2(0.0f, 0.0f))) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                ImGui::TableSetupColumn("Chip", ImGuiTableColumnFlags_WidthFixed, 75.0f);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Format / Note", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < m_samples.size(); ++i) {
                    const auto& item = m_samples[i];
                    if (m_chipFilterIdx > 0 && m_chipFilterIdx < (int)m_chipFilterOptions.size()) {
                        if (item.chip != m_chipFilterOptions[m_chipFilterIdx]) continue;
                    }

                    ImGui::TableNextRow();
                    bool isSelected = ((int)i == m_selectedIndex);

                    ImGui::TableSetColumnIndex(0);
                    char idLabel[32];
                    std::snprintf(idLabel, sizeof(idLabel), "%d", item.id);
                    if (ImGui::Selectable(idLabel, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                        if (m_selectedIndex != (int)i) {
                            audioEngine.StopPreviewSample();
                            m_selectedIndex = (int)i;
                            m_playbackSampleRate = item.defaultSampleRate;
                            m_cursorNorm = 0.0f;
                            m_wasPlaying = false;
                        }
                    }

                    ImGui::TableSetColumnIndex(1);
                    if (item.chip.find("RF5C") != std::string::npos) {
                        ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "%s", item.chip.c_str());
                    } else if (item.chip.find("OKI") != std::string::npos) {
                        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "%s", item.chip.c_str());
                    } else if (item.chip.find("SegaPCM") != std::string::npos) {
                        ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.0f), "%s", item.chip.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(0.85f, 0.55f, 0.95f, 1.0f), "%s", item.chip.c_str());
                    }

                    ImGui::TableSetColumnIndex(2);
                    if (item.rawSize >= 1024) {
                        ImGui::TextDisabled("%.1f KB", (float)item.rawSize / 1024.0f);
                    } else {
                        ImGui::TextDisabled("%u B", item.rawSize);
                    }

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%s", item.notes.empty() ? item.formatName.c_str() : item.notes.c_str());
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::EndChild();

    // Draggable Splitter between Left and Right Panes
    float splitterWidth = 6.0f;
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::InvisibleButton("##SampleViewerSplitter", ImVec2(splitterWidth, contentHeight));
    bool isSplitterHovered = ImGui::IsItemHovered();
    bool isSplitterActive = ImGui::IsItemActive();
    if (isSplitterHovered || isSplitterActive) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (isSplitterActive) {
        m_leftPaneWidth += ImGui::GetIO().MouseDelta.x;
        m_leftPaneWidth = std::clamp(m_leftPaneWidth, minLeft, maxLeft);
    }

    ImVec2 sMin = ImGui::GetItemRectMin();
    ImVec2 sMax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float sMidX = (sMin.x + sMax.x) * 0.5f;
    ImU32 splitterCol = isSplitterActive ? IM_COL32(0, 210, 225, 255)
                      : (isSplitterHovered ? IM_COL32(0, 180, 200, 180) : IM_COL32(35, 42, 54, 200));
    dl->AddLine(ImVec2(sMidX, sMin.y), ImVec2(sMidX, sMax.y), splitterCol, isSplitterActive ? 2.0f : 1.5f);

    ImGui::SameLine(0.0f, 2.0f);

    // RIGHT PANE: Waveform & Audition Controls
    ImGui::BeginChild("SampleInspectorPane", ImVec2(0.0f, contentHeight), true);
    {
        bool isPlaying = audioEngine.IsPreviewSamplePlaying();
        bool isPaused = audioEngine.IsPreviewSamplePaused();

        if (isPlaying) {
            m_cursorNorm = audioEngine.GetPreviewSampleProgress();
            m_wasPlaying = true;
        } else if (!isPaused) {
            if (m_wasPlaying) {
                m_cursorNorm = 0.0f;
                m_wasPlaying = false;
            }
        }

        if (m_viewMode == 0 && m_selectedSliceIndex >= 0 && m_selectedSliceIndex < (int)m_slices.size()) {
            auto& selSlice = m_slices[m_selectedSliceIndex];
            const PcmSampleItem* parentSample = (selSlice.parentSampleIndex >= 0 && selSlice.parentSampleIndex < (int)m_samples.size())
                                                ? &m_samples[selSlice.parentSampleIndex] : nullptr;

            // Header details
            ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "%s - Cut #%d", selSlice.chip.c_str(), selSlice.id);
            ImGui::SameLine();
            ImGui::TextDisabled("|  Channel: %s  |  First at: %s (Sample %u)",
                                selSlice.channelName.c_str(), selSlice.songTimeStr.c_str(), selSlice.firstSamplePos);

            ImGui::Spacing();

            // Stats row
            char statsBuf[256];
            std::snprintf(statsBuf, sizeof(statsBuf),
                          "Cut Range: 0x%04X..0x%04X (%u bytes)  |  Duration: %.3f s  |  Triggers: %d times  |  Peak: %.1f dB",
                          selSlice.sliceOffset, selSlice.sliceEndOffset, selSlice.sliceLength,
                          selSlice.durationSec, selSlice.triggerCount, selSlice.peakDb);
            ImGui::TextColored(ImVec4(0.75f, 0.85f, 0.95f, 1.0f), "%s", statsBuf);

            ImGui::Separator();
            ImGui::Spacing();

            // Toolbar Controls
            ImU32 playActiveColor = isPlaying ? IM_COL32(0, 155, 170, 255) : 0;
            bool playPausePressed = false;
            if (isPlaying) {
                playPausePressed = icons::IconButton("##SlicePlayPauseBtn", icons::DrawPause, 13.0f, ImVec2(9.0f, 4.0f), true, playActiveColor);
            } else {
                playPausePressed = icons::IconButton("##SlicePlayPauseBtn", icons::DrawPlay, 13.0f, ImVec2(9.0f, 4.0f), false, 0);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(isPlaying ? "Pause Cut" : (isPaused ? "Resume Cut" : "Play Cut"));

            if (playPausePressed) {
                if (isPlaying) {
                    audioEngine.PausePreviewSample();
                } else if (isPaused) {
                    audioEngine.ResumePreviewSample();
                } else {
                    if (m_cursorNorm >= 0.995f || m_cursorNorm < 0.0f) {
                        m_cursorNorm = 0.0f;
                    }
                    float effStep = (float)m_playbackSampleRate / 44100.0f;
                    size_t startOffset = (size_t)(m_cursorNorm * (float)selSlice.decodedPcm.size());
                    if (startOffset >= selSlice.decodedPcm.size()) {
                        startOffset = 0;
                        m_cursorNorm = 0.0f;
                    }
                    audioEngine.SetPreviewSampleVolume(m_playbackVolume);
                    audioEngine.PlayPreviewSamplePitched(selSlice.decodedPcm, effStep, startOffset);
                    m_wasPlaying = true;
                }
            }

            ImGui::SameLine();

            if (icons::IconButton("##SliceStopBtn", icons::DrawStop, 12.0f, ImVec2(8.0f, 4.0f))) {
                audioEngine.StopPreviewSample();
                m_cursorNorm = 0.0f;
                m_wasPlaying = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // Zoom toggle button
            if (ImGui::Button(m_zoomToSlice ? "Full Waveform" : "Zoom to Cut")) {
                m_zoomToSlice = !m_zoomToSlice;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle between viewing the cut in the full bank or zoomed in");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // Sample Rate combo
            ImGui::SetNextItemWidth(100.0f);
            int rateChoices[] = { 4000, 7500, 8000, 10000, 11025, 15600, 16000, 21300, 22050, 31250, 32000, 44100 };
            const char* rateNames[] = { "4,000 Hz", "7,500 Hz", "8,000 Hz", "10,000 Hz", "11,025 Hz", "15,600 Hz",
                                        "16,000 Hz", "21,300 Hz", "22,050 Hz", "31,250 Hz", "32,000 Hz", "44,100 Hz" };
            int curRateIdx = 6;
            for (int r = 0; r < 12; ++r) {
                if (rateChoices[r] == m_playbackSampleRate) {
                    curRateIdx = r;
                    break;
                }
            }
            if (ImGui::Combo("##SliceRateCombo", &curRateIdx, rateNames, 12)) {
                m_playbackSampleRate = rateChoices[curRateIdx];
                if (isPlaying) {
                    float effStep = (float)m_playbackSampleRate / 44100.0f;
                    size_t curOffset = (size_t)(audioEngine.GetPreviewSampleProgress() * (float)selSlice.decodedPcm.size());
                    audioEngine.PlayPreviewSamplePitched(selSlice.decodedPcm, effStep, curOffset);
                    m_wasPlaying = true;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback Sample Rate");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            if (DrawMasterStyleFader("##SliceVolFader", &m_playbackVolume, 0.0f, 1.0f, 95.0f, 24.0f)) {
                audioEngine.SetPreviewSampleVolume(m_playbackVolume);
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // Export Cut WAV
            if (ImGui::Button("Export Cut WAV...")) {
                std::filesystem::create_directories("samples");
                char outFn[256];
                std::snprintf(outFn, sizeof(outFn), "samples/cut_%02d_%s_%04X.wav",
                              selSlice.id, selSlice.chip.c_str(), selSlice.sliceOffset);
                if (SaveWavFile(outFn, selSlice.decodedPcm, (uint32_t)m_playbackSampleRate)) {
                    m_toastMessage = "Exported Cut WAV to: " + std::string(outFn);
                    m_toastTimer = 5.0f;
                } else {
                    m_toastMessage = "Failed to export WAV file!";
                    m_toastTimer = 5.0f;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Export this exact sliced cut as a 16-bit PCM WAV file into samples/");

            ImGui::Separator();

            // --- Waveform Canvas ---
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float canvasHeight = std::clamp(avail.y * 0.28f, 52.0f, 140.0f);
            ImVec2 canvasSize(avail.x, canvasHeight);
            ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
            ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSize.x, canvasP0.y + canvasSize.y);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(canvasP0, canvasP1, IM_COL32(18, 22, 28, 255));
            drawList->AddRect(canvasP0, canvasP1, IM_COL32(50, 60, 75, 255));

            float centerY = canvasP0.y + canvasSize.y * 0.5f;
            float halfH = canvasSize.y * 0.46f;
            drawList->AddLine(ImVec2(canvasP0.x, centerY), ImVec2(canvasP1.x, centerY), IM_COL32(65, 80, 100, 160), 1.0f);
            drawList->AddLine(ImVec2(canvasP0.x, centerY - halfH * 0.5f), ImVec2(canvasP1.x, centerY - halfH * 0.5f), IM_COL32(40, 50, 65, 100), 1.0f);
            drawList->AddLine(ImVec2(canvasP0.x, centerY + halfH * 0.5f), ImVec2(canvasP1.x, centerY + halfH * 0.5f), IM_COL32(40, 50, 65, 100), 1.0f);

            const std::vector<float>& displayPcm = (m_zoomToSlice || !parentSample) ? selSlice.decodedPcm : parentSample->decodedPcm;
            size_t totalSamples = displayPcm.size();

            // Draw waveform min/max columns
            if (totalSamples > 0 && canvasSize.x > 2.0f) {
                int numPixels = (int)canvasSize.x;
                float samplesPerPixel = (float)totalSamples / (float)numPixels;

                for (int px = 0; px < numPixels; ++px) {
                    size_t idxStart = (size_t)(px * samplesPerPixel);
                    size_t idxEnd = (size_t)((px + 1) * samplesPerPixel);
                    if (idxEnd > totalSamples) idxEnd = totalSamples;
                    if (idxStart >= idxEnd && idxStart < totalSamples) idxEnd = idxStart + 1;

                    float minVal = 1.0f, maxVal = -1.0f;
                    for (size_t s = idxStart; s < idxEnd; ++s) {
                        float v = displayPcm[s];
                        if (v < minVal) minVal = v;
                        if (v > maxVal) maxVal = v;
                    }
                    if (minVal > maxVal) { minVal = 0.0f; maxVal = 0.0f; }

                    float yTop = centerY - maxVal * halfH;
                    float yBottom = centerY - minVal * halfH;
                    if (yBottom - yTop < 1.0f) yBottom = yTop + 1.0f;

                    float posX = canvasP0.x + (float)px;
                    drawList->AddLine(ImVec2(posX, yTop), ImVec2(posX, yBottom), IM_COL32(0, 215, 255, 220), 1.0f);
                }
            }

            // Visual Markers for Cut Region (when viewing full sample)
            float normStart = 0.0f;
            float normEnd = 1.0f;
            if (!m_zoomToSlice && parentSample && totalSamples > 0) {
                normStart = (float)selSlice.sampleStartInParent / (float)totalSamples;
                normEnd = (float)selSlice.sampleEndInParent / (float)totalSamples;
                normStart = std::clamp(normStart, 0.0f, 1.0f);
                normEnd = std::clamp(normEnd, 0.0f, 1.0f);

                if (normEnd > normStart) {
                    float x0 = canvasP0.x + normStart * canvasSize.x;
                    float x1 = canvasP0.x + normEnd * canvasSize.x;
                    if (x1 - x0 < 3.0f) x1 = x0 + 3.0f;

                    // Tinted highlight overlay for slice cut region
                    drawList->AddRectFilled(ImVec2(x0, canvasP0.y), ImVec2(x1, canvasP1.y), IM_COL32(0, 210, 255, 55));
                    drawList->AddRect(ImVec2(x0, canvasP0.y), ImVec2(x1, canvasP1.y), IM_COL32(0, 225, 255, 140), 0.0f, 0, 1.2f);

                    // Start cut marker line & tag
                    drawList->AddLine(ImVec2(x0, canvasP0.y), ImVec2(x0, canvasP1.y), IM_COL32(0, 255, 200, 255), 1.5f);
                    drawList->AddTriangleFilled(ImVec2(x0 - 4.0f, canvasP0.y), ImVec2(x0 + 4.0f, canvasP0.y), ImVec2(x0, canvasP0.y + 6.0f), IM_COL32(0, 255, 200, 255));

                    // End cut marker line & tag
                    drawList->AddLine(ImVec2(x1, canvasP0.y), ImVec2(x1, canvasP1.y), IM_COL32(255, 175, 40, 255), 1.5f);
                    drawList->AddTriangleFilled(ImVec2(x1 - 4.0f, canvasP0.y), ImVec2(x1 + 4.0f, canvasP0.y), ImVec2(x1, canvasP0.y + 6.0f), IM_COL32(255, 175, 40, 255));

                    char cutTag[64];
                    std::snprintf(cutTag, sizeof(cutTag), "CUT #%d [0x%X..0x%X]", selSlice.id, selSlice.sliceOffset, selSlice.sliceEndOffset);
                    float tagX = (x0 + 4.0f + 180.0f > canvasP1.x) ? (x1 - 180.0f) : (x0 + 4.0f);
                    if (tagX < canvasP0.x + 4.0f) tagX = canvasP0.x + 4.0f;
                    drawList->AddText(ImVec2(tagX, canvasP0.y + 4.0f), IM_COL32(0, 240, 255, 240), cutTag);
                }
            }

            // Playback playhead line
            if (m_cursorNorm >= 0.0f && m_cursorNorm <= 1.0f) {
                float curProgressX = 0.0f;
                if (m_zoomToSlice || !parentSample) {
                    curProgressX = canvasP0.x + m_cursorNorm * canvasSize.x;
                } else {
                    curProgressX = canvasP0.x + (normStart + m_cursorNorm * (normEnd - normStart)) * canvasSize.x;
                }
                drawList->AddLine(ImVec2(curProgressX, canvasP0.y), ImVec2(curProgressX, canvasP1.y), IM_COL32(255, 215, 0, 255), 2.0f);
                drawList->AddTriangleFilled(
                    ImVec2(curProgressX - 4.0f, canvasP0.y),
                    ImVec2(curProgressX + 4.0f, canvasP0.y),
                    ImVec2(curProgressX, canvasP0.y + 6.0f),
                    IM_COL32(255, 215, 0, 255)
                );
            }

            // Click canvas to seek / preview cut
            ImGui::InvisibleButton("##SliceCanvasInput", canvasSize);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to play / audition cut from this position");
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    float mouseX = ImGui::GetIO().MousePos.x;
                    if (m_zoomToSlice || !parentSample) {
                        m_cursorNorm = std::clamp((mouseX - canvasP0.x) / canvasSize.x, 0.0f, 1.0f);
                    } else {
                        float clickNorm = std::clamp((mouseX - canvasP0.x) / canvasSize.x, 0.0f, 1.0f);
                        if (normEnd > normStart) {
                            m_cursorNorm = std::clamp((clickNorm - normStart) / (normEnd - normStart), 0.0f, 1.0f);
                        } else {
                            m_cursorNorm = 0.0f;
                        }
                    }
                    if (m_cursorNorm >= 0.995f) m_cursorNorm = 0.0f;

                    float effStep = (float)m_playbackSampleRate / 44100.0f;
                    size_t startOffset = (size_t)(m_cursorNorm * (float)selSlice.decodedPcm.size());
                    if (startOffset >= selSlice.decodedPcm.size()) {
                        startOffset = 0;
                        m_cursorNorm = 0.0f;
                    }
                    audioEngine.SetPreviewSampleVolume(m_playbackVolume);
                    audioEngine.PlayPreviewSamplePitched(selSlice.decodedPcm, effStep, startOffset);
                    m_wasPlaying = true;
                }
            }

            ImGui::Separator();

            // Audition Piano Keyboard for Slice
            RenderAuditionPiano(audioEngine, selSlice.decodedPcm, (uint32_t)m_playbackSampleRate);

        } else if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_samples.size()) {
            // --- Raw Sample Bank Inspector ---
            auto& selItem = m_samples[m_selectedIndex];

            ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "%s - Sample #%d", selItem.chip.c_str(), selItem.id);
            ImGui::SameLine();
            ImGui::TextDisabled("|  %s  |  Offset: 0x%06X", selItem.formatName.c_str(), selItem.fileOffset);

            ImGui::Spacing();

            char statsBuf[256];
            std::snprintf(statsBuf, sizeof(statsBuf),
                          "Size: %u bytes  |  Samples: %zu  |  Duration: %.2f s  |  Peak: %.1f dB",
                          selItem.rawSize, selItem.decodedPcm.size(), selItem.durationSec, selItem.peakDb);
            ImGui::TextColored(ImVec4(0.75f, 0.85f, 0.95f, 1.0f), "%s", statsBuf);

            ImGui::Separator();
            ImGui::Spacing();

            // Toolbar Controls
            ImU32 playActiveColor = isPlaying ? IM_COL32(0, 155, 170, 255) : 0;
            bool playPausePressed = false;
            if (isPlaying) {
                playPausePressed = icons::IconButton("##SamplePlayPauseBtn", icons::DrawPause, 13.0f, ImVec2(9.0f, 4.0f), true, playActiveColor);
            } else {
                playPausePressed = icons::IconButton("##SamplePlayPauseBtn", icons::DrawPlay, 13.0f, ImVec2(9.0f, 4.0f), false, 0);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(isPlaying ? "Pause" : (isPaused ? "Resume" : "Play"));

            if (playPausePressed) {
                if (isPlaying) {
                    audioEngine.PausePreviewSample();
                } else if (isPaused) {
                    audioEngine.ResumePreviewSample();
                } else {
                    if (m_cursorNorm >= 0.995f || m_cursorNorm < 0.0f) {
                        m_cursorNorm = 0.0f;
                    }
                    float effStep = (float)m_playbackSampleRate / 44100.0f;
                    size_t startOffset = (size_t)(m_cursorNorm * (float)selItem.decodedPcm.size());
                    if (startOffset >= selItem.decodedPcm.size()) {
                        startOffset = 0;
                        m_cursorNorm = 0.0f;
                    }
                    audioEngine.SetPreviewSampleVolume(m_playbackVolume);
                    audioEngine.PlayPreviewSamplePitched(selItem.decodedPcm, effStep, startOffset);
                    m_wasPlaying = true;
                }
            }

            ImGui::SameLine();

            if (icons::IconButton("##SampleStopBtn", icons::DrawStop, 12.0f, ImVec2(8.0f, 4.0f))) {
                audioEngine.StopPreviewSample();
                m_cursorNorm = 0.0f;
                m_wasPlaying = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            ImGui::SetNextItemWidth(100.0f);
            int rateChoices[] = { 4000, 7500, 8000, 10000, 11025, 15600, 16000, 21300, 22050, 31250, 32000, 44100 };
            const char* rateNames[] = { "4,000 Hz", "7,500 Hz", "8,000 Hz", "10,000 Hz", "11,025 Hz", "15,600 Hz",
                                        "16,000 Hz", "21,300 Hz", "22,050 Hz", "31,250 Hz", "32,000 Hz", "44,100 Hz" };
            int curRateIdx = 6;
            for (int r = 0; r < 12; ++r) {
                if (rateChoices[r] == m_playbackSampleRate) {
                    curRateIdx = r;
                    break;
                }
            }
            if (ImGui::Combo("##SampleRateCombo", &curRateIdx, rateNames, 12)) {
                m_playbackSampleRate = rateChoices[curRateIdx];
                if (isPlaying) {
                    float effStep = (float)m_playbackSampleRate / 44100.0f;
                    size_t curOffset = (size_t)(audioEngine.GetPreviewSampleProgress() * (float)selItem.decodedPcm.size());
                    audioEngine.PlayPreviewSamplePitched(selItem.decodedPcm, effStep, curOffset);
                    m_wasPlaying = true;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback Sample Rate");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            if (DrawMasterStyleFader("##SampleVolFader", &m_playbackVolume, 0.0f, 1.0f, 95.0f, 24.0f)) {
                audioEngine.SetPreviewSampleVolume(m_playbackVolume);
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            if (ImGui::Button("Export WAV...")) {
                std::filesystem::create_directories("samples");
                char outFn[256];
                std::snprintf(outFn, sizeof(outFn), "samples/sample_%d_%s.wav", selItem.id, selItem.chip.c_str());
                if (SaveWavFile(outFn, selItem.decodedPcm, (uint32_t)m_playbackSampleRate)) {
                    m_toastMessage = "Exported WAV to: " + std::string(outFn);
                    m_toastTimer = 5.0f;
                } else {
                    m_toastMessage = "Failed to export WAV file!";
                    m_toastTimer = 5.0f;
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Export this sample as a standard 16-bit PCM WAV file into samples/");

            ImGui::Separator();

            // Waveform Canvas
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float canvasHeight = std::clamp(avail.y * 0.28f, 52.0f, 140.0f);
            ImVec2 canvasSize(avail.x, canvasHeight);
            ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
            ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSize.x, canvasP0.y + canvasSize.y);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(canvasP0, canvasP1, IM_COL32(18, 22, 28, 255));
            drawList->AddRect(canvasP0, canvasP1, IM_COL32(50, 60, 75, 255));

            float centerY = canvasP0.y + canvasSize.y * 0.5f;
            float halfH = canvasSize.y * 0.46f;
            drawList->AddLine(ImVec2(canvasP0.x, centerY), ImVec2(canvasP1.x, centerY), IM_COL32(65, 80, 100, 160), 1.0f);
            drawList->AddLine(ImVec2(canvasP0.x, centerY - halfH * 0.5f), ImVec2(canvasP1.x, centerY - halfH * 0.5f), IM_COL32(40, 50, 65, 100), 1.0f);
            drawList->AddLine(ImVec2(canvasP0.x, centerY + halfH * 0.5f), ImVec2(canvasP1.x, centerY + halfH * 0.5f), IM_COL32(40, 50, 65, 100), 1.0f);

            const auto& pcm = selItem.decodedPcm;
            size_t totalSamples = pcm.size();

            if (totalSamples > 0 && canvasSize.x > 2.0f) {
                int numPixels = (int)canvasSize.x;
                float samplesPerPixel = (float)totalSamples / (float)numPixels;

                for (int px = 0; px < numPixels; ++px) {
                    size_t idxStart = (size_t)(px * samplesPerPixel);
                    size_t idxEnd = (size_t)((px + 1) * samplesPerPixel);
                    if (idxEnd > totalSamples) idxEnd = totalSamples;
                    if (idxStart >= idxEnd && idxStart < totalSamples) idxEnd = idxStart + 1;

                    float minVal = 1.0f, maxVal = -1.0f;
                    for (size_t s = idxStart; s < idxEnd; ++s) {
                        float v = pcm[s];
                        if (v < minVal) minVal = v;
                        if (v > maxVal) maxVal = v;
                    }
                    if (minVal > maxVal) { minVal = 0.0f; maxVal = 0.0f; }

                    float yTop = centerY - maxVal * halfH;
                    float yBottom = centerY - minVal * halfH;
                    if (yBottom - yTop < 1.0f) yBottom = yTop + 1.0f;

                    float posX = canvasP0.x + (float)px;
                    drawList->AddLine(ImVec2(posX, yTop), ImVec2(posX, yBottom), IM_COL32(0, 215, 255, 230), 1.0f);
                }
            }

            if (m_cursorNorm >= 0.0f && m_cursorNorm <= 1.0f) {
                float curX = canvasP0.x + m_cursorNorm * canvasSize.x;
                drawList->AddLine(ImVec2(curX, canvasP0.y), ImVec2(curX, canvasP1.y), IM_COL32(255, 215, 0, 255), 2.0f);
                drawList->AddTriangleFilled(
                    ImVec2(curX - 4.0f, canvasP0.y),
                    ImVec2(curX + 4.0f, canvasP0.y),
                    ImVec2(curX, canvasP0.y + 6.0f),
                    IM_COL32(255, 215, 0, 255)
                );
            }

            ImGui::InvisibleButton("##WaveformCanvasInput", canvasSize);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to play / audition sample from this position");
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    float mouseX = ImGui::GetIO().MousePos.x;
                    m_cursorNorm = std::clamp((mouseX - canvasP0.x) / canvasSize.x, 0.0f, 1.0f);
                    if (m_cursorNorm >= 0.995f) m_cursorNorm = 0.0f;

                    float effStep = (float)m_playbackSampleRate / 44100.0f;
                    size_t startOffset = (size_t)(m_cursorNorm * (float)selItem.decodedPcm.size());
                    if (startOffset >= selItem.decodedPcm.size()) {
                        startOffset = 0;
                        m_cursorNorm = 0.0f;
                    }
                    audioEngine.SetPreviewSampleVolume(m_playbackVolume);
                    audioEngine.PlayPreviewSamplePitched(selItem.decodedPcm, effStep, startOffset);
                    m_wasPlaying = true;
                }
            }

            ImGui::Separator();

            // Audition Piano Keyboard for Raw Sample
            RenderAuditionPiano(audioEngine, selItem.decodedPcm, (uint32_t)m_playbackSampleRate);

        } else {
            ImGui::TextDisabled("Select an item from the list on the left to inspect its waveform and audition audio.");
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace vgmstudio
