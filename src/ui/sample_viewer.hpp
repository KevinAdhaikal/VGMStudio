#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include "../audio/audio_engine.hpp"

namespace vgmstudio {

struct PcmSampleItem {
    int id = 0;
    std::string name;
    std::string chip;
    std::string formatName;
    uint8_t dblkType = 0;
    uint32_t fileOffset = 0;
    uint32_t rawSize = 0;
    uint32_t defaultSampleRate = 16000;
    std::string notes;
    std::vector<float> decodedPcm;
    float durationSec = 0.0f;
    float peakDb = -96.0f;
};

class SampleViewer {
public:
    SampleViewer();
    ~SampleViewer();

    void Render(AudioEngine& audioEngine, bool* pOpen);
    void RescanSamples(const AudioEngine& audioEngine);
    void RequestFocus() { m_focusRequested = true; }

private:
    bool m_focusRequested = false;
    void ExtractSamples(const uint8_t* vgmData, size_t dataSize);
    bool SaveWavFile(const std::string& outPath, const std::vector<float>& pcm, uint32_t sampleRate);

    std::vector<PcmSampleItem> m_samples;
    int m_selectedIndex = -1;
    const uint8_t* m_lastDataPtr = nullptr;
    size_t m_lastDataSize = 0;

    // Preview / playback settings
    int m_playbackSampleRate = 16000;
    float m_playbackVolume = 1.0f;
    float m_cursorNorm = 0.0f;
    bool m_wasPlaying = false;
    float m_leftPaneWidth = 380.0f;

    // Filters
    int m_chipFilterIdx = 0;
    std::vector<std::string> m_chipFilterOptions;

    // Feedback toast
    std::string m_toastMessage;
    float m_toastTimer = 0.0f;
};

} // namespace vgmstudio
