#include "sample_viewer.hpp"
#include "icons.hpp"
#include <imgui.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <algorithm>
#include <set>
#include <filesystem>

namespace vgmstudio {

static inline uint16_t ReadLE16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
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
    m_selectedIndex = -1;
    m_cursorNorm = 0.0f;
    m_wasPlaying = false;

    if (!data || dataSize < 0x40) {
        return;
    }

    ExtractSamples(data, dataSize);

    // Build unique chips filter list
    std::set<std::string> uniqueChips;
    for (const auto& item : m_samples) {
        uniqueChips.insert(item.chip);
    }
    m_chipFilterOptions.clear();
    m_chipFilterOptions.push_back("All Chips");
    for (const auto& ch : uniqueChips) {
        m_chipFilterOptions.push_back(ch);
    }
    m_chipFilterIdx = 0;

    if (!m_samples.empty()) {
        m_selectedIndex = 0;
        m_playbackSampleRate = m_samples[0].defaultSampleRate;
    }
}

void SampleViewer::ExtractSamples(const uint8_t* vgmData, size_t dataSize) {
    if (dataSize < 0x40) return;

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

    size_t filePos = vgmDataOfs;
    int nextId = 1;

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
                    item.chip = "YMW258";
                    item.formatName = "8-bit Signed PCM";
                    item.defaultSampleRate = 16000;
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

    if (m_focusRequested) {
        ImGui::SetNextWindowFocus();
        m_focusRequested = false;
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

    // LEFT PANE: Sample List
    ImGui::BeginChild("SampleListPane", ImVec2(m_leftPaneWidth, contentHeight), true);
    {
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.95f, 1.0f), "Samples: %zu", m_samples.size());
        ImGui::Spacing();

        // Chip Filter Combo
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

        // Table
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

                // Apply chip filter
                if (m_chipFilterIdx > 0 && m_chipFilterIdx < (int)m_chipFilterOptions.size()) {
                    if (item.chip != m_chipFilterOptions[m_chipFilterIdx]) continue;
                }

                ImGui::TableNextRow();
                bool isSelected = ((int)i == m_selectedIndex);

                // Col 0: ID
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

                // Col 1: Chip
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

                // Col 2: Size
                ImGui::TableSetColumnIndex(2);
                if (item.rawSize >= 1024) {
                    ImGui::TextDisabled("%.1f KB", (float)item.rawSize / 1024.0f);
                } else {
                    ImGui::TextDisabled("%u B", item.rawSize);
                }

                // Col 3: Format / Note
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", item.notes.empty() ? item.formatName.c_str() : item.notes.c_str());
            }
            ImGui::EndTable();
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

    // Visual divider line for splitter
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
        if (m_selectedIndex >= 0 && m_selectedIndex < (int)m_samples.size()) {
            auto& selItem = m_samples[m_selectedIndex];

            // Header details
            ImGui::TextColored(ImVec4(0.0f, 0.9f, 1.0f, 1.0f), "%s - Sample #%d", selItem.chip.c_str(), selItem.id);
            ImGui::SameLine();
            ImGui::TextDisabled("|  %s  |  Offset: 0x%06X", selItem.formatName.c_str(), selItem.fileOffset);

            ImGui::Spacing();

            // Stats row
            char statsBuf[256];
            std::snprintf(statsBuf, sizeof(statsBuf),
                          "Size: %u bytes  |  Samples: %zu  |  Duration: %.2f s  |  Peak: %.1f dB",
                          selItem.rawSize, selItem.decodedPcm.size(), selItem.durationSec, selItem.peakDb);
            ImGui::TextColored(ImVec4(0.75f, 0.85f, 0.95f, 1.0f), "%s", statsBuf);

            ImGui::Separator();
            ImGui::Spacing();

            // --- Toolbar Controls ---
            bool isPlaying = audioEngine.IsPreviewSamplePlaying();
            bool isPaused = audioEngine.IsPreviewSamplePaused();

            // Auto-rewind when playback finishes naturally
            if (isPlaying) {
                m_cursorNorm = audioEngine.GetPreviewSampleProgress();
                m_wasPlaying = true;
            } else if (!isPaused) {
                if (m_wasPlaying) {
                    m_cursorNorm = 0.0f;
                    m_wasPlaying = false;
                }
            }

            // Play / Pause Button (exact layout and styling as Player Control)
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

            // Stop Button (exact styling as Player Control)
            if (icons::IconButton("##SampleStopBtn", icons::DrawStop, 12.0f, ImVec2(8.0f, 4.0f))) {
                audioEngine.StopPreviewSample();
                m_cursorNorm = 0.0f;
                m_wasPlaying = false;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop");

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // Sample Rate combo
            ImGui::SetNextItemWidth(110.0f);
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

            // Volume control (Master Mixer style fader with integrated speaker icon)
            if (DrawMasterStyleFader("##SampleVolFader", &m_playbackVolume, 0.0f, 1.0f, 100.0f, 24.0f)) {
                audioEngine.SetPreviewSampleVolume(m_playbackVolume);
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            // Export WAV button
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
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Export this sample as a standard 16-bit PCM WAV file into samples/");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // --- Waveform Canvas ---
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float canvasHeight = avail.y - 30.0f;
            if (canvasHeight < 120.0f) canvasHeight = 120.0f;

            ImVec2 canvasSize(avail.x, canvasHeight);
            ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
            ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSize.x, canvasP0.y + canvasSize.y);

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // Canvas background
            drawList->AddRectFilled(canvasP0, canvasP1, IM_COL32(18, 22, 28, 255));
            drawList->AddRect(canvasP0, canvasP1, IM_COL32(50, 60, 75, 255));

            float centerY = canvasP0.y + canvasSize.y * 0.5f;
            float halfH = canvasSize.y * 0.46f;

            // Horizontal zero line and reference guides
            drawList->AddLine(ImVec2(canvasP0.x, centerY), ImVec2(canvasP1.x, centerY), IM_COL32(65, 80, 100, 160), 1.0f);
            drawList->AddLine(ImVec2(canvasP0.x, centerY - halfH * 0.5f), ImVec2(canvasP1.x, centerY - halfH * 0.5f), IM_COL32(40, 50, 65, 100), 1.0f);
            drawList->AddLine(ImVec2(canvasP0.x, centerY + halfH * 0.5f), ImVec2(canvasP1.x, centerY + halfH * 0.5f), IM_COL32(40, 50, 65, 100), 1.0f);

            // Draw waveform min/max columns
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

                    float minVal = 1.0f;
                    float maxVal = -1.0f;

                    for (size_t s = idxStart; s < idxEnd; ++s) {
                        float v = pcm[s];
                        if (v < minVal) minVal = v;
                        if (v > maxVal) maxVal = v;
                    }

                    if (minVal > maxVal) {
                        minVal = 0.0f;
                        maxVal = 0.0f;
                    }

                    float yTop = centerY - maxVal * halfH;
                    float yBottom = centerY - minVal * halfH;
                    if (yBottom - yTop < 1.0f) {
                        yBottom = yTop + 1.0f;
                    }

                    float posX = canvasP0.x + (float)px;
                    // Glowing cyan waveform line
                    drawList->AddLine(ImVec2(posX, yTop), ImVec2(posX, yBottom), IM_COL32(0, 215, 255, 230), 1.0f);
                }
            }

            // Playback playhead line
            if (m_cursorNorm >= 0.0f && m_cursorNorm <= 1.0f) {
                float curX = canvasP0.x + m_cursorNorm * canvasSize.x;
                // Bright yellow cursor line
                drawList->AddLine(ImVec2(curX, canvasP0.y), ImVec2(curX, canvasP1.y), IM_COL32(255, 215, 0, 255), 2.0f);
                // Top cursor indicator triangle
                drawList->AddTriangleFilled(
                    ImVec2(curX - 5.0f, canvasP0.y),
                    ImVec2(curX + 5.0f, canvasP0.y),
                    ImVec2(curX, canvasP0.y + 7.0f),
                    IM_COL32(255, 215, 0, 255)
                );
            }

            // Click canvas to seek / preview from click position
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
        } else {
            ImGui::TextDisabled("Select a sample from the list on the left to inspect its waveform and audition audio.");
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace vgmstudio
