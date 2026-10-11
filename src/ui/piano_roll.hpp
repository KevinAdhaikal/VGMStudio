#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <imgui.h>
#include "../audio/audio_engine.hpp"

namespace vgmstudio {

enum class VisualMode {
    Horizontal,
    Waterfall
};

class PianoRoll {
public:
    PianoRoll();
    ~PianoRoll();

    // Render the Piano Roll visualizer window
    void Render(AudioEngine& audioEngine, bool* pOpen = nullptr);

    // Reset view position (e.g. when loading a new song)
    void ResetView(const NoteTracker& tracker);

    // Rewind view scroll to the start
    void RewindToStart() { m_scrollX = 0.0f; }

    // Settings
    bool& GetShowKeyLabels() { return m_showKeyLabels; }
    bool& GetShowNoteNames() { return m_showNoteNames; }
    VisualMode GetVisualMode() const { return m_visualMode; }
    void SetVisualMode(VisualMode mode) { m_visualMode = mode; }

private:
    void RenderToolbar(AudioEngine& audioEngine, NoteTracker& tracker);
    void RenderRuler(ImVec2 rulerP0, ImVec2 rulerP1, float totalWidth, uint32_t totalSamples, AudioEngine& audioEngine);
    void RenderKeyboard(ImDrawList* drawList, ImVec2 kbP0, ImVec2 kbP1, const NoteTracker& tracker, AudioEngine& audioEngine);
    void RenderNotesAndGrid(ImDrawList* drawList, ImVec2 gridP0, ImVec2 gridP1, const NoteTracker& tracker, AudioEngine& audioEngine);
    void RenderScrollbars(ImVec2 gridP0, ImVec2 gridP1, float songTotalWidth, float totalPitchHeight);
    void RenderWaterfall(ImDrawList* drawList, ImVec2 canvasP0, ImVec2 canvasP1, NoteTracker& tracker, AudioEngine& audioEngine);

    // View state
    VisualMode m_visualMode = VisualMode::Horizontal;
    float m_scrollX = 0.0f;     // Horizontal scroll in pixels
    float m_scrollY = 0.0f;     // Vertical scroll in pixels
    float m_zoomX = 1.0f;       // Horizontal zoom factor (pixels per second or unit)
    float m_keyHeight = 16.0f;  // Height of each piano key/pitch row (pixels)

    bool m_followPlayhead = true;
    bool m_showKeyLabels = true;
    bool m_showNoteNames = true;

    // Internal cache for pitch range (C1 to C8 default)
    int m_minKey = 24;  // C1
    int m_maxKey = 108; // C8
    int m_totalKeys = 85;

    // Drag / Pan / Scrub interaction state
    bool m_isPanning = false;
    bool m_isScrubbing = false;
    bool m_rulerWasPlaying = false;
    float m_scrubAnchorX = -1.0f;
    uint32_t m_lastScrubbedSample = 0;
    ImVec2 m_lastMousePos{0, 0};

    // Scrollbar drag states
    bool m_isDraggingThumbX = false;
    bool m_isDraggingThumbY = false;

    // Waterfall settings & scrolling
    float m_waterfallTimeSpan = 3.5f; // Seconds of upcoming notes in waterfall view
    float m_waterfallZoomX = 1.0f;    // Horizontal zoom factor for waterfall keys
    float m_waterfallScrollX = 0.0f;  // Horizontal scroll in waterfall mode
    bool m_isDraggingWaterfallThumbX = false;
    float m_waterfallKeyHold[128]{0.0f};
    uint32_t m_waterfallKeyHoldColor[128]{0};
};

} // namespace vgmstudio
