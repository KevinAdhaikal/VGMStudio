#pragma once

#include <SDL.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>

#include "../vgm/vgm_info.hpp"
#include "../vgm/note_tracker.hpp"
#include "../vgm/vgm_disassembly.hpp"
#include "../vgm/vgmplayer.hpp"
#include "../vgm/FileLoader.h"
#include "../emu/Resampler.h"
#include "../vgm/vgm_patch_extractor.hpp"

namespace vgmstudio {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool Init(int sampleRate = 44100, int bufferSize = 512);
    void Shutdown();

    bool LoadFile(const std::string& path);
    void UnloadFile();

    void Play();
    void Pause();
    void TogglePlayPause();
    void Stop();
    void Rewind();

    void SeekToSample(uint32_t sample);
    void SeekToFraction(float fraction);

    void SetVolume(float vol); // 0.0f to 1.0f
    float GetVolume() const { return m_volume.load(); }

    void SetLoop(bool loop);
    bool GetLoop() const { return m_loop.load(); }

    bool IsPlaying() const;
    bool IsPaused() const;
    bool IsLoaded() const { return m_loaded.load(); }

    uint32_t GetCurSample() const;
    double GetCurSeconds() const;
    uint32_t GetTotalSamples() const;
    double GetTotalSeconds() const;

    const VgmInfo& GetCurrentInfo() const { return m_currentInfo; }
    void UpdateSongInfo(const VgmInfo& newInfo) { m_currentInfo = newInfo; }
    std::string GetLastLoadError() const { return m_lastLoadError; }

    // Playback Speed & Pitch Shifting
    void SetPlaybackSpeed(float speed);
    float GetPlaybackSpeed() const { return m_playbackSpeed.load(); }
    void SetPitchSemitones(float semitones);
    float GetPitchSemitones() const { return m_pitchSemitones.load(); }
    void SetLinkSpeedPitch(bool link);
    bool GetLinkSpeedPitch() const { return m_linkSpeedPitch.load(); }
    void ResetSpeedAndPitch();
    void UpdatePlayerSpeed();

    // VGM Player Settings
    struct Settings {
        int loopMode = 0;             // 0 = Infinite Loop, 1 = Fixed Loop Count, 2 = Play Once (No Loop)
        int loopCount = 2;            // Loop count when Fixed Loop Count is selected
        bool enableFadeout = true;    // Fade out at end of loops
        float fadeoutSeconds = 4.0f;  // Fadeout length in seconds (1.0 to 10.0)
        float playbackSpeed = 1.0f;   // 0.50x to 2.00x playback speed
        int regionOverride = 0;       // 0 = Original / Auto, 1 = NTSC (60 Hz), 2 = PAL (50 Hz)

        int sampleRate = 44100;       // 44100 Hz, 48000 Hz, 96000 Hz
        int bufferFrames = 512;       // 256, 512, 1024, 2048
        int resampleQuality = 0;      // 0 = High Quality (HQ), 1 = Fast / Low Latency

        int ym2612Core = 0;           // 0 = Nuked OPN2, 1 = MAME OPN2
        int opl3Core = 0;             // 0 = Nuked OPL3, 1 = MAME OPL3
        int ym2151Core = 0;           // 0 = Nuked OPM, 1 = MAME OPM
        int sn76489Core = 0;          // 0 = Maxim SN76489, 1 = MAME SN76496
        int nesCore = 0;              // 0 = NSFPlay NES APU, 1 = MAME NES
    };

    Settings& GetSettings() { return m_settings; }
    const Settings& GetSettings() const { return m_settings; }
    void ApplySettings(const Settings& s);

    // Live visualization data
    void GetWaveform(float* outL, float* outR, int count);
    void GetPeakLevels(float& outL, float& outR);
    void GetChannelWaveform(size_t chIdx, float* outBuf, int count);

    // Note Tracker
    const NoteTracker& GetNoteTracker() const { return m_noteTracker; }
    NoteTracker& GetNoteTracker() { return m_noteTracker; }

    // VGM Instruction Disassembly
    const VgmDisassembly& GetDisassembly() const { return m_disassembly; }
    VgmDisassembly& GetDisassembly() { return m_disassembly; }

    // Channel control & real hardware muting
    void SetChannelMute(int channelIdx, bool mute);
    void SetChannelSolo(int channelIdx, bool solo);
    void ToggleChannelMute(int channelIdx);
    void ToggleChannelSolo(int channelIdx);
    void MuteAllChannels(bool mute);
    void ClearAllSolo();
    void UpdateChannelMuting();

    // Per-channel mixer adjustments (Volume & Pan knobs)
    void SetChannelVolume(size_t chIdx, float vol);
    float GetChannelVolume(size_t chIdx) const;
    void SetChannelPan(size_t chIdx, float pan);
    float GetChannelPan(size_t chIdx) const;

    // Raw VGM data access
    const uint8_t* GetVgmData() const;
    size_t GetVgmDataSize() const;

    // Preview / Audition synthesizer for testing instruments, samples, and wavetables
    void StartPreview(float freq, int mode = 0, float param1 = 0.0f, float param2 = 0.0f);
    void StopPreview();
    void StartPreviewNote(int noteId, float freq, int mode = 0, float param1 = 0.0f, float param2 = 0.0f);
    void StopPreviewNote(int noteId);
    void StopAllPreviewNotes();
    void PlayPreviewSampleBuffer(const std::vector<float>& pcmData);
    void PlayPreviewSamplePitched(const std::vector<float>& pcmData, float pitchRatio, size_t startOffset = 0);
    void PausePreviewSample();
    void ResumePreviewSample();
    void StopPreviewSample();
    bool IsPreviewSamplePlaying() const;
    bool IsPreviewSamplePaused() const;
    void SetPreviewSampleVolume(float vol);
    float GetPreviewSampleVolume() const;
    float GetPreviewSampleProgress() const;
    void SetPreviewWavetable(const std::vector<float>& wavetable);
    void SetPreviewFmParams(int alg, int fb, float mult1, float tl1, float mult2, float tl2, float mult3, float tl3, float mult4, float tl4);
    void SetPreviewFmPatch(const FmPresetData& fm, const std::string& chipName);
    bool IsPreviewActive() const { return m_previewActive.load(); }
    void RenderAudioFrames(int16_t* outStream, int frames) { ProcessAudio(outStream, frames); }

private:
    static void AudioCallback(void* userdata, Uint8* stream, int len);
    static UINT8 PlayerEventCallback(PlayerBase* player, void* userParam, UINT8 evtType, void* evtParam);
    void ProcessAudio(int16_t* outStream, int frames);

    SDL_AudioDeviceID m_deviceId = 0;
    int m_sampleRate = 44100;
    int m_bufferFrames = 2048;

    DATA_LOADER* m_dLoad = nullptr;
    std::unique_ptr<VGMPlayer> m_player;
    VgmInfo m_currentInfo;
    NoteTracker m_noteTracker;
    VgmDisassembly m_disassembly;

    std::atomic<bool> m_loaded{false};
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_loop{true};
    std::atomic<float> m_volume{0.8f};

    // Playback Speed & Pitch shifting
    std::atomic<float> m_playbackSpeed{1.0f};
    std::atomic<float> m_pitchSemitones{0.0f};
    std::atomic<bool> m_linkSpeedPitch{false};
    std::vector<WAVE_32BS> m_resampleFifo;
    double m_resamplePhase{0.0};

    Settings m_settings;
    std::atomic<int> m_loopCountCurrent{0};
    std::atomic<bool> m_isFadingOut{false};
    std::atomic<uint32_t> m_fadeSamplesLeft{0};
    std::atomic<uint32_t> m_fadeSamplesTotal{0};

    std::atomic<uint32_t> m_curSample{0};

    // Temporary sample render buffer
    std::vector<WAVE_32BS> m_renderBuf;

    // Master oscilloscope ring buffer
    static constexpr int OSC_BUFFER_SIZE = 4096;
    float m_oscBufferL[OSC_BUFFER_SIZE]{0};
    float m_oscBufferR[OSC_BUFFER_SIZE]{0};
    std::atomic<int> m_oscWritePos{0};
    uint64_t m_totalOscSamples{0};
    uint64_t m_lastOscTriggerSample{0};
    uint64_t m_lastOscTotalSamples{0};
    float m_oscPeakTracker{1.0f};

    // Per-channel real-time oscilloscope ring buffers
    static constexpr int CHAN_OSC_SIZE = 512;
    static constexpr size_t MAX_CHANNELS = 64;
    struct ChannelOsc {
        float buffer[CHAN_OSC_SIZE]{0};
        float phase = 0.0f;
    };
    std::array<ChannelOsc, MAX_CHANNELS> m_chanOsc;
    std::atomic<int> m_chanOscWritePos{0};

    // Preview / Audition synthesizer state (Polyphonic)
    static constexpr int MAX_PREVIEW_VOICES = 8;
    struct PreviewVoice {
        int noteId = -1;
        float freq = 440.0f;
        bool active = false;
        bool keyOn = false;
        uint32_t releaseFrames = 0;
        float phase = 0.0f;
        float duty = 0.5f;
        float volume = 1.0f;
        int fmHardwareCh = -1; // 0..5 for FM
        uint64_t triggerOrder = 0;
    };
    std::array<PreviewVoice, MAX_PREVIEW_VOICES> m_previewVoices;
    uint64_t m_voiceTriggerCounter = 0;

    std::atomic<bool> m_previewActive{false};
    std::atomic<float> m_previewFreq{440.0f};
    std::atomic<int> m_previewMode{0}; // 0 = Sine/FM, 1 = Square/Pulse, 2 = Triangle, 3 = Saw/Wavetable, 4 = Noise
    std::atomic<float> m_previewParam1{0.0f};
    std::atomic<float> m_previewParam2{0.0f};
    float m_previewPhase = 0.0f;
    float m_previewModPhase = 0.0f;
    std::vector<float> m_previewSampleBuffer;
    float m_previewSamplePosFloat = 0.0f;
    float m_previewSampleStep = 1.0f;
    size_t m_previewSamplePos = 0;
    std::atomic<bool> m_previewSamplePaused{false};
    std::atomic<float> m_previewSampleVolume{1.0f};

    std::vector<float> m_previewWavetable;
    int m_previewFmAlg = 4;
    int m_previewFmFb = 3;
    float m_previewOpMult[4] = {1.0f, 1.0f, 2.0f, 1.0f};
    float m_previewOpTL[4] = {38.0f, 0.0f, 42.0f, 4.0f};
    float m_previewOpPhase[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Authentic FM Preview Synth state (Yamaha OPN hardware engine - 6 hardware channels)
    void* m_previewFmChip = nullptr;
    uint32_t m_previewFmClock = 4000000;
    std::atomic<bool> m_previewFmKeyOn{false};
    uint32_t m_previewFmReleaseFrames{0};
    FmPresetData m_previewFmPatch;
    std::string m_previewFmChipName;
    // Peak levels with decay
    std::atomic<float> m_peakL{0.0f};
    std::atomic<float> m_peakR{0.0f};

    mutable std::mutex m_audioMutex;
    std::string m_lastLoadError;
};

} // namespace vgmstudio
