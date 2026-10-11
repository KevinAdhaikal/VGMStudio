#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace vgmstudio {

enum class InstCategory : uint8_t {
    All = 0,
    SoundChips,
    FM,
    PSG_Square,
    Delays,
    KeyEvents,
    DataBlock,
    Other
};

struct VgmInstruction {
    uint32_t index = 0;          // Sequential instruction index (0, 1, 2...)
    uint32_t fileOffset = 0;     // Byte offset in VGM file
    uint32_t samplePos = 0;      // Timestamp in 44.1 kHz samples
    uint8_t  cmd = 0;            // Opcode byte
    uint32_t length = 0;         // Instruction length in bytes
    char     hexBytes[32] = {0}; // "54 28 01"
    char     chip[24] = {0};     // "YM2151", "YM2612", "SN76489", "Wait", etc.
    char     channel[24] = {0};  // "Ch 1", "Ch 2", "Noise", "DAC", "Global", etc.
    char     desc[128] = {0};    // Disassembled human-readable description
    InstCategory category = InstCategory::Other;
    bool isWait = false;
    bool isKeyOn = false;
    bool isKeyOff = false;
};

class VgmDisassembly {
public:
    VgmDisassembly();
    ~VgmDisassembly();

    void Clear();
    bool Disassemble(const uint8_t* vgmData, size_t dataSize);

    const std::vector<VgmInstruction>& GetInstructions() const { return m_instructions; }
    size_t GetCount() const { return m_instructions.size(); }
    bool IsEmpty() const { return m_instructions.empty(); }

    uint32_t GetDataStartOffset() const { return m_dataStartOfs; }
    uint32_t GetDataEndOffset() const { return m_dataEndOfs; }
    uint32_t GetTotalSamples() const { return m_totalSamples; }

    // Fast binary search to find instruction index corresponding to the given playback sample
    int FindInstructionIndexAtSample(uint32_t sample) const;

    // Fast binary search to find instruction index covering the given file byte offset
    int FindInstructionIndexAtOffset(uint32_t offset) const;

    // List of unique sound chips detected in the instruction stream
    const std::vector<std::string>& GetDetectedChips() const { return m_detectedChips; }
    const std::vector<std::string>& GetDetectedChannels() const { return m_detectedChannels; }

private:
    std::vector<VgmInstruction> m_instructions;
    std::vector<std::string> m_detectedChips;
    std::vector<std::string> m_detectedChannels;
    uint32_t m_dataStartOfs = 0x40;
    uint32_t m_dataEndOfs = 0;
    uint32_t m_totalSamples = 0;
};

} // namespace vgmstudio
