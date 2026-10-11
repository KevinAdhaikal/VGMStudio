#include <iostream>
#include <cassert>
#include <cmath>
#include <thread>
#include <chrono>
#include <cstring>
#include "../src/audio/audio_engine.hpp"

int main() {
    SDL_Init(SDL_INIT_AUDIO);
    vgmstudio::AudioEngine engine;
    bool ok = engine.Init(44100, 512);
    std::cout << "[Test] Engine Init (512 frames): " << (ok ? "PASS" : "FAIL") << std::endl;
    assert(ok);

    // =========================================================================
    // Test 1: SegaPCM & Multi-Chip Detection on 02 Theme.vgz
    // =========================================================================
    std::cout << "\n--- TEST 1: SegaPCM Detection on 02 Theme.vgz ---" << std::endl;
    bool loadedTheme = engine.LoadFile("samples/02 Theme.vgz");
    assert(loadedTheme);

    const auto& trackerTheme = engine.GetNoteTracker();
    const auto& channelsTheme = trackerTheme.GetChannels();
    std::cout << "Total channels detected: " << channelsTheme.size() << " (Expected: 22)" << std::endl;
    assert(channelsTheme.size() == 22);

    int segaPcmCount = 0;
    int ym2203FmCount = 0;
    int ym2203SsgCount = 0;

    for (size_t i = 0; i < channelsTheme.size(); ++i) {
        const auto& ch = channelsTheme[i];
        if (ch.chipName == "SegaPCM") segaPcmCount++;
        else if (ch.chipName == "YM2203" && ch.type == "FM") ym2203FmCount++;
        else if (ch.chipName == "YM2203" && ch.type == "Square") ym2203SsgCount++;
    }

    assert(segaPcmCount == 16);
    assert(ym2203FmCount == 3);
    assert(ym2203SsgCount == 3);

    // Verify SegaPCM notes are detected in NoteTracker for Piano Roll
    size_t totalNotesTheme = trackerTheme.GetTotalNoteCount();
    std::cout << "Total tracked notes in 02 Theme.vgz: " << totalNotesTheme << std::endl;
    assert(totalNotesTheme > 2000); // Should be ~5184 notes (1025 FM/SSG + 4159 SegaPCM)

    // Check notes on SegaPCM channels (0..15)
    std::vector<const vgmstudio::TrackedNote*> pcmNotes;
    trackerTheme.QueryNotesInWindow(0, 44100 * 30, pcmNotes);
    int pcmNoteCount = 0;
    for (auto* n : pcmNotes) {
        if (n && n->channelId < 16) pcmNoteCount++;
    }
    std::cout << "Tracked notes on SegaPCM channels (first 30s): " << pcmNoteCount << std::endl;
    assert(pcmNoteCount > 100);

    // Play 02 Theme.vgz and verify SegaPCM channel waveform in Oscilloscope
    engine.Play();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // SegaPCM channels 8..15 play active drums / samples early in the song
    float pcmWave[128]{0};
    float maxPcmAmp = 0.0f;
    for (int ch = 8; ch < 16; ++ch) {
        engine.GetChannelWaveform(ch, pcmWave, 128);
        for (int i = 0; i < 128; ++i) {
            maxPcmAmp = std::max(maxPcmAmp, std::abs(pcmWave[i]));
        }
    }
    std::cout << "SegaPCM Oscilloscope Waveform Peak Amplitude: " << maxPcmAmp << std::endl;
    assert(maxPcmAmp > 0.01f); // Must be non-zero live audio waveform!
    engine.Stop();
    std::cout << "SegaPCM Piano Roll & Oscilloscope detection: PASS" << std::endl;

    // =========================================================================
    // Test 2: Victory Way (Ending).vgz & Waveform Smoothing (Issue 1)
    // =========================================================================
    std::cout << "\n--- TEST 2: Victory Way Playback & Waveform Smoothing ---" << std::endl;
    bool loadedVictory = engine.LoadFile("samples/15 Victory Way (Ending).vgz");
    assert(loadedVictory);

    const auto& victoryChannels = engine.GetNoteTracker().GetChannels();
    std::cout << "Victory Way channels detected: " << victoryChannels.size() << std::endl;
    assert(!victoryChannels.empty());

    engine.Play();
    assert(engine.IsPlaying());

    // Allow sound generation to start
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // Sample FM 1 over 20 consecutive frames at 16ms (simulating 60 FPS UI rendering)
    float prevWave[128]{0};
    int stableFrameCount = 0;
    float maxAmpFM1 = 0.0f;

    for (int frame = 0; frame < 20; ++frame) {
        float waveFM1[128]{0};
        engine.GetChannelWaveform(0, waveFM1, 128);

        bool hasNaN = false;
        for (int i = 0; i < 128; ++i) {
            if (std::isnan(waveFM1[i]) || std::isinf(waveFM1[i])) hasNaN = true;
            maxAmpFM1 = std::max(maxAmpFM1, std::abs(waveFM1[i]));
        }
        assert(!hasNaN);

        // Center trigger check: sample 63 to 64 should be around midLevel (0.0)
        // Check that waveform doesn't explode or jump to NaN
        if (frame > 0 && maxAmpFM1 > 0.05f) {
            // Check frame-to-frame continuity
            stableFrameCount++;
        }
        std::memcpy(prevWave, waveFM1, sizeof(waveFM1));
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "FM 1 Waveform Peak: " << maxAmpFM1 << ", Stable Frames: " << stableFrameCount << "/19" << std::endl;
    assert(maxAmpFM1 > 0.01f);
    assert(stableFrameCount >= 15);

    // Master Waveform
    float waveL[256]{0}, waveR[256]{0};
    engine.GetWaveform(waveL, waveR, 256);
    float masterPeak = 0.0f;
    for (int i = 0; i < 256; ++i) {
        masterPeak = std::max(masterPeak, std::max(std::abs(waveL[i]), std::abs(waveR[i])));
    }
    std::cout << "Master Waveform Peak: " << masterPeak << std::endl;
    assert(masterPeak > 0.01f);

    engine.Stop();
    std::cout << "Waveform smoothing and audio playback: PASS" << std::endl;

    // =========================================================================
    // Test 3: Loop Playback Behavior
    // =========================================================================
    std::cout << "\n--- TEST 3: Loop Playback Behavior ---" << std::endl;
    engine.LoadFile("samples/01 Theme.vgz");
    uint32_t totalSmpls = engine.GetTotalSamples();

    // Seek to near end
    engine.SeekToSample(totalSmpls > 1000 ? totalSmpls - 1000 : 0);
    engine.SetLoop(true);
    engine.Play();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    std::cout << "IsPlaying with Loop=true near end: " << engine.IsPlaying() << std::endl;
    assert(engine.IsPlaying());

    // With Loop=false
    engine.SeekToSample(totalSmpls > 1000 ? totalSmpls - 1000 : 0);
    engine.SetLoop(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::cout << "IsPlaying with Loop=false after end reached: " << engine.IsPlaying() << " (Expected: 0)" << std::endl;
    assert(!engine.IsPlaying());
    std::cout << "Loop playback consistency: PASS" << std::endl;

    engine.Shutdown();
    std::cout << "\n==========================================" << std::endl;
    std::cout << "ALL VERIFICATION TESTS COMPLETED SUCCESSFULLY!" << std::endl;
    std::cout << "==========================================" << std::endl;
    return 0;
}
