#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <algorithm>

namespace vgmstudio {

enum class PresetType {
    FM_4OP = 0,    // YM2612, YM2151, YM2203 FM, YMF262 4-Op
    FM_2OP,        // YM2413, YM3812, OPL2/OPL3 2-Op
    SQUARE,        // NES APU Square 1/2, Game Boy Pulse, PSG
    TRIANGLE,      // NES APU Triangle
    NOISE,         // NES APU Noise, Game Boy Noise, SN76496 Noise
    WAVETABLE      // HuC6280, Game Boy Wave (Ch 3), SCC1
};

struct FmOperatorPreset {
    int mult = 1;      // 0..15 (0 = 0.5x, 1 = 1x, etc.)
    int tl = 0;        // Total Level (0..127)
    int ar = 31;       // Attack Rate (0..31)
    int dr = 0;        // Decay 1 Rate / D1R (0..31)
    int sr = 0;        // Decay 2 Rate / D2R / Sustain (0..31)
    int sl = 0;        // Decay Level / DL / Sustain Level (0..15)
    int rr = 15;       // Release Rate (0..15)
    int dt = 0;        // Detune (-3..+3)
    int rs = 0;        // Rate Scale / Key Scale (0..3)
    int ssg = 0;       // SSG-EG envelope mode (0..15)
    bool am = false;   // Amplitude Modulation enable
};

struct FmPresetData {
    int algorithm = 0; // 0..7
    int feedback = 0;  // 0..7
    int pms = 0;       // Phase Modulation Sensitivity (0..7)
    int ams = 0;       // Amplitude Modulation Sensitivity (0..3)
    FmOperatorPreset ops[4];
};

struct SquarePresetData {
    float dutyCycle = 0.5f; // 0.125, 0.25, 0.5, 0.75
    int dutyIndex = 2;      // 0..3
    int volume = 15;        // 0..15
    bool envLoop = false;
    bool constantVol = true;
    int envPeriod = 0;
    bool sweepEnabled = false;
    int sweepPeriod = 0;
    bool sweepNegate = false;
    int sweepShift = 0;
};

struct TrianglePresetData {
    int linearCounter = 127; // 0..127
    bool lengthHalt = false;
};

struct NoisePresetData {
    int mode = 0;           // 0 = Long LFSR / White, 1 = Short LFSR / Metallic
    int volume = 15;        // 0..15
    int period = 0;         // 0..15
    bool constantVol = true;
};

struct WavetablePresetData {
    std::vector<float> samples; // 32 normalized samples -1.0 .. +1.0
    int bitDepth = 5;           // 4-bit (GB), 5-bit (PCE), 8-bit (SCC)
    int volume = 15;
};

struct VgmPreset {
    int id = 0;
    std::string name;          // e.g. "YM2612 #01 (Slap Bass)", "Square 50% Pulse"
    std::string chipName;      // "YM2612", "YM2151", "NES APU", "HuC6280", etc.
    std::string channelName;   // First channel where it appeared (e.g. "Ch 2")
    int channelIdx = 0;        // Numeric channel index
    std::vector<std::string> usedChannels;       // All channels that used this preset, e.g. ["Ch 2", "Ch 3", "Ch 4", "Ch 5"]
    std::map<std::string, int> channelUseCounts; // Per-channel trigger count, e.g. {"Ch 2": 101, "Ch 3": 101}
    PresetType type = PresetType::FM_4OP;
    uint32_t firstSamplePos = 0;
    std::string timestampStr;  // "00:01.42"
    int useCount = 1;          // Total triggers in song

    // Preset payload
    FmPresetData fm;
    SquarePresetData square;
    TrianglePresetData triangle;
    NoisePresetData noise;
    WavetablePresetData wavetable;

    // Raw captured hardware register dump from VGM instructions
    std::map<std::string, uint8_t> rawRegisters;

    void AddChannelUse(const std::string& ch) {
        if (std::find(usedChannels.begin(), usedChannels.end(), ch) == usedChannels.end()) {
            usedChannels.push_back(ch);
        }
        channelUseCounts[ch]++;
    }

    bool IsUsedByChannel(const std::string& ch) const {
        for (const auto& c : usedChannels) {
            if (c == ch) return true;
        }
        return false;
    }

    std::string GetChannelsString() const {
        if (usedChannels.empty()) return channelName;
        std::string s;
        for (size_t i = 0; i < usedChannels.size(); ++i) {
            if (i > 0) s += ", ";
            s += usedChannels[i];
        }
        return s;
    }

    std::string GetChannelsDetailString() const {
        if (usedChannels.empty()) return channelName;
        std::string s;
        for (size_t i = 0; i < usedChannels.size(); ++i) {
            if (i > 0) s += ", ";
            s += usedChannels[i];
            auto it = channelUseCounts.find(usedChannels[i]);
            if (it != channelUseCounts.end()) {
                s += " (" + std::to_string(it->second) + "x)";
            }
        }
        return s;
    }

    std::string GetCompactChannelsString() const {
        if (usedChannels.empty()) return channelName;
        if (usedChannels.size() == 1) return usedChannels[0];
        std::string prefix = "";
        bool allPrefix = true;
        if (usedChannels[0].rfind("Ch ", 0) == 0) prefix = "Ch ";
        else if (usedChannels[0].rfind("FM ", 0) == 0) prefix = "FM ";
        else if (usedChannels[0].rfind("Square ", 0) == 0) prefix = "Sq ";

        if (!prefix.empty()) {
            for (const auto& c : usedChannels) {
                if (c.rfind(prefix == "Sq " ? "Square " : prefix, 0) != 0) {
                    allPrefix = false;
                    break;
                }
            }
        } else {
            allPrefix = false;
        }

        if (allPrefix) {
            std::string res = prefix;
            for (size_t i = 0; i < usedChannels.size(); ++i) {
                if (i > 0) res += ",";
                std::string num = usedChannels[i].substr(prefix == "Sq " ? 7 : prefix.size());
                res += num;
            }
            return res;
        }

        std::string res;
        for (size_t i = 0; i < usedChannels.size(); ++i) {
            if (i > 0) res += ",";
            res += usedChannels[i];
        }
        return res;
    }
};

class VgmPatchExtractor {
public:
    VgmPatchExtractor();
    ~VgmPatchExtractor();

    void Clear();
    bool ExtractFromVgm(const uint8_t* vgmData, size_t vgmSize);

    const std::vector<VgmPreset>& GetPresets() const { return m_presets; }
    size_t GetPresetCount() const { return m_presets.size(); }
    bool IsEmpty() const { return m_presets.empty(); }
    const std::vector<std::string>& GetAllChannels() const { return m_allChannels; }

    // Export helpers
    static bool ExportTFI(const VgmPreset& preset, const std::string& path);
    static bool ExportVGI(const VgmPreset& preset, const std::string& path);
    static std::string FormatPresetText(const VgmPreset& preset);

private:
    std::vector<VgmPreset> m_presets;
    std::vector<std::string> m_allChannels;

    void RecordChannel(const std::string& ch);

    bool CompareFm(const FmPresetData& a, const FmPresetData& b);
    bool CompareSquare(const SquarePresetData& a, const SquarePresetData& b);
    bool CompareTriangle(const TrianglePresetData& a, const TrianglePresetData& b);
    bool CompareNoise(const NoisePresetData& a, const NoisePresetData& b);
    bool CompareWavetable(const WavetablePresetData& a, const WavetablePresetData& b);

    static bool IsFmCarrier(int algorithm, int opIdx);
};

} // namespace vgmstudio
