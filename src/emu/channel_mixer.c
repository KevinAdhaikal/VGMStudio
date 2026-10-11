#include "channel_mixer.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

#define CM_MASK (CM_WAVE_SIZE - 1)

typedef struct {
    float vol;
    float pan;
    float gainL;
    float gainR;
    float waveBuffer[CM_WAVE_SIZE];
    int wavePos;
    uint64_t totalSamples;
    uint64_t lastTriggerSample;
    uint64_t lastTotalSamples;
    float peakTracker;
} CmChannel;

static CmChannel s_channels[CM_MAX_CHIPS][CM_MAX_INSTANCES][CM_MAX_CHANNELS];
static int s_initialized = 0;

static void CalcGains(CmChannel* ch) {
    float vol = ch->vol;
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;

    float pan = ch->pan;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;

    float left = 1.0f;
    float right = 1.0f;

    if (pan < 0.0f) {
        right = 1.0f + pan;
    } else if (pan > 0.0f) {
        left = 1.0f - pan;
    }

    ch->gainL = vol * left;
    ch->gainR = vol * right;
}

void ChannelMixer_Reset(void) {
    for (int c = 0; c < CM_MAX_CHIPS; ++c) {
        for (int inst = 0; inst < CM_MAX_INSTANCES; ++inst) {
            for (int ch = 0; ch < CM_MAX_CHANNELS; ++ch) {
                s_channels[c][inst][ch].vol = 1.0f;
                s_channels[c][inst][ch].pan = 0.0f;
                s_channels[c][inst][ch].gainL = 1.0f;
                s_channels[c][inst][ch].gainR = 1.0f;
                memset(s_channels[c][inst][ch].waveBuffer, 0, sizeof(s_channels[c][inst][ch].waveBuffer));
                s_channels[c][inst][ch].wavePos = 0;
                s_channels[c][inst][ch].totalSamples = 0;
                s_channels[c][inst][ch].lastTriggerSample = 0;
                s_channels[c][inst][ch].lastTotalSamples = 0;
                s_channels[c][inst][ch].peakTracker = 1.0f;
            }
        }
    }
    s_initialized = 1;
}

void ChannelMixer_ResetWaveforms(void) {
    if (!s_initialized) return;
    for (int c = 0; c < CM_MAX_CHIPS; ++c) {
        for (int inst = 0; inst < CM_MAX_INSTANCES; ++inst) {
            for (int ch = 0; ch < CM_MAX_CHANNELS; ++ch) {
                memset(s_channels[c][inst][ch].waveBuffer, 0, sizeof(s_channels[c][inst][ch].waveBuffer));
                s_channels[c][inst][ch].wavePos = 0;
                s_channels[c][inst][ch].totalSamples = 0;
                s_channels[c][inst][ch].lastTriggerSample = 0;
                s_channels[c][inst][ch].lastTotalSamples = 0;
                s_channels[c][inst][ch].peakTracker = 1.0f;
            }
        }
    }
}

void ChannelMixer_Set(int chipType, int chipInst, int channel, float vol, float pan) {
    if (!s_initialized) ChannelMixer_Reset();
    if (chipType < 0 || chipType >= CM_MAX_CHIPS) return;
    if (chipInst < 0 || chipInst >= CM_MAX_INSTANCES) return;
    if (channel < 0 || channel >= CM_MAX_CHANNELS) return;

    CmChannel* ch = &s_channels[chipType][chipInst][channel];
    ch->vol = vol;
    ch->pan = pan;
    CalcGains(ch);
}

void ChannelMixer_Get(int chipType, int chipInst, int channel, float* outVol, float* outPan) {
    if (!s_initialized) ChannelMixer_Reset();
    if (chipType < 0 || chipType >= CM_MAX_CHIPS ||
        chipInst < 0 || chipInst >= CM_MAX_INSTANCES ||
        channel < 0 || channel >= CM_MAX_CHANNELS) {
        if (outVol) *outVol = 1.0f;
        if (outPan) *outPan = 0.0f;
        return;
    }

    const CmChannel* ch = &s_channels[chipType][chipInst][channel];
    if (outVol) *outVol = ch->vol;
    if (outPan) *outPan = ch->pan;
}

void ChannelMixer_GetGains(int chipType, int chipInst, int channel, float* gainL, float* gainR) {
    if (!s_initialized) {
        if (gainL) *gainL = 1.0f;
        if (gainR) *gainR = 1.0f;
        return;
    }
    if (chipType < 0 || chipType >= CM_MAX_CHIPS ||
        chipInst < 0 || chipInst >= CM_MAX_INSTANCES ||
        channel < 0 || channel >= CM_MAX_CHANNELS) {
        if (gainL) *gainL = 1.0f;
        if (gainR) *gainR = 1.0f;
        return;
    }

    const CmChannel* ch = &s_channels[chipType][chipInst][channel];
    if (gainL) *gainL = ch->gainL;
    if (gainR) *gainR = ch->gainR;
}

void ChannelMixer_PushSample(int chipType, int chipInst, int channel, float sample) {
    if (!s_initialized) ChannelMixer_Reset();
    if (chipType < 0 || chipType >= CM_MAX_CHIPS) return;
    if (chipInst < 0 || chipInst >= CM_MAX_INSTANCES) return;
    if (channel < 0 || channel >= CM_MAX_CHANNELS) return;

    CmChannel* ch = &s_channels[chipType][chipInst][channel];
    ch->waveBuffer[ch->wavePos] = sample;
    ch->wavePos = (ch->wavePos + 1) & CM_MASK;
    ch->totalSamples++;
}

void ChannelMixer_GetWaveform(int chipType, int chipInst, int channel, float* outBuf, int count) {
    if (!outBuf || count <= 0) return;
    if (!s_initialized || chipType < 0 || chipType >= CM_MAX_CHIPS ||
        chipInst < 0 || chipInst >= CM_MAX_INSTANCES ||
        channel < 0 || channel >= CM_MAX_CHANNELS) {
        memset(outBuf, 0, count * sizeof(float));
        return;
    }

    CmChannel* ch = &s_channels[chipType][chipInst][channel];
    uint64_t curTotal = ch->totalSamples;
    int wPos = ch->wavePos;

    // Latency compensation: read 640 samples (~14.5ms) behind the audio write head.
    // This perfectly synchronizes what is displayed with what is heard through the speakers,
    // and ensures the render thread reads from a quiescent region with ZERO race conditions.
    int latency = 640;
    if ((int)curTotal < latency + count + 1200) {
        memset(outBuf, 0, count * sizeof(float));
        return;
    }

    uint64_t targetSample = curTotal - latency;
    int targetIdx = (wPos - latency + CM_WAVE_SIZE) & CM_MASK;

    // Scan window of 1200 samples for amplitude envelope and DC midpoint
    int scanLen = 1200;
    float minVal = 1e9f;
    float maxVal = -1e9f;
    for (int i = 0; i < scanLen; ++i) {
        int idx = (targetIdx - i + CM_WAVE_SIZE) & CM_MASK;
        float v = ch->waveBuffer[idx];
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
    }

    float peakToPeak = maxVal - minVal;

    // Silence threshold: If inactive or near-zero, return clean empty baseline
    if (peakToPeak < 0.005f) {
        memset(outBuf, 0, count * sizeof(float));
        ch->lastTriggerSample = 0;
        ch->lastTotalSamples = curTotal;
        return;
    }

    float midLevel = 0.5f * (minVal + maxVal);
    // Noise floor threshold: 0.1% of peak-to-peak accommodates all frequencies down to 20 Hz
    float hystThreshold = peakToPeak * 0.001f;
    if (hystThreshold < 0.0001f) hystThreshold = 0.0001f;

    // Smooth peak tracking (decay by ~2% per frame to preserve ADSR dynamics without pumping)
    float curHalf = peakToPeak * 0.5f;
    if (curHalf > ch->peakTracker) {
        ch->peakTracker = curHalf; // Instant attack
    } else {
        ch->peakTracker = ch->peakTracker * 0.98f + curHalf * 0.02f; // Smooth decay
    }
    if (ch->peakTracker < 1.0f) ch->peakTracker = 1.0f;

    // Collect all rising zero-crossings in search window [targetSample - scanLen, targetSample]
    typedef struct {
        uint64_t sample;
        float slope;
        float frac;
    } Crossing;

    Crossing crossings[128];
    int crossingCount = 0;
    float maxSlope = 0.0f;

    for (int s = scanLen - 1; s >= 0 && crossingCount < 128; --s) {
        uint64_t curS = targetSample - s;
        int idx1 = (int)(curS & CM_MASK);
        int idx2 = (int)((curS + 1) & CM_MASK);
        float v1 = ch->waveBuffer[idx1];
        float v2 = ch->waveBuffer[idx2];

        if (v1 <= midLevel && v2 > midLevel) {
            float slope = v2 - v1;
            if (slope >= hystThreshold) {
                if (slope > maxSlope) maxSlope = slope;
                float frac = (midLevel - v1) / slope;
                if (frac < 0.0f) frac = 0.0f;
                if (frac > 1.0f) frac = 1.0f;
                crossings[crossingCount].sample = curS;
                crossings[crossingCount].slope = slope;
                crossings[crossingCount].frac = frac;
                crossingCount++;
            }
        }
    }

    Crossing bestCrossing;
    bestCrossing.sample = targetSample - count / 2;
    bestCrossing.slope = 0.0f;
    bestCrossing.frac = 0.0f;

    if (crossingCount > 0) {
        // Filter to primary crossings (slope >= 0.65 * maxSlope) to reject secondary harmonic ripples
        float slopeThreshold = maxSlope * 0.65f;
        Crossing primary[128];
        int primaryCount = 0;
        for (int c = 0; c < crossingCount; ++c) {
            if (crossings[c].slope >= slopeThreshold) {
                primary[primaryCount++] = crossings[c];
            }
        }
        if (primaryCount == 0) {
            memcpy(primary, crossings, crossingCount * sizeof(Crossing));
            primaryCount = crossingCount;
        }

        // Anchor to expected phase continuation: lastTriggerSample + elapsed_samples
        uint64_t anchor = targetSample - count / 2;
        if (ch->lastTriggerSample > 0 && ch->lastTotalSamples > 0 && curTotal >= ch->lastTotalSamples) {
            uint64_t elapsed = curTotal - ch->lastTotalSamples;
            anchor = ch->lastTriggerSample + elapsed;
        }

        // Select primary crossing closest to anchor
        int64_t bestDist = 0x7FFFFFFFFFFFFFFFLL;
        for (int c = 0; c < primaryCount; ++c) {
            int64_t dist = (int64_t)primary[c].sample - (int64_t)anchor;
            if (dist < 0) dist = -dist;
            if (dist < bestDist) {
                bestDist = dist;
                bestCrossing = primary[c];
            }
        }
    }

    ch->lastTriggerSample = bestCrossing.sample;
    ch->lastTotalSamples = curTotal;

    // Sub-sample linear interpolation centered around bestCrossing
    float invScale = 1.0f / ch->peakTracker;
    int halfCount = count / 2;
    float bestFrac = bestCrossing.frac;
    uint64_t bestS = bestCrossing.sample;

    for (int i = 0; i < count; ++i) {
        int offset = i - halfCount;
        uint64_t smpl = bestS + offset;
        int i1 = (int)(smpl & CM_MASK);
        int i2 = (int)((smpl + 1) & CM_MASK);

        float s1 = ch->waveBuffer[i1];
        float s2 = ch->waveBuffer[i2];
        float interp = s1 + (s2 - s1) * bestFrac;

        outBuf[i] = (interp - midLevel) * invScale;
    }
}
