#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

class PlayerBase;

namespace vgmstudio {

// Represents a single note event on a specific channel
struct TrackedNote {
    int channelId = 0;          // Index into NoteTracker::GetChannels()
    uint32_t startSample = 0;   // In 44.1 kHz samples
    uint32_t endSample = 0;     // In 44.1 kHz samples
    float midiNote = 60.0f;     // Fractional MIDI note (e.g. 60.0 = C4, 69.0 = A440)
    int noteNumber = 60;        // Rounded MIDI note (0 to 127)
    float volume = 0.8f;        // 0.0 to 1.0
    char noteName[8] = "C-4";   // "C-4", "F#5", etc.
};

// Represents a sound channel detected from the VGM file
struct TrackedChannel {
    int id = 0;                 // Unique sequential channel ID
    std::string chipName;       // e.g. "YM2612", "SN76489", "NES APU"
    std::string name;           // e.g. "FM 1", "Pulse 1", "Noise"
    std::string type;           // "FM", "Square", "Triangle", "Wave", "Noise", "PCM"
    uint32_t color = 0;         // ImU32 RGBA color
    bool visible = true;        // Show/hide in piano roll
    bool solo = false;          // Solo in piano roll / audio
    bool muted = false;         // Mute in piano roll / audio

    // Hardware chip mapping for real audio muting
    int chipType = 0;           // libvgm device ID (e.g. DEVID_YM2612)
    int chipInstance = 0;       // 0 or 1 (first or second chip)
    int channelInChip = 0;      // 0, 1, 2, ...
    int linkIndex = 0;          // 0 = primary device, 1 = linked sub-device (e.g. SSG)

    // Live state at current playhead position
    bool active = false;
    float curMidiNote = 0.0f;
    float curVolume = 0.0f;
    char curNoteName[8] = "---";

    // Channel mixer parameters (controlled via VOL & PAN knobs)
    float volume = 1.0f;        // 0.0f to 1.0f (default 100%)
    float pan = 0.0f;           // -1.0f (L) to +1.0f (R), 0.0f = Center
};

class NoteTracker {
public:
    NoteTracker();
    ~NoteTracker();

    // Reset all notes and channels
    void Clear();

    // Parse the entire VGM file into tracked notes and channel metadata
    bool ProcessVgm(const uint8_t* vgmData, size_t dataSize, uint32_t sampleRate = 44100, PlayerBase* player = nullptr);

    // Update real-time live channel states (active note, pitch, volume) for the current playback position
    void UpdateLiveState(uint32_t curSample, float pitchShiftSemitones = 0.0f);

    // Get note & channel lists
    const std::vector<TrackedChannel>& GetChannels() const { return m_channels; }
    std::vector<TrackedChannel>& GetChannels() { return m_channels; }
    const std::vector<TrackedNote>& GetNotes() const { return m_notes; }
    const std::vector<TrackedNote>& GetChannelNotes(size_t ch) const {
        if (ch < m_channelNotes.size()) return m_channelNotes[ch];
        static const std::vector<TrackedNote> empty;
        return empty;
    }

    // Fast range query for rendering visible notes within a time window [startSample, endSample]
    // Returns indices of notes that intersect the window
    void QueryNotesInWindow(uint32_t startSample, uint32_t endSample, std::vector<const TrackedNote*>& outNotes) const;

    // Pitch range of song
    int GetMinMidiNote() const { return m_minMidiNote; }
    int GetMaxMidiNote() const { return m_maxMidiNote; }
    uint32_t GetTotalSamples() const { return m_totalSamples; }
    size_t GetTotalNoteCount() const { return m_notes.size(); }

    // Helpers
    static float FreqToMidi(float freq);
    static void MidiToNoteName(float midiNote, char* outStr, size_t maxLen);
    static uint32_t GetChannelColor(const std::string& chip, const std::string& type, int chIndex);

private:
    std::vector<TrackedChannel> m_channels;
    std::vector<TrackedNote> m_notes;
    
    // Per-channel note lists sorted by startSample for fast binary search
    std::vector<std::vector<TrackedNote>> m_channelNotes;

    int m_minMidiNote = 36; // C2
    int m_maxMidiNote = 84; // C6
    uint32_t m_totalSamples = 0;

    // Helper during parsing to track currently active (open) notes
    struct OpenNote {
        bool active = false;
        uint32_t startSample = 0;
        float midiNote = 60.0f;
        float volume = 0.8f;
    };
    std::vector<OpenNote> m_openNotes;

    void NoteOn(int channelId, uint32_t currentSample, float midiNote, float volume);
    void NoteOff(int channelId, uint32_t currentSample);
    void FlushOpenNotes(uint32_t finalSample);
};

} // namespace vgmstudio
