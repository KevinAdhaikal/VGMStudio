#ifndef CHANNEL_MIXER_H
#define CHANNEL_MIXER_H

#ifdef __cplusplus
extern "C" {
#endif

// Maximum chips and channels supported
#define CM_MAX_CHIPS 64
#define CM_MAX_INSTANCES 2
#define CM_MAX_CHANNELS 32
#define CM_WAVE_SIZE 4096

// Set volume (0.0f - 1.0f) and pan (-1.0f to +1.0f) for a specific chip/channel
void ChannelMixer_Set(int chipType, int chipInst, int channel, float vol, float pan);

// Get current volume and pan settings
void ChannelMixer_Get(int chipType, int chipInst, int channel, float* outVol, float* outPan);

// Get calculated left and right gains for audio rendering
void ChannelMixer_GetGains(int chipType, int chipInst, int channel, float* gainL, float* gainR);

// Push a single output sample from an emulated chip voice
void ChannelMixer_PushSample(int chipType, int chipInst, int channel, float sample);

// Retrieve latest waveform buffer for an emulated chip voice (stable centered trigger)
void ChannelMixer_GetWaveform(int chipType, int chipInst, int channel, float* outBuf, int count);

// Clear waveform display buffers without altering volume/pan
void ChannelMixer_ResetWaveforms(void);

// Reset all channels to default (vol = 1.0f, pan = 0.0f)
void ChannelMixer_Reset(void);

#ifdef __cplusplus
}
#endif

#endif // CHANNEL_MIXER_H
