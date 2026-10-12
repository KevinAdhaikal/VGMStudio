#include "audio_engine.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <filesystem>
#include "emu/channel_mixer.h"
#include "emu/SoundDevs.h"

namespace vgmstudio {

AudioEngine::AudioEngine() {
    m_player = std::make_unique<VGMPlayer>();
    m_renderBuf.resize(4096);
}

AudioEngine::~AudioEngine() {
    Shutdown();
}

bool AudioEngine::Init(int sampleRate, int bufferSize) {
    m_sampleRate = sampleRate;
    m_bufferFrames = bufferSize;

    SDL_AudioSpec desired, obtained;
    std::memset(&desired, 0, sizeof(desired));
    desired.freq = m_sampleRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = m_bufferFrames;
    desired.callback = AudioCallback;
    desired.userdata = this;

    m_deviceId = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (m_deviceId == 0) {
        std::cerr << "[AudioEngine] Failed to open audio device: " << SDL_GetError() << std::endl;
        return false;
    }

    m_sampleRate = obtained.freq;
    m_bufferFrames = obtained.samples;
    m_renderBuf.resize(m_bufferFrames * 2);

    m_player->SetSampleRate(m_sampleRate);
    m_player->SetEventCallback(PlayerEventCallback, this);

    // Start audio device thread (starts unpaused in SDL, but our flag controls playback)
    SDL_PauseAudioDevice(m_deviceId, 0);

    std::cout << "[AudioEngine] Audio initialized: " << m_sampleRate << " Hz, " 
              << m_bufferFrames << " frames buffer." << std::endl;
    return true;
}

void AudioEngine::Shutdown() {
    if (m_deviceId != 0) {
        SDL_CloseAudioDevice(m_deviceId);
        m_deviceId = 0;
    }
    UnloadFile();
}

bool AudioEngine::LoadFile(const std::string& path) {
    m_lastLoadError.clear();
    if (m_deviceId == 0) {
        m_lastLoadError = "Audio device not initialized.";
        return false;
    }

    SDL_LockAudioDevice(m_deviceId);

    m_playing.store(false);
    m_paused.store(false);
    m_curSample.store(0);

    if (m_loaded.load()) {
        m_player->Stop();
        m_player->UnloadFile();
        if (m_dLoad) {
            DataLoader_Deinit(m_dLoad);
            m_dLoad = nullptr;
        }
        m_loaded.store(false);
    }

#ifdef _WIN32
    std::filesystem::path fsPath(std::filesystem::u8path(path));
    m_dLoad = FileLoader_InitW(fsPath.wstring().c_str());
#else
    m_dLoad = FileLoader_Init(path.c_str());
#endif
    if (!m_dLoad) {
        m_lastLoadError = "Could not initialize file loader for path: " + path;
        SDL_UnlockAudioDevice(m_deviceId);
        return false;
    }

    if (DataLoader_Load(m_dLoad)) {
        m_lastLoadError = "Failed to open or decompress file data.";
        DataLoader_Deinit(m_dLoad);
        m_dLoad = nullptr;
        SDL_UnlockAudioDevice(m_deviceId);
        return false;
    }

    UINT8 err = m_player->LoadFile(m_dLoad);
    if (err) {
        if (err == 0xF0) {
            m_lastLoadError = "Invalid file signature or corrupted header (must be a valid VGM stream).";
        } else {
            char errBuf[64];
            snprintf(errBuf, sizeof(errBuf), "VGM player engine rejected file (error code 0x%02X).", err);
            m_lastLoadError = errBuf;
        }
        DataLoader_Deinit(m_dLoad);
        m_dLoad = nullptr;
        SDL_UnlockAudioDevice(m_deviceId);
        return false;
    }

    m_player->SetSampleRate(m_sampleRate);
    m_loaded.store(true);

    m_currentInfo = InspectVgmFile(path);

    // Track notes & disassemble instructions across the song
    if (m_dLoad) {
        m_noteTracker.ProcessVgm(DataLoader_GetData(m_dLoad), DataLoader_GetSize(m_dLoad), m_sampleRate, m_player.get());
        m_disassembly.Disassemble(DataLoader_GetData(m_dLoad), DataLoader_GetSize(m_dLoad));
    }

    // Initialize chips & sound devices so they are ready for seeking or playback
    m_player->ResetDeviceMuting();
    m_player->Start();
    m_player->Reset();
    ChannelMixer_Reset();
    UpdateChannelMuting();
    m_resampleFifo.clear();
    m_resamplePhase = 0.0;
    UpdatePlayerSpeed();
    m_playing.store(false);
    m_paused.store(false);
    m_curSample.store(0);
    m_loopCountCurrent.store(0);
    m_isFadingOut.store(false);
    m_fadeSamplesLeft.store(0);
    m_fadeSamplesTotal.store(0);
    ApplySettings(m_settings);

    SDL_UnlockAudioDevice(m_deviceId);
    return true;
}

void AudioEngine::UnloadFile() {
    if (m_deviceId != 0) {
        SDL_LockAudioDevice(m_deviceId);
    }

    m_playing.store(false);
    m_paused.store(false);
    ChannelMixer_Reset();
    if (m_player) {
        m_player->ResetDeviceMuting();
    }

    if (m_loaded.load()) {
        m_player->Stop();
        m_player->UnloadFile();
        if (m_dLoad) {
            DataLoader_Deinit(m_dLoad);
            m_dLoad = nullptr;
        }
        m_loaded.store(false);
    }

    m_noteTracker.Clear();
    m_disassembly.Clear();
    m_currentInfo = VgmInfo();

    if (m_deviceId != 0) {
        SDL_UnlockAudioDevice(m_deviceId);
    }
}

void AudioEngine::Play() {
    if (!m_loaded.load()) return;
    if (m_deviceId != 0) SDL_LockAudioDevice(m_deviceId);

    if (m_paused.load()) {
        m_paused.store(false);
        m_playing.store(true);
    } else if (!m_playing.load()) {
        uint32_t cur = m_player->GetCurPos(PLAYPOS_TICK);
        if (m_currentInfo.totalSamples > 0 && cur >= m_currentInfo.totalSamples) {
            m_player->Seek(PLAYPOS_TICK, 0);
            m_curSample.store(0);
        }
        if ((m_player->GetState() & PLAYSTATE_PLAY) == 0) {
            m_player->Start();
        }
        m_playing.store(true);
    }

    if (m_deviceId != 0) SDL_UnlockAudioDevice(m_deviceId);
}

void AudioEngine::Pause() {
    if (!m_loaded.load()) return;
    if (m_playing.load()) {
        m_paused.store(true);
        m_playing.store(false);
        std::fill(std::begin(m_oscBufferL), std::end(m_oscBufferL), 0.0f);
        std::fill(std::begin(m_oscBufferR), std::end(m_oscBufferR), 0.0f);
        m_peakL.store(0.0f);
        m_peakR.store(0.0f);
        ChannelMixer_ResetWaveforms();
    }
}

void AudioEngine::TogglePlayPause() {
    if (!m_loaded.load()) return;
    if (m_playing.load()) {
        Pause();
    } else {
        Play();
    }
}

void AudioEngine::Stop() {
    if (!m_loaded.load()) return;
    if (m_deviceId != 0) SDL_LockAudioDevice(m_deviceId);

    m_playing.store(false);
    m_paused.store(false);
    m_player->Reset();
    m_curSample.store(0);
    m_loopCountCurrent.store(0);
    m_isFadingOut.store(false);
    m_fadeSamplesLeft.store(0);
    m_fadeSamplesTotal.store(0);
    m_resampleFifo.clear();
    m_resamplePhase = 0.0;
    std::fill(std::begin(m_oscBufferL), std::end(m_oscBufferL), 0.0f);
    std::fill(std::begin(m_oscBufferR), std::end(m_oscBufferR), 0.0f);
    m_peakL.store(0.0f);
    m_peakR.store(0.0f);
    ChannelMixer_ResetWaveforms();

    if (m_deviceId != 0) SDL_UnlockAudioDevice(m_deviceId);
}

void AudioEngine::Rewind() {
    SeekToSample(0);
}

void AudioEngine::SeekToSample(uint32_t sample) {
    if (!m_loaded.load()) return;
    if (m_deviceId != 0) SDL_LockAudioDevice(m_deviceId);

    if ((m_player->GetState() & PLAYSTATE_PLAY) == 0) {
        m_player->Start();
    }
    m_player->Seek(PLAYPOS_TICK, sample);
    m_curSample.store(sample);
    m_loopCountCurrent.store(0);
    m_isFadingOut.store(false);
    m_fadeSamplesLeft.store(0);
    m_fadeSamplesTotal.store(0);
    m_resampleFifo.clear();
    m_resamplePhase = 0.0;

    if (m_deviceId != 0) SDL_UnlockAudioDevice(m_deviceId);
}

void AudioEngine::SeekToFraction(float fraction) {
    if (!m_loaded.load()) return;
    uint32_t total = GetTotalSamples();
    if (total == 0) return;
    uint32_t target = (uint32_t)(fraction * total);
    SeekToSample(target);
}

void AudioEngine::SetPlaybackSpeed(float speed) {
    speed = std::clamp(speed, 0.25f, 2.50f);
    m_playbackSpeed.store(speed);
    if (m_linkSpeedPitch.load()) {
        float semitones = 12.0f * (float)std::log2((double)speed);
        m_pitchSemitones.store(semitones);
    }
    UpdatePlayerSpeed();
}

void AudioEngine::SetPitchSemitones(float semitones) {
    semitones = std::clamp(semitones, -12.0f, 12.0f);
    m_pitchSemitones.store(semitones);
    if (m_linkSpeedPitch.load()) {
        float speed = (float)std::pow(2.0, (double)semitones / 12.0);
        m_playbackSpeed.store(std::clamp(speed, 0.25f, 2.50f));
    }
    UpdatePlayerSpeed();
}

void AudioEngine::SetLinkSpeedPitch(bool link) {
    m_linkSpeedPitch.store(link);
    if (link) {
        float speed = m_playbackSpeed.load();
        float semitones = 12.0f * (float)std::log2((double)speed);
        m_pitchSemitones.store(semitones);
        UpdatePlayerSpeed();
    }
}

void AudioEngine::ResetSpeedAndPitch() {
    m_playbackSpeed.store(1.0f);
    m_pitchSemitones.store(0.0f);
    UpdatePlayerSpeed();
}

void AudioEngine::UpdatePlayerSpeed() {
    if (!m_player) return;
    float speed = m_playbackSpeed.load();
    float pitchSemitones = m_pitchSemitones.load();
    float pitchRatio = (float)std::pow(2.0, (double)pitchSemitones / 12.0);

    float internalSpeed = 1.0f;
    if (m_linkSpeedPitch.load()) {
        internalSpeed = 1.0f;
    } else {
        if (pitchRatio > 0.001f) {
            internalSpeed = speed / pitchRatio;
        } else {
            internalSpeed = speed;
        }
    }
    internalSpeed = std::clamp(internalSpeed, 0.1f, 10.0f);
    m_player->SetPlaybackSpeed((double)internalSpeed);
}

void AudioEngine::SetVolume(float vol) {
    m_volume.store(std::clamp(vol, 0.0f, 1.5f));
}

void AudioEngine::SetLoop(bool loop) {
    m_loop.store(loop);
    if (loop && m_loaded.load()) {
        uint32_t total = m_currentInfo.totalSamples;
        uint32_t cur = m_curSample.load();
        if (!m_playing.load() && total > 0 && cur >= total) {
            Rewind();
            Play();
        }
    }
}

void AudioEngine::ApplySettings(const Settings& s) {
    m_settings = s;
    if (m_player) {
        UpdatePlayerSpeed();

        VGM_PLAY_OPTIONS opts;
        if (m_player->GetPlayerOptions(opts) == 0) {
            if (m_settings.regionOverride == 1) {
                opts.playbackHz = 60;
            } else if (m_settings.regionOverride == 2) {
                opts.playbackHz = 50;
            } else {
                opts.playbackHz = 0;
            }
            m_player->SetPlayerOptions(opts);
        }
    }
    if (m_settings.loopMode == 2) {
        m_loop.store(false);
    } else {
        m_loop.store(true);
    }
}

bool AudioEngine::IsPlaying() const {
    return m_playing.load() && !m_paused.load();
}

bool AudioEngine::IsPaused() const {
    return m_paused.load();
}

uint32_t AudioEngine::GetCurSample() const {
    return m_curSample.load();
}

double AudioEngine::GetCurSeconds() const {
    return (double)m_curSample.load() / (double)m_sampleRate;
}

uint32_t AudioEngine::GetTotalSamples() const {
    return m_currentInfo.totalSamples;
}

double AudioEngine::GetTotalSeconds() const {
    return m_currentInfo.totalSeconds;
}

void AudioEngine::GetWaveform(float* outL, float* outR, int count) {
    if (!outL || !outR || count <= 0) return;
    if (!IsPlaying()) {
        std::fill(outL, outL + count, 0.0f);
        std::fill(outR, outR + count, 0.0f);
        m_lastOscTriggerSample = 0;
        return;
    }

    uint64_t curTotal = m_totalOscSamples;
    int writePos = m_oscWritePos.load();

    int latency = 640;
    if ((int)curTotal < latency + count + 1200) {
        std::fill(outL, outL + count, 0.0f);
        std::fill(outR, outR + count, 0.0f);
        return;
    }

    uint64_t targetSample = curTotal - latency;
    int targetIdx = (writePos - latency + OSC_BUFFER_SIZE) % OSC_BUFFER_SIZE;

    int scanLen = 1200;
    float minVal = 1e9f;
    float maxVal = -1e9f;
    for (int i = 0; i < scanLen; ++i) {
        int idx = (targetIdx - i + OSC_BUFFER_SIZE) % OSC_BUFFER_SIZE;
        float v = 0.5f * (m_oscBufferL[idx] + m_oscBufferR[idx]);
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
    }

    float peakToPeak = maxVal - minVal;

    // Silence threshold
    if (peakToPeak < 0.005f) {
        std::fill(outL, outL + count, 0.0f);
        std::fill(outR, outR + count, 0.0f);
        m_lastOscTriggerSample = 0;
        m_lastOscTotalSamples = curTotal;
        return;
    }

    float midLevel = 0.5f * (minVal + maxVal);
    float hystThreshold = peakToPeak * 0.001f;
    if (hystThreshold < 0.0001f) hystThreshold = 0.0001f;

    struct Crossing {
        uint64_t sample;
        float slope;
        float frac;
    };
    std::vector<Crossing> crossings;
    crossings.reserve(64);
    float maxSlope = 0.0f;

    for (int s = scanLen - 1; s >= 0; --s) {
        uint64_t curS = targetSample - s;
        int idx1 = (int)(curS % OSC_BUFFER_SIZE);
        int idx2 = (int)((curS + 1) % OSC_BUFFER_SIZE);
        float v1 = 0.5f * (m_oscBufferL[idx1] + m_oscBufferR[idx1]);
        float v2 = 0.5f * (m_oscBufferL[idx2] + m_oscBufferR[idx2]);

        if (v1 <= midLevel && v2 > midLevel) {
            float slope = v2 - v1;
            if (slope >= hystThreshold) {
                if (slope > maxSlope) maxSlope = slope;
                float frac = (midLevel - v1) / slope;
                if (frac < 0.0f) frac = 0.0f;
                if (frac > 1.0f) frac = 1.0f;
                crossings.push_back({ curS, slope, frac });
            }
        }
    }

    Crossing bestCrossing = { targetSample - count / 2, 0.0f, 0.0f };

    if (!crossings.empty()) {
        float slopeThreshold = maxSlope * 0.65f;
        std::vector<Crossing> primary;
        for (const auto& c : crossings) {
            if (c.slope >= slopeThreshold) primary.push_back(c);
        }
        if (primary.empty()) primary = crossings;

        uint64_t anchor = targetSample - count / 2;
        if (m_lastOscTriggerSample > 0 && m_lastOscTotalSamples > 0 && curTotal >= m_lastOscTotalSamples) {
            uint64_t elapsed = curTotal - m_lastOscTotalSamples;
            anchor = m_lastOscTriggerSample + elapsed;
        }

        int64_t bestDist = 0x7FFFFFFFFFFFFFFFLL;
        for (const auto& c : primary) {
            int64_t dist = std::abs((int64_t)c.sample - (int64_t)anchor);
            if (dist < bestDist) {
                bestDist = dist;
                bestCrossing = c;
            }
        }
    }

    m_lastOscTriggerSample = bestCrossing.sample;
    m_lastOscTotalSamples = curTotal;

    int halfCount = count / 2;
    float bestFrac = bestCrossing.frac;
    uint64_t bestS = bestCrossing.sample;

    for (int i = 0; i < count; ++i) {
        int offset = i - halfCount;
        uint64_t smpl = bestS + offset;
        int i1 = (int)(smpl % OSC_BUFFER_SIZE);
        int i2 = (int)((smpl + 1) % OSC_BUFFER_SIZE);

        float sL1 = m_oscBufferL[i1];
        float sL2 = m_oscBufferL[i2];
        float sR1 = m_oscBufferR[i1];
        float sR2 = m_oscBufferR[i2];

        outL[i] = (sL1 + (sL2 - sL1) * bestFrac) - midLevel;
        outR[i] = (sR1 + (sR2 - sR1) * bestFrac) - midLevel;
    }
}

void AudioEngine::GetChannelWaveform(size_t chIdx, float* outBuf, int count) {
    if (!outBuf || count <= 0) return;
    if (!IsPlaying()) {
        std::fill(outBuf, outBuf + count, 0.0f);
        return;
    }
    const auto& channels = m_noteTracker.GetChannels();
    if (chIdx < channels.size()) {
        const auto& ch = channels[chIdx];
        if (ch.muted) {
            std::fill(outBuf, outBuf + count, 0.0f);
            return;
        }
        int chipType = ch.chipType;
        int chipInst = ch.chipInstance;
        int chNum = ch.channelInChip;
        if (ch.chipType == DEVID_YM2203 && ch.linkIndex == 1) {
            chNum = 3 + ch.channelInChip;
        }
        ChannelMixer_GetWaveform(chipType, chipInst, chNum, outBuf, count);
        return;
    }
    std::fill(outBuf, outBuf + count, 0.0f);
}

void AudioEngine::SetChannelVolume(size_t chIdx, float vol) {
    auto& channels = m_noteTracker.GetChannels();
    if (chIdx < channels.size()) {
        channels[chIdx].volume = std::clamp(vol, 0.0f, 1.0f);
        int chNum = channels[chIdx].channelInChip;
        if (channels[chIdx].chipType == DEVID_YM2203 && channels[chIdx].linkIndex == 1) {
            chNum = 3 + channels[chIdx].channelInChip;
        }
        ChannelMixer_Set(channels[chIdx].chipType, channels[chIdx].chipInstance,
                         chNum, channels[chIdx].volume, channels[chIdx].pan);
    }
}

float AudioEngine::GetChannelVolume(size_t chIdx) const {
    const auto& channels = m_noteTracker.GetChannels();
    if (chIdx < channels.size()) {
        return channels[chIdx].volume;
    }
    return 1.0f;
}

void AudioEngine::SetChannelPan(size_t chIdx, float pan) {
    auto& channels = m_noteTracker.GetChannels();
    if (chIdx < channels.size()) {
        channels[chIdx].pan = std::clamp(pan, -1.0f, 1.0f);
        int chNum = channels[chIdx].channelInChip;
        if (channels[chIdx].chipType == DEVID_YM2203 && channels[chIdx].linkIndex == 1) {
            chNum = 3 + channels[chIdx].channelInChip;
        }
        ChannelMixer_Set(channels[chIdx].chipType, channels[chIdx].chipInstance,
                         chNum, channels[chIdx].volume, channels[chIdx].pan);
    }
}

float AudioEngine::GetChannelPan(size_t chIdx) const {
    const auto& channels = m_noteTracker.GetChannels();
    if (chIdx < channels.size()) {
        return channels[chIdx].pan;
    }
    return 0.0f;
}

void AudioEngine::GetPeakLevels(float& outL, float& outR) {
    outL = m_peakL.load();
    outR = m_peakR.load();

    // Smooth decay
    m_peakL.store(std::max(0.0f, outL - 0.05f));
    m_peakR.store(std::max(0.0f, outR - 0.05f));
}

void AudioEngine::AudioCallback(void* userdata, Uint8* stream, int len) {
    AudioEngine* self = static_cast<AudioEngine*>(userdata);
    int16_t* outStream = reinterpret_cast<int16_t*>(stream);
    int frames = len / (sizeof(int16_t) * 2);

    bool isPlaying = self->m_playing.load() && !self->m_paused.load() && self->m_loaded.load();
    bool hasPreview = self->m_previewActive.load() || (!self->m_previewSampleBuffer.empty() && self->m_previewSamplePosFloat < (float)self->m_previewSampleBuffer.size() && !self->m_previewSamplePaused.load());

    if (isPlaying || hasPreview) {
        self->ProcessAudio(outStream, frames);
    } else {
        std::memset(stream, 0, len);
    }
}

UINT8 AudioEngine::PlayerEventCallback(PlayerBase* player, void* userParam, UINT8 evtType, void* evtParam) {
    AudioEngine* self = static_cast<AudioEngine*>(userParam);
    if (!self) return 0x00;

    if (evtType == PLREVT_LOOP) {
        if (!self->m_loop.load() || self->m_settings.loopMode == 2) {
            return 0x01; // Stop playback when loop is disabled or loopMode is Play Once
        }
        if (self->m_settings.loopMode == 1) {
            UINT32 curLoop = evtParam ? *static_cast<UINT32*>(evtParam) : (self->m_loopCountCurrent.fetch_add(1) + 1);
            if ((int)curLoop >= self->m_settings.loopCount) {
                if (self->m_settings.enableFadeout) {
                    if (!self->m_isFadingOut.load()) {
                        self->m_isFadingOut.store(true);
                        uint32_t totalFade = (uint32_t)(self->m_settings.fadeoutSeconds * (float)self->m_sampleRate);
                        if (totalFade < 1000) totalFade = 1000;
                        self->m_fadeSamplesLeft.store(totalFade);
                        self->m_fadeSamplesTotal.store(totalFade);
                    }
                    return 0x00; // Allow libvgm to continue rendering into the fadeout
                } else {
                    return 0x01; // Stop playback
                }
            }
        }
        return 0x00; // 0x00 tells libvgm to loop naturally and seamlessly
    } else if (evtType == PLREVT_END) {
        if (!self->m_loop.load() || self->m_settings.loopMode == 2) {
            self->m_playing.store(false);
        }
        return 0x00;
    }
    return 0x00;
}

void AudioEngine::ProcessAudio(int16_t* outStream, int frames) {
    if ((int)m_renderBuf.size() < frames) {
        m_renderBuf.resize(frames);
    }

    std::memset(m_renderBuf.data(), 0, frames * sizeof(WAVE_32BS));
    UINT32 rendered = 0;

    float pitchSemitones = m_pitchSemitones.load();
    float pitchRatio = (float)std::pow(2.0, (double)pitchSemitones / 12.0);
    bool usePitchResample = (std::abs(pitchRatio - 1.0f) >= 0.005f);

    if (m_playing.load() && !m_paused.load() && m_loaded.load() && m_player) {
        if (!usePitchResample) {
            rendered = m_player->Render(frames, m_renderBuf.data());
        } else {
            double step = (double)pitchRatio;
            int neededSrc = (int)std::ceil(frames * step + m_resamplePhase) + 4;
            while ((int)m_resampleFifo.size() < neededSrc && (m_player->GetState() & PLAYSTATE_PLAY)) {
                std::vector<WAVE_32BS> chunk(256);
                UINT32 cRen = m_player->Render(256, chunk.data());
                if (cRen == 0) break;
                chunk.resize(cRen);
                m_resampleFifo.insert(m_resampleFifo.end(), chunk.begin(), chunk.end());
            }

            rendered = 0;
            int fifoSz = (int)m_resampleFifo.size();
            for (int i = 0; i < frames; ++i) {
                int idx = (int)m_resamplePhase;
                if (idx + 1 >= fifoSz) {
                    m_renderBuf[i].L = 0;
                    m_renderBuf[i].R = 0;
                    continue;
                }
                float frac = (float)(m_resamplePhase - idx);

                // Cubic Catmull-Rom interpolation for Left channel
                float y0L = (idx > 0) ? (float)m_resampleFifo[idx - 1].L : (float)m_resampleFifo[idx].L;
                float y1L = (float)m_resampleFifo[idx].L;
                float y2L = (float)m_resampleFifo[idx + 1].L;
                float y3L = (idx + 2 < fifoSz) ? (float)m_resampleFifo[idx + 2].L : y2L;

                float aL = -0.5f * y0L + 1.5f * y1L - 1.5f * y2L + 0.5f * y3L;
                float bL = y0L - 2.5f * y1L + 2.0f * y2L - 0.5f * y3L;
                float cL = -0.5f * y0L + 0.5f * y2L;
                float dL = y1L;
                m_renderBuf[i].L = (INT32)(aL * frac * frac * frac + bL * frac * frac + cL * frac + dL);

                // Cubic Catmull-Rom interpolation for Right channel
                float y0R = (idx > 0) ? (float)m_resampleFifo[idx - 1].R : (float)m_resampleFifo[idx].R;
                float y1R = (float)m_resampleFifo[idx].R;
                float y2R = (float)m_resampleFifo[idx + 1].R;
                float y3R = (idx + 2 < fifoSz) ? (float)m_resampleFifo[idx + 2].R : y2R;

                float aR = -0.5f * y0R + 1.5f * y1R - 1.5f * y2R + 0.5f * y3R;
                float bR = y0R - 2.5f * y1R + 2.0f * y2R - 0.5f * y3R;
                float cR = -0.5f * y0R + 0.5f * y2R;
                float dR = y1R;
                m_renderBuf[i].R = (INT32)(aR * frac * frac * frac + bR * frac * frac + cR * frac + dR);

                m_resamplePhase += step;
                rendered++;
            }

            int consumed = (int)m_resamplePhase;
            if (consumed > 0 && consumed <= (int)m_resampleFifo.size()) {
                m_resampleFifo.erase(m_resampleFifo.begin(), m_resampleFifo.begin() + consumed);
                m_resamplePhase -= consumed;
            }
        }
    }

    float vol = m_volume.load();

    // Fadeout handling if active
    if (m_isFadingOut.load()) {
        uint32_t left = m_fadeSamplesLeft.load();
        uint32_t total = m_fadeSamplesTotal.load();
        if (total > 0 && left > 0) {
            float fadeFactor = (float)left / (float)total;
            vol *= fadeFactor;
            if (left > (uint32_t)frames) {
                m_fadeSamplesLeft.store(left - (uint32_t)frames);
            } else {
                m_fadeSamplesLeft.store(0);
                m_isFadingOut.store(false);
                m_playing.store(false);
            }
        }
    }

    float curPeakL = 0.0f;
    float curPeakR = 0.0f;
    int oscPos = m_oscWritePos.load();

    bool previewActive = m_previewActive.load();
    float previewFreq = m_previewFreq.load();
    int previewMode = m_previewMode.load();
    float pParam1 = m_previewParam1.load();
    float pParam2 = m_previewParam2.load();

    for (UINT32 i = 0; i < (UINT32)frames; ++i) {
        float previewSample = 0.0f;
        if (previewActive) {
            float dt = previewFreq / (float)m_sampleRate;
            m_previewPhase += dt;
            if (m_previewPhase >= 1.0f) m_previewPhase -= 1.0f;

            if (previewMode == 0) { // FM 4-op simulation
                // Advance operator phases
                float mults[4];
                float gains[4];
                for (int k = 0; k < 4; ++k) {
                    mults[k] = (m_previewOpMult[k] > 0.0f) ? m_previewOpMult[k] : 0.5f;
                    gains[k] = std::exp(-m_previewOpTL[k] * 0.055f);
                    m_previewOpPhase[k] += dt * mults[k];
                    if (m_previewOpPhase[k] >= 1.0f) m_previewOpPhase[k] -= 1.0f;
                }

                float fbVal = std::sin(m_previewOpPhase[0] * 6.2831853f) * ((float)m_previewFmFb * 0.45f);
                float o0 = std::sin((m_previewOpPhase[0] + fbVal) * 6.2831853f) * gains[0];
                float o1 = 0.0f, o2 = 0.0f, o3 = 0.0f;

                switch (m_previewFmAlg) {
                    case 0: // 0 -> 1 -> 2 -> 3
                        o1 = std::sin((m_previewOpPhase[1] + o0 * 1.5f) * 6.2831853f) * gains[1];
                        o2 = std::sin((m_previewOpPhase[2] + o1 * 1.5f) * 6.2831853f) * gains[2];
                        o3 = std::sin((m_previewOpPhase[3] + o2 * 1.5f) * 6.2831853f) * gains[3];
                        previewSample = o3 * 0.4f;
                        break;
                    case 1: // (0+1) -> 2 -> 3
                        o1 = std::sin(m_previewOpPhase[1] * 6.2831853f) * gains[1];
                        o2 = std::sin((m_previewOpPhase[2] + (o0 + o1) * 1.2f) * 6.2831853f) * gains[2];
                        o3 = std::sin((m_previewOpPhase[3] + o2 * 1.5f) * 6.2831853f) * gains[3];
                        previewSample = o3 * 0.4f;
                        break;
                    case 2: // (0->1 + 2) -> 3
                        o1 = std::sin((m_previewOpPhase[1] + o0 * 1.5f) * 6.2831853f) * gains[1];
                        o2 = std::sin(m_previewOpPhase[2] * 6.2831853f) * gains[2];
                        o3 = std::sin((m_previewOpPhase[3] + (o1 + o2) * 1.2f) * 6.2831853f) * gains[3];
                        previewSample = o3 * 0.4f;
                        break;
                    case 3: // (0->1) + (0->2) -> 3
                        o1 = std::sin((m_previewOpPhase[1] + o0 * 1.5f) * 6.2831853f) * gains[1];
                        o2 = std::sin((m_previewOpPhase[2] + o0 * 1.5f) * 6.2831853f) * gains[2];
                        o3 = std::sin((m_previewOpPhase[3] + (o1 + o2) * 1.0f) * 6.2831853f) * gains[3];
                        previewSample = o3 * 0.4f;
                        break;
                    case 4: // (0->1) + (2->3)
                        o1 = std::sin((m_previewOpPhase[1] + o0 * 1.5f) * 6.2831853f) * gains[1];
                        o2 = std::sin(m_previewOpPhase[2] * 6.2831853f) * gains[2];
                        o3 = std::sin((m_previewOpPhase[3] + o2 * 1.5f) * 6.2831853f) * gains[3];
                        previewSample = (o1 + o3) * 0.28f;
                        break;
                    case 5: // 0 -> (1+2+3)
                        o1 = std::sin((m_previewOpPhase[1] + o0 * 1.5f) * 6.2831853f) * gains[1];
                        o2 = std::sin((m_previewOpPhase[2] + o0 * 1.5f) * 6.2831853f) * gains[2];
                        o3 = std::sin((m_previewOpPhase[3] + o0 * 1.5f) * 6.2831853f) * gains[3];
                        previewSample = (o1 + o2 + o3) * 0.22f;
                        break;
                    case 6: // (0->1) + 2 + 3
                        o1 = std::sin((m_previewOpPhase[1] + o0 * 1.5f) * 6.2831853f) * gains[1];
                        o2 = std::sin(m_previewOpPhase[2] * 6.2831853f) * gains[2];
                        o3 = std::sin(m_previewOpPhase[3] * 6.2831853f) * gains[3];
                        previewSample = (o1 + o2 + o3) * 0.22f;
                        break;
                    case 7: // 0 + 1 + 2 + 3
                    default:
                        o1 = std::sin(m_previewOpPhase[1] * 6.2831853f) * gains[1];
                        o2 = std::sin(m_previewOpPhase[2] * 6.2831853f) * gains[2];
                        o3 = std::sin(m_previewOpPhase[3] * 6.2831853f) * gains[3];
                        previewSample = (o0 + o1 + o2 + o3) * 0.18f;
                        break;
                }
            } else if (previewMode == 1) { // Square / Pulse
                float duty = (pParam1 > 0.05f && pParam1 < 0.95f) ? pParam1 : 0.5f;
                previewSample = (m_previewPhase < duty ? 0.3f : -0.3f);
            } else if (previewMode == 2) { // Triangle
                float p = m_previewPhase;
                previewSample = (p < 0.5f ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p)) * 0.4f;
            } else if (previewMode == 3) { // Wavetable / Custom RAM
                if (!m_previewWavetable.empty()) {
                    float pos = m_previewPhase * (float)m_previewWavetable.size();
                    size_t idx0 = (size_t)pos % m_previewWavetable.size();
                    size_t idx1 = (idx0 + 1) % m_previewWavetable.size();
                    float frac = pos - (float)(size_t)pos;
                    previewSample = (m_previewWavetable[idx0] * (1.0f - frac) + m_previewWavetable[idx1] * frac) * 0.4f;
                } else {
                    previewSample = (2.0f * m_previewPhase - 1.0f) * 0.3f;
                }
            } else if (previewMode == 4) { // Noise
                static uint16_t lfsr = 0x7FFF;
                lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & (pParam1 > 0.5f ? 0x6000 : 0x4000));
                previewSample = ((lfsr & 1) ? 0.3f : -0.3f);
            }
        }

        // PCM Sample playback
        if (!m_previewSampleBuffer.empty()) {
            if (m_previewSamplePaused.load()) {
                // Paused: do nothing, do not advance position
            } else if (m_previewSamplePosFloat < (float)m_previewSampleBuffer.size()) {
                size_t idx = (size_t)m_previewSamplePosFloat;
                previewSample += m_previewSampleBuffer[idx] * (0.7f * m_previewSampleVolume.load());
                m_previewSamplePosFloat += m_previewSampleStep;
            } else {
                // Finished playing completely - clean up so it never loops or plays unpitched
                m_previewSampleBuffer.clear();
                m_previewSamplePosFloat = 0.0f;
                m_previewSamplePos = 0;
                m_previewSamplePaused.store(false);
            }
        }

        int32_t smplL = (i < rendered) ? (int32_t)(m_renderBuf[i].L >> 8) : 0;
        int32_t smplR = (i < rendered) ? (int32_t)(m_renderBuf[i].R >> 8) : 0;

        smplL = (int32_t)(smplL * vol);
        smplR = (int32_t)(smplR * vol);

        if (previewSample != 0.0f) {
            int32_t prevInt = (int32_t)(previewSample * 32767.0f);
            smplL += prevInt;
            smplR += prevInt;
        }

        smplL = std::clamp(smplL, -32768, 32767);
        smplR = std::clamp(smplR, -32768, 32767);

        outStream[i * 2 + 0] = (int16_t)smplL;
        outStream[i * 2 + 1] = (int16_t)smplR;

        float normL = smplL / 32768.0f;
        float normR = smplR / 32768.0f;

        m_oscBufferL[oscPos] = normL;
        m_oscBufferR[oscPos] = normR;
        oscPos = (oscPos + 1) % OSC_BUFFER_SIZE;

        curPeakL = std::max(curPeakL, std::abs(normL));
        curPeakR = std::max(curPeakR, std::abs(normR));
    }

    m_oscWritePos.store(oscPos);
    m_totalOscSamples += (UINT32)frames;
    m_peakL.store(std::max(m_peakL.load(), curPeakL));
    m_peakR.store(std::max(m_peakR.load(), curPeakR));

    // Update note tracker live notes for piano roll and note badges
    if (m_playing.load() && !m_paused.load() && m_loaded.load() && m_player) {
        uint32_t rawSmpl = m_player->GetCurPos(PLAYPOS_TICK);
        uint32_t totalSmpls = m_currentInfo.totalSamples;

        size_t fifoSz = m_resampleFifo.size();
        uint32_t actualSmpl = (rawSmpl >= fifoSz) ? (rawSmpl - (uint32_t)fifoSz) : 0;

        // Check if song reached the end (libvgm signaled end-of-stream via PLAYSTATE_END)
        bool reachedEnd = ((m_player->GetState() & PLAYSTATE_END) != 0);

        float curPitchSemi = m_pitchSemitones.load();
        if (reachedEnd) {
            // For non-looping track where user explicitly enabled looping in the UI
            if (m_loop.load() && m_settings.loopMode != 2 && !m_currentInfo.hasLoop) {
                m_player->Seek(PLAYPOS_TICK, 0);
                m_curSample.store(0);
                m_resampleFifo.clear();
                m_resamplePhase = 0.0;
                m_noteTracker.UpdateLiveState(0, curPitchSemi);
                return;
            } else {
                m_playing.store(false);
                m_curSample.store(totalSmpls);
                std::memset(outStream, 0, frames * 2 * sizeof(int16_t));
                return;
            }
        }

        // Wrap playback position for display when looping normally
        uint32_t displaySmpl = actualSmpl;
        if (totalSmpls > 0 && actualSmpl >= totalSmpls) {
            if (m_currentInfo.hasLoop && m_currentInfo.loopSamples > 0 && totalSmpls > m_currentInfo.loopSamples) {
                uint32_t loopSmpls = m_currentInfo.loopSamples;
                uint32_t loopStart = totalSmpls - loopSmpls;
                displaySmpl = loopStart + ((actualSmpl - loopStart) % loopSmpls);
            } else {
                displaySmpl = actualSmpl % totalSmpls;
            }
        }
        m_curSample.store(displaySmpl);
        m_noteTracker.UpdateLiveState(displaySmpl, curPitchSemi);
    }
}

void AudioEngine::SetChannelMute(int channelIdx, bool mute) {
    auto& channels = m_noteTracker.GetChannels();
    if (channelIdx >= 0 && channelIdx < (int)channels.size()) {
        channels[channelIdx].muted = mute;
        channels[channelIdx].visible = !mute;
        if (mute) {
            channels[channelIdx].solo = false;
        }
        UpdateChannelMuting();
    }
}

void AudioEngine::SetChannelSolo(int channelIdx, bool solo) {
    auto& channels = m_noteTracker.GetChannels();
    if (channelIdx >= 0 && channelIdx < (int)channels.size()) {
        channels[channelIdx].solo = solo;
        if (solo) {
            channels[channelIdx].muted = false;
            channels[channelIdx].visible = true;
        }
        UpdateChannelMuting();
    }
}

void AudioEngine::ToggleChannelMute(int channelIdx) {
    auto& channels = m_noteTracker.GetChannels();
    if (channelIdx >= 0 && channelIdx < (int)channels.size()) {
        SetChannelMute(channelIdx, !channels[channelIdx].muted);
    }
}

void AudioEngine::ToggleChannelSolo(int channelIdx) {
    auto& channels = m_noteTracker.GetChannels();
    if (channelIdx >= 0 && channelIdx < (int)channels.size()) {
        SetChannelSolo(channelIdx, !channels[channelIdx].solo);
    }
}

void AudioEngine::MuteAllChannels(bool mute) {
    auto& channels = m_noteTracker.GetChannels();
    for (auto& ch : channels) {
        ch.muted = mute;
        ch.visible = !mute;
        if (mute) {
            ch.solo = false;
        }
    }
    UpdateChannelMuting();
}

void AudioEngine::ClearAllSolo() {
    auto& channels = m_noteTracker.GetChannels();
    for (auto& ch : channels) {
        ch.solo = false;
    }
    UpdateChannelMuting();
}

void AudioEngine::UpdateChannelMuting() {
    if (!m_loaded.load() || !m_player) return;

    if (m_deviceId != 0) SDL_LockAudioDevice(m_deviceId);

    auto& channels = m_noteTracker.GetChannels();
    bool anySolo = false;
    for (const auto& ch : channels) {
        if (ch.solo) { anySolo = true; break; }
    }

    // Key: (chipType | (chipInstance << 8)) -> PLR_MUTE_OPTS
    std::map<uint32_t, PLR_MUTE_OPTS> muteMap;

    for (const auto& ch : channels) {
        uint32_t key = (uint32_t)ch.chipType | ((uint32_t)ch.chipInstance << 8);
        if (muteMap.find(key) == muteMap.end()) {
            PLR_MUTE_OPTS mOpts{};
            mOpts.disable = 0;
            mOpts.chnMute[0] = 0;
            mOpts.chnMute[1] = 0;
            muteMap[key] = mOpts;
        }

        bool isMuted = false;
        if (anySolo) {
            isMuted = !ch.solo;
        } else {
            isMuted = ch.muted;
        }

        if (isMuted) {
            int linkIdx = (ch.linkIndex > 0) ? 1 : 0;
            muteMap[key].chnMute[linkIdx] |= (1U << ch.channelInChip);
        }
    }

    for (const auto& [key, mOpts] : muteMap) {
        uint8_t chipType = key & 0xFF;
        uint8_t chipInst = (key >> 8) & 0xFF;
        m_player->SetDeviceMuting(PLR_DEV_ID(chipType, chipInst), mOpts);
    }

    if (m_deviceId != 0) SDL_UnlockAudioDevice(m_deviceId);
}

const uint8_t* AudioEngine::GetVgmData() const {
    return m_dLoad ? (const uint8_t*)DataLoader_GetData(m_dLoad) : nullptr;
}

size_t AudioEngine::GetVgmDataSize() const {
    return m_dLoad ? (size_t)DataLoader_GetSize(m_dLoad) : 0;
}

void AudioEngine::StartPreview(float freq, int mode, float param1, float param2) {
    m_previewFreq.store(freq);
    m_previewMode.store(mode);
    m_previewParam1.store(param1);
    m_previewParam2.store(param2);
    m_previewActive.store(true);
}

void AudioEngine::StopPreview() {
    m_previewActive.store(false);
}

void AudioEngine::PlayPreviewSampleBuffer(const std::vector<float>& pcmData) {
    PlayPreviewSamplePitched(pcmData, 1.0f);
}

void AudioEngine::PlayPreviewSamplePitched(const std::vector<float>& pcmData, float pitchRatio, size_t startOffset) {
    if (m_deviceId != 0) SDL_LockAudioDevice(m_deviceId);
    m_previewSampleBuffer = pcmData;
    m_previewSamplePosFloat = (float)startOffset;
    m_previewSamplePos = startOffset;
    m_previewSampleStep = (pitchRatio > 0.05f) ? pitchRatio : 1.0f;
    m_previewSamplePaused.store(false);
    if (m_deviceId != 0) SDL_UnlockAudioDevice(m_deviceId);
}

void AudioEngine::PausePreviewSample() {
    m_previewSamplePaused.store(true);
}

void AudioEngine::ResumePreviewSample() {
    m_previewSamplePaused.store(false);
}

void AudioEngine::StopPreviewSample() {
    if (m_deviceId != 0) SDL_LockAudioDevice(m_deviceId);
    m_previewSampleBuffer.clear();
    m_previewSamplePosFloat = 0.0f;
    m_previewSamplePos = 0;
    m_previewSamplePaused.store(false);
    if (m_deviceId != 0) SDL_UnlockAudioDevice(m_deviceId);
}

bool AudioEngine::IsPreviewSamplePlaying() const {
    return !m_previewSampleBuffer.empty() && 
           (m_previewSamplePosFloat < (float)m_previewSampleBuffer.size()) && 
           !m_previewSamplePaused.load();
}

bool AudioEngine::IsPreviewSamplePaused() const {
    return !m_previewSampleBuffer.empty() && 
           (m_previewSamplePosFloat < (float)m_previewSampleBuffer.size()) && 
           m_previewSamplePaused.load();
}

void AudioEngine::SetPreviewSampleVolume(float vol) {
    m_previewSampleVolume.store(std::clamp(vol, 0.0f, 1.5f));
}

float AudioEngine::GetPreviewSampleVolume() const {
    return m_previewSampleVolume.load();
}

float AudioEngine::GetPreviewSampleProgress() const {
    if (m_previewSampleBuffer.empty()) return 0.0f;
    float p = m_previewSamplePosFloat / (float)m_previewSampleBuffer.size();
    return (p > 1.0f) ? 1.0f : (p < 0.0f ? 0.0f : p);
}

void AudioEngine::SetPreviewWavetable(const std::vector<float>& wavetable) {
    if (m_deviceId != 0) SDL_LockAudioDevice(m_deviceId);
    m_previewWavetable = wavetable;
    if (m_deviceId != 0) SDL_UnlockAudioDevice(m_deviceId);
}

void AudioEngine::SetPreviewFmParams(int alg, int fb, float mult1, float tl1, float mult2, float tl2, float mult3, float tl3, float mult4, float tl4) {
    if (m_deviceId != 0) SDL_LockAudioDevice(m_deviceId);
    m_previewFmAlg = std::clamp(alg, 0, 7);
    m_previewFmFb = std::clamp(fb, 0, 7);
    m_previewOpMult[0] = mult1; m_previewOpTL[0] = tl1;
    m_previewOpMult[1] = mult2; m_previewOpTL[1] = tl2;
    m_previewOpMult[2] = mult3; m_previewOpTL[2] = tl3;
    m_previewOpMult[3] = mult4; m_previewOpTL[3] = tl4;
    for (int i = 0; i < 4; ++i) {
        m_previewOpPhase[i] = 0.0f;
    }
    if (m_deviceId != 0) SDL_UnlockAudioDevice(m_deviceId);
}

} // namespace vgmstudio
