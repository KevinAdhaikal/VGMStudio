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
    uint32_t romOfs = 0;
    uint32_t rawSize = 0;
    uint32_t defaultSampleRate = 16000;
    std::string notes;
    std::vector<float> decodedPcm;
    float durationSec = 0.0f;
    float peakDb = -96.0f;
};

struct PcmSliceItem {
    int id = 0;
    std::string chip;               // "YM2612", "SegaPCM", "NES APU", "MultiPCM", "RF5C164", "RF5C68", "DAC Stream"
    std::string channelName;        // "Ch 6 / DAC", "PCM 1", "DMC", etc.
    int parentSampleId = 0;         // ID of the parent sample in m_samples
    int parentSampleIndex = -1;     // Index in m_samples
    uint32_t firstSamplePos = 0;    // Song sample position
    float songTimeSec = 0.0f;       // Song timestamp in seconds
    std::string songTimeStr;        // e.g. "00:00.93"
    uint32_t sliceOffset = 0;       // Cut start (byte / address)
    uint32_t sliceLength = 0;       // Cut length
    uint32_t sliceEndOffset = 0;    // Cut end (byte / address)
    size_t sampleStartInParent = 0; // Relative sample start index in parent.decodedPcm
    size_t sampleEndInParent = 0;   // Relative sample end index in parent.decodedPcm
    int triggerCount = 1;           // Number of triggers in song
    uint32_t defaultSampleRate = 16000;
    float durationSec = 0.0f;
    float peakDb = -96.0f;
    std::vector<float> decodedPcm;  // Sliced PCM audio buffer
    std::vector<float> occurrencesSec; // All timestamps where triggered
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
    void AnalyzeSongPcmSlices(const uint8_t* vgmData, size_t dataSize);
    bool SaveWavFile(const std::string& outPath, const std::vector<float>& pcm, uint32_t sampleRate);
    void RenderAuditionPiano(AudioEngine& audioEngine, const std::vector<float>& pcmData, uint32_t sampleRate);
    void TriggerAuditionPcm(AudioEngine& audioEngine, const std::vector<float>& pcmData, uint32_t sampleRate, int midiNote);
    void StopAuditionPcm(AudioEngine& audioEngine);

    std::vector<PcmSampleItem> m_samples;
    int m_selectedIndex = -1;
    const uint8_t* m_lastDataPtr = nullptr;
    size_t m_lastDataSize = 0;

    // Assembled chip buffers
    std::vector<uint8_t> m_multiPcmRom;
    std::vector<uint8_t> m_rf5cRam;

    // Slices extracted from song playback instructions
    std::vector<PcmSliceItem> m_slices;
    int m_selectedSliceIndex = -1;
    int m_viewMode = 0; // 0 = Song PCM Slices, 1 = Raw Sample Banks
    bool m_zoomToSlice = false;
    int m_sliceSortMode = 0; // 0 = Timeline, 1 = Most Used, 2 = Offset
    char m_sliceSearchFilter[64] = "";

    // Preview / playback settings
    int m_playbackSampleRate = 16000;
    float m_playbackVolume = 1.0f;
    float m_cursorNorm = 0.0f;
    bool m_wasPlaying = false;
    float m_leftPaneWidth = 380.0f;

    // Audition Piano Keyboard settings
    int m_testOctave = 3;
    float m_testVolume = 0.85f;
    int m_activeTestKey = -1;
    bool m_mouseTesting = false;
    int m_lastTestedMidi = 60;

    // Filters
    int m_chipFilterIdx = 0;
    std::vector<std::string> m_chipFilterOptions;

    // Feedback toast
    std::string m_toastMessage;
    float m_toastTimer = 0.0f;
};

} // namespace vgmstudio
