#pragma once

// VGM patch snapshot extractor for YM2612, YM2203 and YM2151.
// Feed it decoded chip register writes from your existing VGM command parser.
// No VGM file parser, JSON library, or chip emulator is required.
//
// Example:
//   VgmPatchExtractor extractor;
//   // whenever the VGM parser decodes a chip write:
//   extractor.onWrite(VgmChip::YM2612, port, address, value, currentSample);
//   // after parsing:
//   std::cout << extractor.toJson() << '\n';
//
// `currentSample` is the absolute VGM playback sample position (44,100 Hz timebase).
// YM2612: port 0/1, commands 0x52/0x53.
// YM2203: port 0, command 0x55.
// YM2151: port 0, command 0x54.
// Do not feed SegaPCM writes to this class.

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

enum class VgmChip {
    YM2612,
    YM2203,
    YM2151
};

struct VgmPatchSnapshot {
    VgmChip chip{};
    int channel = 0;                    // zero-based channel number
    std::uint64_t firstSample = 0;
    std::uint64_t lastSample = 0;
    std::uint64_t keyOnCount = 0;
    std::uint8_t lastKeyOnMask = 0;     // normalized operator mask: bits 0..3 for all supported chips
    std::map<std::uint16_t, std::uint8_t> registers; // encoded register key -> value
};

class VgmPatchExtractor {
public:
    // Call once for every decoded chip register write.
    // port is 0 or 1 for YM2612; ignored for YM2203 and YM2151.
    void onWrite(VgmChip chip,
                 int port,
                 std::uint8_t address,
                 std::uint8_t value,
                 std::uint64_t absoluteSample) {
        port = (chip == VgmChip::YM2612 && port != 0) ? 1 : 0;
        const std::uint16_t key = encodeKey(chip, port, address);
        state_[chipIndex(chip)][key] = value;

        int channel = -1;
        std::uint8_t keyMask = 0;

        if ((chip == VgmChip::YM2612 || chip == VgmChip::YM2203) &&
            address == 0x28 && port == 0) {
            // OPN/OPN2 key-on: bits 0..2 select channel, bits 4..7 select operators.
            const int localChannel = value & 0x03;
            if (localChannel != 3) {
                channel = localChannel + ((value & 0x04) ? 3 : 0);
                if (chip == VgmChip::YM2203 && channel >= 3)
                    channel = -1;
                keyMask = static_cast<std::uint8_t>((value >> 4) & 0x0F);
            }
        } else if (chip == VgmChip::YM2151 && address == 0x08) {
            channel = value & 0x07;
            keyMask = static_cast<std::uint8_t>((value >> 3) & 0x0F);
        }

        // A zero operator mask is a key-off, not a patch capture.
        if (channel >= 0 && keyMask != 0)
            capture(chip, channel, keyMask, absoluteSample);
    }

    const std::vector<VgmPatchSnapshot>& patches() const {
        return patches_;
    }

    void clear() {
        for (auto& chipState : state_)
            chipState.clear();
        patches_.clear();
        lookup_.clear();
    }

    // JSON is deliberately numeric-only, so no string escaping/library is needed.
    // Register keys are encoded as (port << 8) | address.
    std::string toJson() const {
        std::ostringstream out;
        out << "{\n  \"format\": \"vgm-patch-snapshots-v1\",\n  \"patches\": [";
        for (std::size_t i = 0; i < patches_.size(); ++i) {
            const auto& p = patches_[i];
            out << (i ? ",\n" : "\n")
                << "    {\"chip\":\"" << chipName(p.chip)
                << "\",\"channel\":" << p.channel
                << ",\"first_sample\":" << p.firstSample
                << ",\"last_sample\":" << p.lastSample
                << ",\"key_on_count\":" << p.keyOnCount
                << ",\"last_key_on_mask\":" << static_cast<unsigned>(p.lastKeyOnMask)
                << ",\"registers\":{";
            bool first = true;
            for (const auto& [key, value] : p.registers) {
                if (!first) out << ',';
                first = false;
                out << '"' << hexKey(key) << "\":" << static_cast<unsigned>(value);
            }
            out << "}}";
        }
        if (!patches_.empty()) out << '\n';
        out << "  ]\n}\n";
        return out.str();
    }

private:
    // Three independent register-state maps, indexed by VgmChip.
    std::map<std::uint16_t, std::uint8_t> state_[3];
    std::vector<VgmPatchSnapshot> patches_;
    std::map<std::string, std::size_t> lookup_;

    static int chipIndex(VgmChip chip) {
        switch (chip) {
            case VgmChip::YM2612: return 0;
            case VgmChip::YM2203: return 1;
            case VgmChip::YM2151: return 2;
        }
        return 0;
    }

    static const char* chipName(VgmChip chip) {
        switch (chip) {
            case VgmChip::YM2612: return "YM2612";
            case VgmChip::YM2203: return "YM2203";
            case VgmChip::YM2151: return "YM2151";
        }
        return "unknown";
    }

    static std::uint16_t encodeKey(VgmChip chip, int port, std::uint8_t address) {
        // Only YM2612 has two register ports; retain port in the key.
        (void)chip;
        return static_cast<std::uint16_t>(((port & 1) << 8) | address);
    }

    static std::string hexKey(std::uint16_t key) {
        std::ostringstream out;
        out << std::uppercase << std::hex << std::setfill('0');
        if ((key & 0x100) != 0)
            out << std::setw(2) << (key & 0xFF00) / 0x100 << ':';
        out << std::setw(2) << (key & 0xFF);
        return out.str();
    }

    static std::string signature(VgmChip chip, int channel,
                                 const std::map<std::uint16_t, std::uint8_t>& regs) {
        std::ostringstream out;
        out << chipIndex(chip) << '/' << channel << '/';
        for (const auto& [key, value] : regs)
            out << key << '=' << static_cast<unsigned>(value) << ';';
        return out.str();
    }

    std::uint8_t read(VgmChip chip, int port, std::uint8_t address) const {
        const auto& s = state_[chipIndex(chip)];
        const auto it = s.find(encodeKey(chip, port, address));
        return it == s.end() ? 0 : it->second; // chip registers reset to zero
    }

    std::map<std::uint16_t, std::uint8_t> makeOpnPatch(VgmChip chip, int channel) const {
        std::map<std::uint16_t, std::uint8_t> result;
        const int port = (chip == VgmChip::YM2612 && channel >= 3) ? 1 : 0;
        const int localChannel = channel % 3;

        // Four operators; address slot offsets are 0, 4, 8, 12.
        // Include DT/MUL, TL, KS/AR, AM/D1R, D2R, D1L/RR, SSG-EG.
        for (int base = 0x30; base <= 0x90; base += 0x10) {
            for (int slotOffset = 0; slotOffset <= 0x0C; slotOffset += 4) {
                const auto addr = static_cast<std::uint8_t>(base + slotOffset + localChannel);
                const auto key = encodeKey(chip, port, addr);
                result[key] = read(chip, port, addr);
            }
        }

        // Algorithm/feedback and pan/AMS/FMS (some fields are not implemented on YM2203).
        for (int base : {0xB0, 0xB4}) {
            const auto addr = static_cast<std::uint8_t>(base + localChannel);
            const auto key = encodeKey(chip, port, addr);
            result[key] = read(chip, port, addr);
        }
        return result;
    }

    std::map<std::uint16_t, std::uint8_t> makeYm2151Patch(int channel) const {
        std::map<std::uint16_t, std::uint8_t> result;
        // RL/FB/algorithm/connection register for this channel.
        {
            const auto addr = static_cast<std::uint8_t>(0x20 + channel);
            result[encodeKey(VgmChip::YM2151, 0, addr)] = read(VgmChip::YM2151, 0, addr);
        }

        // Six operator parameter groups, four operator slots per channel.
        // Frequency/key-code registers (0x28..0x2F) and global LFO/noise
        // controls are intentionally excluded from the patch.
        for (int base = 0x40; base <= 0xE0; base += 0x20) {
            for (int slotOffset = 0; slotOffset <= 0x18; slotOffset += 8) {
                const auto addr = static_cast<std::uint8_t>(base + slotOffset + channel);
                result[encodeKey(VgmChip::YM2151, 0, addr)] =
                    read(VgmChip::YM2151, 0, addr);
            }
        }
        return result;
    }

    void capture(VgmChip chip, int channel, std::uint8_t keyMask,
                 std::uint64_t sample) {
        auto registers = (chip == VgmChip::YM2151)
            ? makeYm2151Patch(channel)
            : makeOpnPatch(chip, channel);

        const std::string sig = signature(chip, channel, registers);
        const auto found = lookup_.find(sig);
        if (found != lookup_.end()) {
            auto& existing = patches_[found->second];
            existing.lastSample = sample;
            existing.keyOnCount++;
            existing.lastKeyOnMask = keyMask;
            return;
        }

        VgmPatchSnapshot patch;
        patch.chip = chip;
        patch.channel = channel;
        patch.firstSample = sample;
        patch.lastSample = sample;
        patch.keyOnCount = 1;
        patch.lastKeyOnMask = keyMask;
        patch.registers = std::move(registers);

        lookup_[sig] = patches_.size();
        patches_.push_back(std::move(patch));
    }
};
