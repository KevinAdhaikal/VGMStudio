#include "piano_roll.hpp"
#include "icons.hpp"
#include "../app/theme.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace vgmstudio {

static const char* const NOTE_LABELS[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static inline bool IsBlackKey(int noteNum) {
    int m = (noteNum % 12 + 12) % 12;
    return (m == 1 || m == 3 || m == 6 || m == 8 || m == 10);
}

static void DrawCircularResetArrow(ImDrawList* dl, ImVec2 center, float radius, ImU32 color, float thickness = 1.6f) {
    const int segments = 16;
    float startAngle = 0.35f * 3.14159265f;
    float endAngle = 2.15f * 3.14159265f;
    dl->PathArcTo(center, radius, startAngle, endAngle, segments);
    dl->PathStroke(color, 0, thickness);

    float tipX = center.x + radius * std::cos(startAngle);
    float tipY = center.y + radius * std::sin(startAngle);
    float dirX = -std::sin(startAngle);
    float dirY = std::cos(startAngle);
    float normX = -dirY;
    float normY = dirX;
    float arrowLen = 3.6f;
    float arrowW = 2.6f;

    ImVec2 p0(tipX + dirX * 1.5f, tipY + dirY * 1.5f);
    ImVec2 p1(tipX - dirX * arrowLen + normX * arrowW, tipY - dirY * arrowLen + normY * arrowW);
    ImVec2 p2(tipX - dirX * arrowLen - normX * arrowW, tipY - dirY * arrowLen - normY * arrowW);
    dl->AddTriangleFilled(p0, p1, p2, color);
}

PianoRoll::PianoRoll() {
    m_scrollY = -1.0f;
}

PianoRoll::~PianoRoll() {
}

void PianoRoll::ResetView(const NoteTracker& tracker) {
    m_minKey = 24;  // C1
    m_maxKey = 108; // C8
    if (tracker.GetTotalNoteCount() > 0) {
        if (tracker.GetMinMidiNote() < m_minKey) m_minKey = std::max(12, tracker.GetMinMidiNote() - 2);
        if (tracker.GetMaxMidiNote() > m_maxKey) m_maxKey = std::min(127, tracker.GetMaxMidiNote() + 2);
    }
    m_totalKeys = m_maxKey - m_minKey + 1;
    m_scrollX = 0.0f;
    m_scrollY = -1.0f;
    m_zoomX = 1.0f;
    m_waterfallZoomX = 1.0f;
    m_waterfallScrollX = 0.0f;
}

void PianoRoll::Render(AudioEngine& audioEngine, bool* pOpen) {
    ImGui::SetNextWindowSize(ImVec2(800, 450), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Piano Roll", pOpen, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return;
    }

    NoteTracker& tracker = audioEngine.GetNoteTracker();
    uint32_t curSample = audioEngine.GetCurSample();

    // Update real-time channel notes
    tracker.UpdateLiveState(curSample, audioEngine.GetPitchSemitones());

    // Dynamic pitch framing: always ensure at least full C1 (24) to C8 (108) range
    if (m_minKey > 24) m_minKey = 24;
    if (m_maxKey < 108) m_maxKey = 108;
    if (tracker.GetTotalNoteCount() > 0) {
        if (tracker.GetMinMidiNote() < m_minKey) m_minKey = std::max(12, tracker.GetMinMidiNote() - 2);
        if (tracker.GetMaxMidiNote() > m_maxKey) m_maxKey = std::min(127, tracker.GetMaxMidiNote() + 2);
    }
    m_totalKeys = m_maxKey - m_minKey + 1;

    // Top Toolbar (in Horizontal mode)
    if (m_visualMode == VisualMode::Horizontal) {
        RenderToolbar(audioEngine, tracker);
        ImGui::Separator();
    }

    // Canvas region setup
    ImVec2 canvasP0 = ImGui::GetCursorScreenPos();
    ImVec2 canvasSz = ImGui::GetContentRegionAvail();
    if (canvasSz.x < 100.0f) canvasSz.x = 100.0f;
    if (canvasSz.y < 100.0f) canvasSz.y = 100.0f;
    ImVec2 canvasP1 = ImVec2(canvasP0.x + canvasSz.x, canvasP0.y + canvasSz.y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Waterfall visualizer mode branch
    if (m_visualMode == VisualMode::Waterfall) {
        RenderWaterfall(drawList, canvasP0, canvasP1, tracker, audioEngine);
        ImGui::Dummy(canvasSz);
        ImGui::End();
        return;
    }

    const float rulerHeight = 22.0f;
    const float keyGutterWidth = 54.0f;
    const float scrollbarW = 14.0f;
    const float scrollbarH = 14.0f;

    ImVec2 rulerP0 = ImVec2(canvasP0.x + keyGutterWidth, canvasP0.y);
    ImVec2 rulerP1 = ImVec2(canvasP1.x - scrollbarW, canvasP0.y + rulerHeight);

    ImVec2 kbP0 = ImVec2(canvasP0.x, canvasP0.y + rulerHeight);
    ImVec2 kbP1 = ImVec2(canvasP0.x + keyGutterWidth, canvasP1.y - scrollbarH);

    ImVec2 gridP0 = ImVec2(canvasP0.x + keyGutterWidth, canvasP0.y + rulerHeight);
    ImVec2 gridP1 = ImVec2(canvasP1.x - scrollbarW, canvasP1.y - scrollbarH);

    // Corner box above piano keyboard
    drawList->AddRectFilled(canvasP0, ImVec2(canvasP0.x + keyGutterWidth, canvasP0.y + rulerHeight), IM_COL32(20, 24, 30, 255));
    drawList->AddRect(canvasP0, ImVec2(canvasP0.x + keyGutterWidth, canvasP0.y + rulerHeight), IM_COL32(36, 42, 54, 255));
    drawList->AddText(ImVec2(canvasP0.x + 8, canvasP0.y + 4), IM_COL32(140, 150, 170, 255), "Note");

    uint32_t totalSamples = tracker.GetTotalSamples();
    if (totalSamples == 0) totalSamples = audioEngine.GetTotalSamples();
    if (totalSamples == 0) totalSamples = 44100 * 180; // default 3 min fallback

    float pixelsPerSec = 80.0f * m_zoomX;
    float samplesPerPixel = 44100.0f / pixelsPerSec;
    float songTotalWidth = (float)totalSamples / samplesPerPixel;
    float gridWidth = gridP1.x - gridP0.x;
    float gridHeight = gridP1.y - gridP0.y;

    // Auto-center vertical scroll on song pitch range if uninitialized or reset
    if (m_scrollY < 0.0f) {
        float centerPitch = (tracker.GetTotalNoteCount() > 0)
            ? ((float)(tracker.GetMinMidiNote() + tracker.GetMaxMidiNote()) * 0.5f)
            : 60.0f; // Default C4
        float centerPitchY = (float)(m_maxKey - centerPitch) * m_keyHeight;
        float maxScrollY = std::max(0.0f, (float)m_totalKeys * m_keyHeight - gridHeight);
        m_scrollY = std::clamp(centerPitchY - gridHeight * 0.5f, 0.0f, maxScrollY);
    }

    // Follow playhead logic
    if (m_followPlayhead && audioEngine.IsPlaying() && !m_isScrubbing) {
        float playheadPixelX = (float)curSample / samplesPerPixel;
        float desiredScrollX = playheadPixelX - gridWidth * 0.35f;
        if (desiredScrollX < 0.0f) desiredScrollX = 0.0f;
        m_scrollX = desiredScrollX;
    }

    // Handle Input & Pan / Zoom / Scrub on Grid
    ImGuiIO& io = ImGui::GetIO();
    bool isWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    bool isModalOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

    bool isHoveringGrid = isWindowHovered && !isModalOpen && ImGui::IsMouseHoveringRect(gridP0, gridP1);
    bool isHoveringRuler = isWindowHovered && !isModalOpen && ImGui::IsMouseHoveringRect(rulerP0, rulerP1);
    bool isHoveringCanvas = isWindowHovered && !isModalOpen && ImGui::IsMouseHoveringRect(canvasP0, canvasP1);

    if (isHoveringCanvas) {
        // Zoom with Ctrl + Wheel
        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            float zoomDelta = io.MouseWheel * 0.2f;
            float newZoom = std::clamp(m_zoomX + zoomDelta, 0.1f, 10.0f);
            // Zoom towards mouse position
            float mouseRelX = io.MousePos.x - gridP0.x + m_scrollX;
            float timeAtMouse = mouseRelX / pixelsPerSec;
            m_zoomX = newZoom;
            pixelsPerSec = 80.0f * m_zoomX;
            samplesPerPixel = 44100.0f / pixelsPerSec;
            m_scrollX = std::max(0.0f, timeAtMouse * pixelsPerSec - (io.MousePos.x - gridP0.x));
        }
        // Horizontal scroll with Shift + Wheel
        else if (io.KeyShift && io.MouseWheel != 0.0f) {
            m_scrollX -= io.MouseWheel * 60.0f;
            if (m_scrollX < 0.0f) m_scrollX = 0.0f;
        }
        // Vertical pitch scroll with regular Wheel
        else if (io.MouseWheel != 0.0f) {
            m_scrollY -= io.MouseWheel * (m_keyHeight * 3.0f);
            float maxScrollY = std::max(0.0f, (float)m_totalKeys * m_keyHeight - gridHeight);
            m_scrollY = std::clamp(m_scrollY, 0.0f, maxScrollY);
        }

        // Panning with Middle click or Right click drag
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            m_isPanning = true;
            m_lastMousePos = io.MousePos;
        }
    }

    if (m_isPanning) {
        if (!isModalOpen && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))) {
            ImVec2 mouseDelta = ImVec2(io.MousePos.x - m_lastMousePos.x, io.MousePos.y - m_lastMousePos.y);
            m_scrollX -= mouseDelta.x;
            m_scrollY -= mouseDelta.y;
            if (m_scrollX < 0.0f) m_scrollX = 0.0f;
            float maxScrollY = std::max(0.0f, (float)m_totalKeys * m_keyHeight - gridHeight);
            m_scrollY = std::clamp(m_scrollY, 0.0f, maxScrollY);
            m_lastMousePos = io.MousePos;
            m_followPlayhead = false; // user took manual control
        } else {
            m_isPanning = false;
        }
    }

    // Timeline Ruler Scrubbing & Seeking:
    // When clicking the ruler, freeze/pause audio and seek.
    // As long as mouse is held down, stay frozen at that position (or dragged position).
    // When released, resume playback if it was playing beforehand!
    if (isWindowHovered && !isModalOpen) {
        if ((isHoveringRuler || (isHoveringGrid && io.KeyAlt)) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_isScrubbing = true;
            m_rulerWasPlaying = audioEngine.IsPlaying();
            if (m_rulerWasPlaying) {
                audioEngine.Pause();
            }
            float clickX = io.MousePos.x - gridP0.x + m_scrollX;
            if (clickX >= 0.0f) {
                uint32_t seekSample = (uint32_t)(clickX * samplesPerPixel);
                audioEngine.SeekToSample(seekSample);
                m_lastScrubbedSample = seekSample;
            }
        }
    }

    if (m_isScrubbing) {
        if (!isModalOpen && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float clickX = io.MousePos.x - gridP0.x + m_scrollX;
            if (clickX >= 0.0f) {
                uint32_t seekSample = (uint32_t)(clickX * samplesPerPixel);
                if (seekSample != m_lastScrubbedSample) {
                    audioEngine.SeekToSample(seekSample);
                    m_lastScrubbedSample = seekSample;
                }
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            float curSecVal = (float)audioEngine.GetCurSeconds();
            int totalSecVal = (int)curSecVal;
            int minVal = totalSecVal / 60;
            int secVal = totalSecVal % 60;
            int centiVal = (int)((curSecVal - totalSecVal) * 100);
            ImGui::SetTooltip("Playback: %02d:%02d.%02d (Hold to pause, Drag to seek)", minVal, secVal, centiVal);
        } else {
            m_isScrubbing = false;
            m_scrubAnchorX = -1.0f;
            if (m_rulerWasPlaying) {
                audioEngine.Play();
                m_rulerWasPlaying = false;
            }
        }
    }

    // Render Components
    RenderRuler(rulerP0, rulerP1, songTotalWidth, totalSamples, audioEngine);
    RenderKeyboard(drawList, kbP0, kbP1, tracker, audioEngine);
    RenderNotesAndGrid(drawList, gridP0, gridP1, tracker, audioEngine);
    RenderScrollbars(gridP0, gridP1, songTotalWidth, (float)m_totalKeys * m_keyHeight);

    // Dummy element to consume content region
    ImGui::Dummy(canvasSz);

    ImGui::End();
}

void PianoRoll::RenderToolbar(AudioEngine& audioEngine, NoteTracker& tracker) {
    (void)audioEngine;
    (void)tracker;
    if (m_visualMode == VisualMode::Horizontal) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
        ImGui::Checkbox("Auto-Scroll", &m_followPlayhead);
        ImGui::PopStyleVar();
    }
}

void PianoRoll::RenderRuler(ImVec2 rulerP0, ImVec2 rulerP1, float totalWidth, uint32_t totalSamples, AudioEngine& audioEngine) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Background & border
    drawList->AddRectFilled(rulerP0, rulerP1, IM_COL32(22, 26, 33, 255));
    drawList->AddLine(ImVec2(rulerP0.x, rulerP1.y), rulerP1, IM_COL32(40, 48, 62, 255), 1.0f);

    float pixelsPerSec = 80.0f * m_zoomX;
    float rulerWidth = rulerP1.x - rulerP0.x;

    // Time markings
    float startSec = m_scrollX / pixelsPerSec;
    float endSec = (m_scrollX + rulerWidth) / pixelsPerSec;

    // Determine interval adaptively so labels never crowd (minimum 75px per label)
    const float minPixelsPerLabel = 75.0f;
    float minSecInterval = minPixelsPerLabel / std::max(0.001f, pixelsPerSec);

    static const float CANDIDATE_INTERVALS[] = {
        0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 30.0f, 60.0f, 120.0f, 300.0f, 600.0f
    };
    float secInterval = 600.0f;
    for (float cand : CANDIDATE_INTERVALS) {
        if (cand >= minSecInterval) {
            secInterval = cand;
            break;
        }
    }

    int firstMark = (int)std::floor(startSec / secInterval);
    int lastMark = (int)std::ceil(endSec / secInterval);

    for (int m = firstMark; m <= lastMark; ++m) {
        float t = (float)m * secInterval;
        float x = rulerP0.x - m_scrollX + t * pixelsPerSec;
        if (x < rulerP0.x || x > rulerP1.x) continue;

        // Tick mark
        drawList->AddLine(ImVec2(x, rulerP1.y - 8), ImVec2(x, rulerP1.y), IM_COL32(70, 82, 105, 255));

        // Time label
        int minutes = (int)t / 60;
        int seconds = (int)t % 60;
        char timeBuf[24];
        if (secInterval < 1.0f) {
            int tenths = (int)((t - std::floor(t)) * 10.0f);
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d.%d", minutes, seconds, tenths);
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", minutes, seconds);
        }
        drawList->AddText(ImVec2(x + 3, rulerP0.y + 2), IM_COL32(160, 175, 195, 255), timeBuf);
    }

    // Loop marker in Timeline Ruler
    const auto& songInfo = audioEngine.GetCurrentInfo();
    if (songInfo.hasLoop && songInfo.loopSamples > 0 && songInfo.totalSamples > songInfo.loopSamples) {
        uint32_t loopStartSample = songInfo.totalSamples - songInfo.loopSamples;
        float rulerLoopX = rulerP0.x - m_scrollX + ((float)loopStartSample / 44100.0f) * pixelsPerSec;
        if (rulerLoopX >= rulerP0.x && rulerLoopX <= rulerP1.x) {
            drawList->AddLine(ImVec2(rulerLoopX, rulerP0.y), ImVec2(rulerLoopX, rulerP1.y), IM_COL32(255, 185, 35, 240), 1.5f);
            drawList->AddRectFilled(
                ImVec2(rulerLoopX, rulerP1.y - 15.0f),
                ImVec2(rulerLoopX + 34.0f, rulerP1.y),
                IM_COL32(235, 145, 15, 240),
                2.0f
            );
            drawList->AddText(
                ImVec2(rulerLoopX + 4.0f, rulerP1.y - 15.0f),
                IM_COL32(255, 255, 255, 255),
                "Loop"
            );
        }
    }
}

void PianoRoll::RenderKeyboard(ImDrawList* drawList, ImVec2 kbP0, ImVec2 kbP1, const NoteTracker& tracker, AudioEngine& audioEngine) {
    drawList->PushClipRect(kbP0, kbP1, true);

    // Keyboard background
    drawList->AddRectFilled(kbP0, kbP1, IM_COL32(18, 21, 26, 255));
    drawList->AddLine(ImVec2(kbP1.x, kbP0.y), kbP1, IM_COL32(40, 48, 60, 255), 1.5f);

    const auto& channels = tracker.GetChannels();
    bool anySolo = false;
    for (const auto& ch : channels) {
        if (ch.solo) { anySolo = true; break; }
    }

    // Map which pitches are currently sounding
    bool keyActive[128] = {false};
    uint32_t keyColor[128] = {0};

    for (const auto& ch : channels) {
        bool isMuted = ch.muted || (anySolo && !ch.solo);
        if (ch.active && ch.visible && !isMuted) {
            int midi = (int)std::round(ch.curMidiNote);
            if (midi >= 0 && midi < 128) {
                keyActive[midi] = true;
                keyColor[midi] = ch.color;
            }
        }
    }

    // Render keys from m_maxKey down to m_minKey
    float gutterWidth = kbP1.x - kbP0.x;

    for (int k = m_maxKey; k >= m_minKey; --k) {
        float y0 = kbP0.y - m_scrollY + (float)(m_maxKey - k) * m_keyHeight;
        float y1 = y0 + m_keyHeight;

        // Skip keys outside view
        if (y1 < kbP0.y || y0 > kbP1.y) continue;

        bool black = IsBlackKey(k);
        bool active = (k >= 0 && k < 128 && keyActive[k]);

        if (active) {
            // Bright glowing key with active channel color
            uint32_t col = keyColor[k];
            drawList->AddRectFilled(ImVec2(kbP0.x + 1, y0 + 1), ImVec2(kbP1.x - 2, y1 - 1), col, 2.0f);
            drawList->AddRect(ImVec2(kbP0.x + 1, y0 + 1), ImVec2(kbP1.x - 2, y1 - 1), IM_COL32(255, 255, 255, 240), 2.0f);
        } else if (black) {
            // Black key
            drawList->AddRectFilled(ImVec2(kbP0.x + 2, y0 + 1), ImVec2(kbP0.x + gutterWidth * 0.65f, y1 - 1), IM_COL32(28, 32, 40, 255), 1.5f);
            drawList->AddRect(ImVec2(kbP0.x + 2, y0 + 1), ImVec2(kbP0.x + gutterWidth * 0.65f, y1 - 1), IM_COL32(45, 52, 65, 255), 1.5f);
        } else {
            // White key
            drawList->AddRectFilled(ImVec2(kbP0.x + 1, y0 + 1), ImVec2(kbP1.x - 2, y1 - 1), IM_COL32(205, 215, 225, 255), 1.5f);
            drawList->AddLine(ImVec2(kbP0.x + 1, y1), ImVec2(kbP1.x - 2, y1), IM_COL32(140, 150, 165, 255));
        }

        // Note labels on C keys or active keys
        int semitone = (k % 12 + 12) % 12;
        if (m_showKeyLabels && (semitone == 0 || active)) {
            int octave = (k / 12) - 1;
            char keyName[16];
            snprintf(keyName, sizeof(keyName), "%s%d", NOTE_LABELS[semitone], octave);

            ImU32 textCol;
            if (active) {
                textCol = IM_COL32(20, 25, 30, 255);
            } else if (black) {
                textCol = IM_COL32(160, 175, 195, 255);
            } else {
                textCol = IM_COL32(30, 35, 45, 255);
            }
            drawList->AddText(ImVec2(kbP0.x + 6, y0 + (m_keyHeight - 13.0f) * 0.5f), textCol, keyName);
        }
    }

    drawList->PopClipRect();
}

void PianoRoll::RenderNotesAndGrid(ImDrawList* drawList, ImVec2 gridP0, ImVec2 gridP1, const NoteTracker& tracker, AudioEngine& audioEngine) {
    drawList->PushClipRect(gridP0, gridP1, true);

    // Canvas background
    drawList->AddRectFilled(gridP0, gridP1, IM_COL32(16, 19, 24, 255));

    float gridWidth = gridP1.x - gridP0.x;
    float pixelsPerSec = 80.0f * m_zoomX;
    float samplesPerPixel = 44100.0f / pixelsPerSec;

    // 1. Horizontal Pitch Lane Lines
    for (int k = m_maxKey; k >= m_minKey; --k) {
        float y0 = gridP0.y - m_scrollY + (float)(m_maxKey - k) * m_keyHeight;
        float y1 = y0 + m_keyHeight;

        if (y1 < gridP0.y || y0 > gridP1.y) continue;

        bool black = IsBlackKey(k);
        int semitone = (k % 12 + 12) % 12;

        // Lane background
        ImU32 rowBg = black ? IM_COL32(20, 23, 29, 255) : IM_COL32(24, 28, 36, 255);
        drawList->AddRectFilled(ImVec2(gridP0.x, y0), ImVec2(gridP1.x, y1), rowBg);

        // Octave divider (C key boundary)
        if (semitone == 0) {
            drawList->AddLine(ImVec2(gridP0.x, y1), ImVec2(gridP1.x, y1), IM_COL32(50, 62, 80, 255), 1.5f);
        } else {
            drawList->AddLine(ImVec2(gridP0.x, y1), ImVec2(gridP1.x, y1), IM_COL32(30, 36, 46, 200), 1.0f);
        }
    }

    // 2. Vertical Time Beat / Second Lines
    float startSec = m_scrollX / pixelsPerSec;
    float endSec = (m_scrollX + gridWidth) / pixelsPerSec;

    const float minPixelsPerLabel = 75.0f;
    float minSecInterval = minPixelsPerLabel / std::max(0.001f, pixelsPerSec);

    static const float CANDIDATE_INTERVALS[] = {
        0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 30.0f, 60.0f, 120.0f, 300.0f, 600.0f
    };
    float secInterval = 600.0f;
    for (float cand : CANDIDATE_INTERVALS) {
        if (cand >= minSecInterval) {
            secInterval = cand;
            break;
        }
    }

    int firstMark = (int)std::floor(startSec / secInterval);
    int lastMark = (int)std::ceil(endSec / secInterval);

    for (int m = firstMark; m <= lastMark; ++m) {
        float t = (float)m * secInterval;
        float x = gridP0.x - m_scrollX + t * pixelsPerSec;
        if (x < gridP0.x || x > gridP1.x) continue;

        bool isBar = ((int)std::round(t / secInterval) % 4 == 0);
        ImU32 lineCol = isBar ? IM_COL32(45, 55, 72, 255) : IM_COL32(32, 38, 50, 200);
        drawList->AddLine(ImVec2(x, gridP0.y), ImVec2(x, gridP1.y), lineCol, isBar ? 1.5f : 1.0f);
    }

    // 3. Render Tracked Note Blocks
    uint32_t startSample = (uint32_t)std::max(0.0f, m_scrollX * samplesPerPixel);
    uint32_t endSample = (uint32_t)std::max(0.0f, (m_scrollX + gridWidth) * samplesPerPixel);

    std::vector<const TrackedNote*> visibleNotes;
    tracker.QueryNotesInWindow(startSample, endSample, visibleNotes);

    uint32_t curSample = audioEngine.GetCurSample();
    const auto& channels = tracker.GetChannels();
    bool anySolo = false;
    for (const auto& ch : channels) {
        if (ch.solo) { anySolo = true; break; }
    }

    for (const TrackedNote* note : visibleNotes) {
        if (!note) continue;
        if (note->channelId < 0 || note->channelId >= (int)channels.size()) continue;
        if (!channels[note->channelId].visible) continue;

        int pShift = (int)std::round(audioEngine.GetPitchSemitones());
        int notePitch = note->noteNumber + pShift;
        if (notePitch < m_minKey || notePitch > m_maxKey) continue;

        bool isMuted = channels[note->channelId].muted || (anySolo && !channels[note->channelId].solo);

        // Position math
        float nx0 = gridP0.x - m_scrollX + (float)note->startSample / samplesPerPixel;
        float nx1 = gridP0.x - m_scrollX + (float)note->endSample / samplesPerPixel;
        if (nx1 - nx0 < 3.0f) nx1 = nx0 + 3.0f; // minimum visible thickness

        float ny0 = gridP0.y - m_scrollY + (float)(m_maxKey - notePitch) * m_keyHeight + 1.0f;
        float ny1 = ny0 + m_keyHeight - 2.0f;

        // Is this note currently intersecting the playhead?
        bool isActiveAtPlayhead = (curSample >= note->startSample && curSample <= note->endSample && !isMuted);

        // Note block color from channel
        uint32_t channelCol = channels[note->channelId].color;
        if (isMuted) {
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(channelCol);
            c.w *= 0.35f;
            channelCol = ImGui::ColorConvertFloat4ToU32(c);
        }

        // Draw note rectangle
        drawList->AddRectFilled(ImVec2(nx0, ny0), ImVec2(nx1, ny1), channelCol, 2.5f);

        // Specular highlight / border
        if (isActiveAtPlayhead) {
            // Bright white glowing outline
            drawList->AddRect(ImVec2(nx0, ny0), ImVec2(nx1, ny1), IM_COL32(255, 255, 255, 255), 2.5f, 0, 2.0f);
        } else if (!isMuted) {
            // Subtle crisp border
            drawList->AddRect(ImVec2(nx0, ny0), ImVec2(nx1, ny1), IM_COL32(255, 255, 255, 80), 2.5f, 0, 1.0f);
        }

        // Render note label if enabled in settings and wide enough
        float noteWidth = nx1 - nx0;
        if (m_showNoteNames && noteWidth >= 28.0f && m_keyHeight >= 12.0f) {
            float textY = ny0 + (m_keyHeight - 14.0f) * 0.5f;
            ImU32 textCol = isMuted ? IM_COL32(180, 190, 205, 120) : IM_COL32(255, 255, 255, 240);
            drawList->AddText(ImVec2(nx0 + 3, textY), textCol, note->noteName);
        }
    }

    // 4. Loop Marker Line (Horizontal Piano Roll)
    const auto& songInfo = audioEngine.GetCurrentInfo();
    if (songInfo.hasLoop && songInfo.loopSamples > 0 && songInfo.totalSamples > songInfo.loopSamples) {
        uint32_t loopStartSample = songInfo.totalSamples - songInfo.loopSamples;
        float loopX = gridP0.x - m_scrollX + (float)loopStartSample / samplesPerPixel;
        if (loopX >= gridP0.x && loopX <= gridP1.x) {
            // Distinct Amber / Gold Vertical Loop Line across piano roll grid
            drawList->AddLine(ImVec2(loopX, gridP0.y), ImVec2(loopX, gridP1.y), IM_COL32(255, 185, 35, 200), 1.5f);
        }
    }

    // 5. Sweeping Neon Playhead Cursor
    float playheadX = gridP0.x - m_scrollX + (float)curSample / samplesPerPixel;
    if (playheadX >= gridP0.x && playheadX <= gridP1.x) {
        // Vertical cursor line
        drawList->AddLine(ImVec2(playheadX, gridP0.y), ImVec2(playheadX, gridP1.y), IM_COL32(0, 240, 255, 240), 2.0f);

        // Triangular playhead marker at top
        drawList->AddTriangleFilled(
            ImVec2(playheadX - 6.0f, gridP0.y),
            ImVec2(playheadX + 6.0f, gridP0.y),
            ImVec2(playheadX, gridP0.y + 10.0f),
            IM_COL32(0, 240, 255, 255)
        );
    }

    // Outer frame border
    drawList->AddRect(gridP0, gridP1, IM_COL32(40, 48, 62, 255));

    drawList->PopClipRect();
}

void PianoRoll::RenderScrollbars(ImVec2 gridP0, ImVec2 gridP1, float songTotalWidth, float totalPitchHeight) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();

    const float sbH = 16.0f;
    const float sbW = 16.0f;
    const float btnSize = 16.0f;

    bool canScroll = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

    // =========================================================================
    // 1. Horizontal Scrollbar (Time / X) with [−] and [+] Zoom Buttons
    // =========================================================================
    float hTrackRight = gridP1.x - (btnSize * 2.0f + 4.0f);
    ImVec2 hTrackP0 = ImVec2(gridP0.x, gridP1.y);
    ImVec2 hTrackP1 = ImVec2(hTrackRight, gridP1.y + sbH);

    dl->AddRectFilled(hTrackP0, hTrackP1, IM_COL32(16, 20, 26, 255));
    dl->AddRect(hTrackP0, hTrackP1, IM_COL32(32, 40, 52, 255));

    float hTrackW = hTrackP1.x - hTrackP0.x;
    float maxScrollX = std::max(0.0f, songTotalWidth - (gridP1.x - gridP0.x));
    if (songTotalWidth > 0.0f && hTrackW > 30.0f) {
        float visibleRatio = std::clamp((gridP1.x - gridP0.x) / songTotalWidth, 0.05f, 1.0f);
        float thumbW = std::clamp(visibleRatio * hTrackW, 25.0f, hTrackW);
        float scrollRatio = (maxScrollX > 0.0f) ? std::clamp(m_scrollX / maxScrollX, 0.0f, 1.0f) : 0.0f;
        float thumbX0 = hTrackP0.x + scrollRatio * (hTrackW - thumbW);
        float thumbX1 = thumbX0 + thumbW;

        bool isHoverThumb = canScroll && io.MousePos.x >= thumbX0 && io.MousePos.x <= thumbX1 &&
                            io.MousePos.y >= hTrackP0.y && io.MousePos.y <= hTrackP1.y;
        bool isHoverTrack = canScroll && io.MousePos.x >= hTrackP0.x && io.MousePos.x <= hTrackP1.x &&
                            io.MousePos.y >= hTrackP0.y && io.MousePos.y <= hTrackP1.y;

        ImU32 thumbCol = isHoverThumb ? IM_COL32(75, 92, 118, 255) : IM_COL32(50, 62, 80, 255);
        if (canScroll && isHoverTrack && io.MouseClicked[0]) {
            m_isDraggingThumbX = true;
            m_followPlayhead = false;
        }

        if (m_isDraggingThumbX) {
            if (io.MouseDown[0]) {
                thumbCol = IM_COL32(0, 173, 181, 255);
                float clickRatio = (io.MousePos.x - hTrackP0.x - thumbW * 0.5f) / std::max(1.0f, hTrackW - thumbW);
                clickRatio = std::clamp(clickRatio, 0.0f, 1.0f);
                m_scrollX = clickRatio * maxScrollX;
                m_followPlayhead = false;
                // Recompute thumbX0
                scrollRatio = (maxScrollX > 0.0f) ? std::clamp(m_scrollX / maxScrollX, 0.0f, 1.0f) : 0.0f;
                thumbX0 = hTrackP0.x + scrollRatio * (hTrackW - thumbW);
                thumbX1 = thumbX0 + thumbW;
            } else {
                m_isDraggingThumbX = false;
            }
        }

        dl->AddRectFilled(ImVec2(thumbX0, hTrackP0.y + 2.0f), ImVec2(thumbX1, hTrackP1.y - 2.0f), thumbCol, 3.0f);
    } else {
        m_isDraggingThumbX = false;
    }

    // Horizontal Zoom [−] Button
    ImVec2 zMinusP0 = ImVec2(hTrackRight + 2.0f, gridP1.y);
    ImVec2 zMinusP1 = ImVec2(zMinusP0.x + btnSize, gridP1.y + sbH);
    bool isHoverZMinus = canScroll && io.MousePos.x >= zMinusP0.x && io.MousePos.x <= zMinusP1.x &&
                         io.MousePos.y >= zMinusP0.y && io.MousePos.y <= zMinusP1.y;
    ImU32 zMinusBg = isHoverZMinus ? (io.MouseDown[0] ? IM_COL32(0, 150, 160, 255) : IM_COL32(45, 55, 72, 255)) : IM_COL32(24, 30, 40, 255);
    dl->AddRectFilled(zMinusP0, zMinusP1, zMinusBg, 2.0f);
    dl->AddRect(zMinusP0, zMinusP1, IM_COL32(42, 52, 68, 255), 2.0f);
    dl->AddText(ImVec2(zMinusP0.x + 4.5f, zMinusP0.y + 0.5f), IM_COL32(220, 230, 245, 255), "-");
    if (isHoverZMinus) {
        ImGui::SetTooltip("Zoom Out Time (Scroll X)");
        if (io.MouseClicked[0]) {
            m_zoomX = std::max(0.2f, m_zoomX * 0.8f);
        }
    }

    // Horizontal Zoom [+] Button
    ImVec2 zPlusP0 = ImVec2(zMinusP1.x + 2.0f, gridP1.y);
    ImVec2 zPlusP1 = ImVec2(zPlusP0.x + btnSize, gridP1.y + sbH);
    bool isHoverZPlus = canScroll && io.MousePos.x >= zPlusP0.x && io.MousePos.x <= zPlusP1.x &&
                        io.MousePos.y >= zPlusP0.y && io.MousePos.y <= zPlusP1.y;
    ImU32 zPlusBg = isHoverZPlus ? (io.MouseDown[0] ? IM_COL32(0, 150, 160, 255) : IM_COL32(45, 55, 72, 255)) : IM_COL32(24, 30, 40, 255);
    dl->AddRectFilled(zPlusP0, zPlusP1, zPlusBg, 2.0f);
    dl->AddRect(zPlusP0, zPlusP1, IM_COL32(42, 52, 68, 255), 2.0f);
    dl->AddText(ImVec2(zPlusP0.x + 3.5f, zPlusP0.y + 0.5f), IM_COL32(220, 230, 245, 255), "+");
    if (isHoverZPlus) {
        ImGui::SetTooltip("Zoom In Time (Scroll X)");
        if (io.MouseClicked[0]) {
            m_zoomX = std::min(8.0f, m_zoomX * 1.25f);
        }
    }

    // =========================================================================
    // 2. Vertical Scrollbar (Pitch / Y) with [+] and [−] Key H Zoom Buttons
    // =========================================================================
    // Vertical Key Height Zoom [+] Button (Top)
    ImVec2 zYPlusP0 = ImVec2(gridP1.x, gridP0.y);
    ImVec2 zYPlusP1 = ImVec2(gridP1.x + sbW, zYPlusP0.y + btnSize);
    bool isHoverZYPlus = canScroll && io.MousePos.x >= zYPlusP0.x && io.MousePos.x <= zYPlusP1.x &&
                         io.MousePos.y >= zYPlusP0.y && io.MousePos.y <= zYPlusP1.y;
    ImU32 zYPlusBg = isHoverZYPlus ? (io.MouseDown[0] ? IM_COL32(0, 150, 160, 255) : IM_COL32(45, 55, 72, 255)) : IM_COL32(24, 30, 40, 255);
    dl->AddRectFilled(zYPlusP0, zYPlusP1, zYPlusBg, 2.0f);
    dl->AddRect(zYPlusP0, zYPlusP1, IM_COL32(42, 52, 68, 255), 2.0f);
    dl->AddText(ImVec2(zYPlusP0.x + 3.5f, zYPlusP0.y + 0.5f), IM_COL32(220, 230, 245, 255), "+");
    if (isHoverZYPlus) {
        ImGui::SetTooltip("Zoom In Key Height (Scroll Y)");
        if (io.MouseClicked[0]) {
            m_keyHeight = std::min(28.0f, m_keyHeight + 2.0f);
        }
    }

    // Vertical Key Height Zoom [−] Button (Below [+])
    ImVec2 zYMinusP0 = ImVec2(gridP1.x, zYPlusP1.y + 2.0f);
    ImVec2 zYMinusP1 = ImVec2(gridP1.x + sbW, zYMinusP0.y + btnSize);
    bool isHoverZYMinus = canScroll && io.MousePos.x >= zYMinusP0.x && io.MousePos.x <= zYMinusP1.x &&
                          io.MousePos.y >= zYMinusP0.y && io.MousePos.y <= zYMinusP1.y;
    ImU32 zYMinusBg = isHoverZYMinus ? (io.MouseDown[0] ? IM_COL32(0, 150, 160, 255) : IM_COL32(45, 55, 72, 255)) : IM_COL32(24, 30, 40, 255);
    dl->AddRectFilled(zYMinusP0, zYMinusP1, zYMinusBg, 2.0f);
    dl->AddRect(zYMinusP0, zYMinusP1, IM_COL32(42, 52, 68, 255), 2.0f);
    dl->AddText(ImVec2(zYMinusP0.x + 4.5f, zYMinusP0.y + 0.5f), IM_COL32(220, 230, 245, 255), "-");
    if (isHoverZYMinus) {
        ImGui::SetTooltip("Zoom Out Key Height (Scroll Y)");
        if (io.MouseClicked[0]) {
            m_keyHeight = std::max(10.0f, m_keyHeight - 2.0f);
        }
    }

    // Vertical Track (below zoom buttons)
    float vTrackTop = zYMinusP1.y + 2.0f;
    ImVec2 vTrackP0 = ImVec2(gridP1.x, vTrackTop);
    ImVec2 vTrackP1 = ImVec2(gridP1.x + sbW, gridP1.y);

    dl->AddRectFilled(vTrackP0, vTrackP1, IM_COL32(16, 20, 26, 255));
    dl->AddRect(vTrackP0, vTrackP1, IM_COL32(32, 40, 52, 255));

    float vTrackH = vTrackP1.y - vTrackP0.y;
    float maxScrollY = std::max(0.0f, totalPitchHeight - (gridP1.y - gridP0.y));
    if (totalPitchHeight > 0.0f && vTrackH > 30.0f) {
        float visibleRatioY = std::clamp((gridP1.y - gridP0.y) / totalPitchHeight, 0.05f, 1.0f);
        float thumbH = std::clamp(visibleRatioY * vTrackH, 25.0f, vTrackH);
        float scrollRatioY = (maxScrollY > 0.0f) ? std::clamp(m_scrollY / maxScrollY, 0.0f, 1.0f) : 0.0f;
        float thumbY0 = vTrackP0.y + scrollRatioY * (vTrackH - thumbH);
        float thumbY1 = thumbY0 + thumbH;

        bool isHoverThumbY = canScroll && io.MousePos.x >= vTrackP0.x && io.MousePos.x <= vTrackP1.x &&
                             io.MousePos.y >= thumbY0 && io.MousePos.y <= thumbY1;
        bool isHoverTrackY = canScroll && io.MousePos.x >= vTrackP0.x && io.MousePos.x <= vTrackP1.x &&
                             io.MousePos.y >= vTrackP0.y && io.MousePos.y <= vTrackP1.y;

        ImU32 thumbColY = isHoverThumbY ? IM_COL32(75, 92, 118, 255) : IM_COL32(50, 62, 80, 255);
        if (canScroll && isHoverTrackY && io.MouseClicked[0]) {
            m_isDraggingThumbY = true;
        }

        if (m_isDraggingThumbY) {
            if (io.MouseDown[0]) {
                thumbColY = IM_COL32(0, 173, 181, 255);
                float clickRatioY = (io.MousePos.y - vTrackP0.y - thumbH * 0.5f) / std::max(1.0f, vTrackH - thumbH);
                clickRatioY = std::clamp(clickRatioY, 0.0f, 1.0f);
                m_scrollY = clickRatioY * maxScrollY;
                // Recompute thumbY0
                scrollRatioY = (maxScrollY > 0.0f) ? std::clamp(m_scrollY / maxScrollY, 0.0f, 1.0f) : 0.0f;
                thumbY0 = vTrackP0.y + scrollRatioY * (vTrackH - thumbH);
                thumbY1 = thumbY0 + thumbH;
            } else {
                m_isDraggingThumbY = false;
            }
        }

        dl->AddRectFilled(ImVec2(vTrackP0.x + 2.0f, thumbY0), ImVec2(vTrackP1.x - 2.0f, thumbY1), thumbColY, 3.0f);
    } else {
        m_isDraggingThumbY = false;
    }

    // Corner junction: Reset Zoom to 100%
    ImVec2 cornerP0 = ImVec2(gridP1.x, gridP1.y);
    ImVec2 cornerP1 = ImVec2(gridP1.x + sbW, gridP1.y + sbH);
    bool isHoverCorner = canScroll && io.MousePos.x >= cornerP0.x && io.MousePos.x <= cornerP1.x &&
                         io.MousePos.y >= cornerP0.y && io.MousePos.y <= cornerP1.y;
    ImU32 cornerBg = isHoverCorner ? IM_COL32(36, 46, 62, 255) : IM_COL32(18, 22, 28, 255);
    dl->AddRectFilled(cornerP0, cornerP1, cornerBg);

    ImVec2 cornerCenter = ImVec2((cornerP0.x + cornerP1.x) * 0.5f, (cornerP0.y + cornerP1.y) * 0.5f);
    ImU32 arrowCol = isHoverCorner ? IM_COL32(0, 240, 255, 255) : IM_COL32(140, 160, 185, 200);
    DrawCircularResetArrow(dl, cornerCenter, 4.5f, arrowCol, 1.4f);

    if (isHoverCorner) {
        ImGui::SetTooltip("Reset Zoom to 100%%");
        if (io.MouseClicked[0]) {
            m_zoomX = 1.0f;
            m_keyHeight = 16.0f;
        }
    }
}

void PianoRoll::RenderWaterfall(ImDrawList* drawList, ImVec2 canvasP0, ImVec2 canvasP1, NoteTracker& tracker, AudioEngine& audioEngine) {
    ImGuiIO& io = ImGui::GetIO();
    bool isWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    bool isModalOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    bool canInteract = isWindowHovered && !isModalOpen;

    const float sbH = 16.0f;      // Bottom horizontal scrollbar height
    const float sbW = 16.0f;      // Right vertical gutter width
    const float btnSize = 16.0f;  // Control button size
    const float kbH = 68.0f;      // Piano keyboard height

    // Active visual dimensions (excluding bottom scrollbar and right gutter)
    float visualW = std::max(60.0f, (canvasP1.x - sbW) - canvasP0.x);
    float visualH = std::max(60.0f, (canvasP1.y - sbH) - canvasP0.y);

    float yTop = canvasP0.y;
    float yContact = (canvasP1.y - sbH) - kbH;
    float yKbBottom = canvasP1.y - sbH;

    // Pitch range: guarantee at least full C1 (24) to C8 (108) range
    int minKey = std::min(24, m_minKey);
    int maxKey = std::max(108, m_maxKey);
    if (tracker.GetTotalNoteCount() > 0) {
        if (tracker.GetMinMidiNote() < minKey) minKey = std::max(0, tracker.GetMinMidiNote() - 2);
        if (tracker.GetMaxMidiNote() > maxKey) maxKey = std::min(127, tracker.GetMaxMidiNote() + 2);
    }

    // Count white keys (exactly 50 between C1 and C8)
    int numWhiteKeys = 0;
    for (int k = minKey; k <= maxKey; ++k) {
        if (!IsBlackKey(k)) numWhiteKeys++;
    }
    if (numWhiteKeys < 1) numWhiteKeys = 1;

    // Horizontal Key Geometry & Zoom
    float baseWhiteKeyW = visualW / (float)numWhiteKeys;
    float whiteKeyW = baseWhiteKeyW * m_waterfallZoomX;
    float totalKeysW = whiteKeyW * (float)numWhiteKeys;
    float maxScrollX = std::max(0.0f, totalKeysW - visualW);
    m_waterfallScrollX = std::clamp(m_waterfallScrollX, 0.0f, maxScrollX);

    // Mouse canvas interaction (Ctrl+Wheel to zoom keys, Shift+Wheel or Right/Middle drag to pan)
    bool isHoverVisual = canInteract && io.MousePos.x >= canvasP0.x && io.MousePos.x <= (canvasP1.x - sbW) &&
                         io.MousePos.y >= canvasP0.y && io.MousePos.y <= (canvasP1.y - sbH);
    if (isHoverVisual) {
        if (io.KeyCtrl && io.MouseWheel != 0.0f) {
            float prevZoom = m_waterfallZoomX;
            m_waterfallZoomX = std::clamp(m_waterfallZoomX + io.MouseWheel * 0.15f, 1.0f, 4.0f);
            if (m_waterfallZoomX <= 1.05f) {
                m_waterfallZoomX = 1.0f;
                m_waterfallScrollX = 0.0f;
            } else if (prevZoom > 0.0f) {
                m_waterfallScrollX = (m_waterfallScrollX / prevZoom) * m_waterfallZoomX;
                maxScrollX = std::max(0.0f, (whiteKeyW * (float)numWhiteKeys) - visualW);
                m_waterfallScrollX = std::clamp(m_waterfallScrollX, 0.0f, maxScrollX);
            }
        } else if (io.KeyShift && io.MouseWheel != 0.0f) {
            m_waterfallScrollX = std::clamp(m_waterfallScrollX - io.MouseWheel * 50.0f, 0.0f, maxScrollX);
        } else if (io.MouseWheel != 0.0f && !io.KeyShift && !io.KeyCtrl) {
            m_waterfallTimeSpan = std::clamp(m_waterfallTimeSpan - io.MouseWheel * 0.25f, 1.0f, 8.0f);
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            m_isPanning = true;
            m_lastMousePos = io.MousePos;
        }
    }

    if (m_isPanning) {
        if (!isModalOpen && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))) {
            float dx = io.MousePos.x - m_lastMousePos.x;
            m_waterfallScrollX = std::clamp(m_waterfallScrollX - dx, 0.0f, maxScrollX);
            m_lastMousePos = io.MousePos;
        } else {
            m_isPanning = false;
        }
    }

    float marginL = canvasP0.x - m_waterfallScrollX;

    struct KeyGeom {
        float x0, x1;
        bool isBlack;
    };
    KeyGeom keyGeom[128];
    for (int k = 0; k < 128; ++k) {
        keyGeom[k] = {0, 0, IsBlackKey(k)};
    }

    int curWhiteIdx = 0;
    for (int k = minKey; k <= maxKey; ++k) {
        if (!IsBlackKey(k)) {
            keyGeom[k].x0 = marginL + (float)curWhiteIdx * whiteKeyW;
            keyGeom[k].x1 = keyGeom[k].x0 + whiteKeyW;
            curWhiteIdx++;
        }
    }

    // Position black keys centered on white key borders
    float blackKeyW = whiteKeyW * 0.62f;
    for (int k = minKey; k <= maxKey; ++k) {
        if (IsBlackKey(k)) {
            if (k > minKey && !IsBlackKey(k - 1)) {
                float borderX = keyGeom[k - 1].x1;
                keyGeom[k].x0 = borderX - blackKeyW * 0.5f;
                keyGeom[k].x1 = borderX + blackKeyW * 0.5f;
            } else if (k < maxKey && !IsBlackKey(k + 1)) {
                float borderX = keyGeom[k + 1].x0;
                keyGeom[k].x0 = borderX - blackKeyW * 0.5f;
                keyGeom[k].x1 = borderX + blackKeyW * 0.5f;
            } else {
                keyGeom[k].x0 = marginL;
                keyGeom[k].x1 = marginL + blackKeyW;
            }
        }
    }

    // Channel active notes lookup
    const auto& channels = tracker.GetChannels();
    bool anySolo = false;
    for (const auto& ch : channels) {
        if (ch.solo) { anySolo = true; break; }
    }

    bool keyActive[128] = {false};
    uint32_t keyColor[128] = {0};
    for (const auto& ch : channels) {
        bool isMuted = ch.muted || (anySolo && !ch.solo);
        if (ch.active && ch.visible && !isMuted) {
            int midi = (int)std::round(ch.curMidiNote);
            if (midi >= 0 && midi < 128) {
                keyActive[midi] = true;
                keyColor[midi] = ch.color;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 1. Render Visual Area (Falling Notes & Piano Keys clipped to main viewport)
    // -------------------------------------------------------------------------
    drawList->PushClipRect(canvasP0, ImVec2(canvasP1.x - sbW, canvasP1.y - sbH), true);

    // Falling field background
    drawList->AddRectFilled(canvasP0, ImVec2(canvasP1.x - sbW, yContact), IM_COL32(14, 17, 23, 255));

    // Vertical pitch guide lines (for white key edges and octave lines)
    for (int k = minKey; k <= maxKey; ++k) {
        if (!IsBlackKey(k)) {
            float x = keyGeom[k].x0;
            if (x >= canvasP0.x - 20.0f && x <= canvasP1.x + 20.0f) {
                int semi = (k % 12 + 12) % 12;
                bool isC = (semi == 0);
                ImU32 lineCol = isC ? IM_COL32(45, 56, 75, 200) : IM_COL32(26, 32, 42, 160);
                drawList->AddLine(ImVec2(x, yTop), ImVec2(x, yContact), lineCol, isC ? 1.5f : 1.0f);
            }
        }
    }

    // Cascading / Falling Notes
    float fallingH = yContact - yTop;
    float timeSpan = std::clamp(m_waterfallTimeSpan, 1.0f, 8.0f);
    float pixelsPerSecY = fallingH / timeSpan;
    uint32_t curSample = audioEngine.GetCurSample();

    // Query window: include notes starting up to 3 seconds in the past to support long sustained notes
    uint32_t pastMargin = (uint32_t)(44100 * 3.0f);
    uint32_t startWindow = (curSample > pastMargin) ? (curSample - pastMargin) : 0;
    uint32_t endWindow = curSample + (uint32_t)(44100 * (timeSpan + 0.5f));

    std::vector<const TrackedNote*> visibleNotes;
    tracker.QueryNotesInWindow(startWindow, endWindow, visibleNotes);

    for (const TrackedNote* note : visibleNotes) {
        if (!note) continue;
        if (note->channelId < 0 || note->channelId >= (int)channels.size()) continue;
        if (!channels[note->channelId].visible) continue;

        int pShift = (int)std::round(audioEngine.GetPitchSemitones());
        int k = note->noteNumber + pShift;
        if (k < minKey || k > maxKey) continue;

        bool isMuted = channels[note->channelId].muted || (anySolo && !channels[note->channelId].solo);

        // Signed sample differences prevent unsigned 32-bit underflow
        float timeStartSec = (float)((int64_t)note->startSample - (int64_t)curSample) / 44100.0f;
        float timeEndSec = (float)((int64_t)note->endSample - (int64_t)curSample) / 44100.0f;

        float noteBottomY = yContact - timeStartSec * pixelsPerSecY;
        float noteTopY = yContact - timeEndSec * pixelsPerSecY;

        bool isActiveAtContact = (curSample >= note->startSample && curSample <= note->endSample && !isMuted);
        if (isActiveAtContact) {
            noteBottomY = yContact;
            if (k >= 0 && k < 128) {
                keyActive[k] = true;
                keyColor[k] = channels[note->channelId].color;
                m_waterfallKeyHold[k] = 0.10f;
                m_waterfallKeyHoldColor[k] = channels[note->channelId].color;
            }
        }

        // Clip notes that are out of bounds
        if (noteBottomY < yTop || noteTopY > yContact) continue;

        float drawTop = std::max(yTop, noteTopY);
        float drawBottom = std::min(yContact, noteBottomY);
        if (drawBottom - drawTop < 2.5f) drawTop = drawBottom - 2.5f;

        float nx0 = keyGeom[k].x0 + 1.5f;
        float nx1 = keyGeom[k].x1 - 1.5f;

        // Skip notes outside visible horizontal range
        if (nx1 < canvasP0.x || nx0 > (canvasP1.x - sbW)) continue;

        uint32_t noteCol = channels[note->channelId].color;
        if (isMuted) {
            ImVec4 c = ImGui::ColorConvertU32ToFloat4(noteCol);
            c.w *= 0.35f;
            noteCol = ImGui::ColorConvertFloat4ToU32(c);
        }

        // Draw note rounded rect
        drawList->AddRectFilled(ImVec2(nx0, drawTop), ImVec2(nx1, drawBottom), noteCol, 3.0f);

        // Highlight border
        if (isActiveAtContact) {
            drawList->AddRect(ImVec2(nx0, drawTop), ImVec2(nx1, drawBottom), IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.0f);
        } else if (!isMuted) {
            drawList->AddRect(ImVec2(nx0, drawTop), ImVec2(nx1, drawBottom), IM_COL32(255, 255, 255, 80), 3.0f, 0, 1.0f);
        }

        // Note name label (if enabled)
        if (m_showNoteNames && (drawBottom - drawTop >= 18.0f) && (nx1 - nx0 >= 16.0f)) {
            ImVec2 txtSz = ImGui::CalcTextSize(note->noteName);
            if (txtSz.x <= (nx1 - nx0 + 2.0f)) {
                float txtX = nx0 + ((nx1 - nx0) - txtSz.x) * 0.5f;
                float txtY = drawBottom - 16.0f;
                ImU32 textCol = isMuted ? IM_COL32(180, 190, 205, 120) : IM_COL32(255, 255, 255, 240);
                drawList->AddText(ImVec2(txtX, txtY), textCol, note->noteName);
            }
        }
    }

    // Apply smooth key hold decay so keys do not flicker or cut off abruptly
    for (int k = 0; k < 128; ++k) {
        if (!keyActive[k] && m_waterfallKeyHold[k] > 0.0f) {
            m_waterfallKeyHold[k] -= io.DeltaTime;
            keyActive[k] = true;
            keyColor[k] = m_waterfallKeyHoldColor[k];
        }
    }

    // Contact Hit Line & Glow
    drawList->AddLine(ImVec2(canvasP0.x, yContact), ImVec2(canvasP1.x - sbW, yContact), IM_COL32(0, 235, 255, 240), 2.5f);

    // Piano Keyboard: White Keys
    for (int k = minKey; k <= maxKey; ++k) {
        if (!IsBlackKey(k)) {
            float x0 = keyGeom[k].x0;
            float x1 = keyGeom[k].x1;
            if (x1 < canvasP0.x || x0 > (canvasP1.x - sbW)) continue;

            float y0 = yContact;
            float y1 = yKbBottom;

            bool active = keyActive[k];
            if (active) {
                drawList->AddRectFilled(ImVec2(x0 + 1.0f, y0 + 1.0f), ImVec2(x1 - 1.0f, y1 - 2.0f), keyColor[k], 2.0f);
                drawList->AddRect(ImVec2(x0 + 1.0f, y0 + 1.0f), ImVec2(x1 - 1.0f, y1 - 2.0f), IM_COL32(255, 255, 255, 255), 2.0f);
            } else {
                drawList->AddRectFilled(ImVec2(x0 + 1.0f, y0 + 1.0f), ImVec2(x1 - 1.0f, y1 - 2.0f), IM_COL32(230, 235, 242, 255), 2.0f);
                drawList->AddRect(ImVec2(x0 + 1.0f, y0 + 1.0f), ImVec2(x1 - 1.0f, y1 - 2.0f), IM_COL32(55, 65, 80, 255), 2.0f);
            }

            int semi = (k % 12 + 12) % 12;
            if (m_showKeyLabels && (semi == 0 || active)) {
                int oct = (k / 12) - 1;
                char keyStr[16];
                snprintf(keyStr, sizeof(keyStr), "%s%d", NOTE_LABELS[semi], oct);
                ImVec2 tSz = ImGui::CalcTextSize(keyStr);
                if (tSz.x <= whiteKeyW) {
                    float tX = x0 + (whiteKeyW - tSz.x) * 0.5f;
                    float tY = y1 - 18.0f;
                    ImU32 tCol = active ? IM_COL32(20, 25, 30, 255) : IM_COL32(50, 60, 75, 255);
                    drawList->AddText(ImVec2(tX, tY), tCol, keyStr);
                }
            }
        }
    }

    // Piano Keyboard: Black Keys
    float blackKeyH = kbH * 0.62f;
    for (int k = minKey; k <= maxKey; ++k) {
        if (IsBlackKey(k)) {
            float x0 = keyGeom[k].x0;
            float x1 = keyGeom[k].x1;
            if (x1 < canvasP0.x || x0 > (canvasP1.x - sbW)) continue;

            float y0 = yContact;
            float y1 = yContact + blackKeyH;

            bool active = keyActive[k];
            if (active) {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), keyColor[k], 2.0f);
                drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(255, 255, 255, 255), 2.0f);
            } else {
                drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(28, 32, 42, 255), 2.0f);
                drawList->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(50, 60, 75, 255), 2.0f);
                drawList->AddLine(ImVec2(x0 + 1.0f, y1 - 1.0f), ImVec2(x1 - 1.0f, y1 - 1.0f), IM_COL32(75, 88, 110, 255));
            }

            if (m_showKeyLabels && active) {
                int semi = (k % 12 + 12) % 12;
                int oct = (k / 12) - 1;
                char keyStr[16];
                snprintf(keyStr, sizeof(keyStr), "%s%d", NOTE_LABELS[semi], oct);
                ImVec2 tSz = ImGui::CalcTextSize(keyStr);
                if (tSz.x <= blackKeyW + 4.0f) {
                    float tX = x0 + (blackKeyW - tSz.x) * 0.5f;
                    float tY = y1 - 16.0f;
                    drawList->AddText(ImVec2(tX, tY), IM_COL32(20, 25, 30, 255), keyStr);
                }
            }
        }
    }

    drawList->PopClipRect();

    // Border around visual region
    drawList->AddRect(canvasP0, ImVec2(canvasP1.x - sbW, canvasP1.y - sbH), IM_COL32(40, 48, 62, 255));

    // -------------------------------------------------------------------------
    // 2. Right Vertical Gutter: Speed [+] and [−] Controls (Y axis)
    // -------------------------------------------------------------------------
    ImVec2 vGutterP0 = ImVec2(canvasP1.x - sbW, canvasP0.y);
    ImVec2 vGutterP1 = ImVec2(canvasP1.x, canvasP1.y - sbH);
    drawList->AddRectFilled(vGutterP0, vGutterP1, IM_COL32(16, 20, 26, 255));
    drawList->AddRect(vGutterP0, vGutterP1, IM_COL32(32, 40, 52, 255));

    // Faster Speed [+] Button (decreases waterfall time span)
    ImVec2 spdPlusP0 = ImVec2(canvasP1.x - sbW, canvasP0.y);
    ImVec2 spdPlusP1 = ImVec2(canvasP1.x, canvasP0.y + btnSize);
    bool isHoverSpdPlus = canInteract && io.MousePos.x >= spdPlusP0.x && io.MousePos.x <= spdPlusP1.x &&
                          io.MousePos.y >= spdPlusP0.y && io.MousePos.y <= spdPlusP1.y;
    ImU32 spdPlusBg = isHoverSpdPlus ? (io.MouseDown[0] ? IM_COL32(0, 150, 160, 255) : IM_COL32(45, 55, 72, 255)) : IM_COL32(24, 30, 40, 255);
    drawList->AddRectFilled(spdPlusP0, spdPlusP1, spdPlusBg, 2.0f);
    drawList->AddRect(spdPlusP0, spdPlusP1, IM_COL32(42, 52, 68, 255), 2.0f);
    drawList->AddText(ImVec2(spdPlusP0.x + 3.5f, spdPlusP0.y + 0.5f), IM_COL32(220, 230, 245, 255), "+");
    if (isHoverSpdPlus) {
        ImGui::SetTooltip("Faster Waterfall Speed (+)\nCurrent Span: %.1fs", m_waterfallTimeSpan);
        if (io.MouseClicked[0]) {
            m_waterfallTimeSpan = std::max(1.0f, m_waterfallTimeSpan - 0.4f);
        }
    }

    // Slower Speed [−] Button (increases waterfall time span)
    ImVec2 spdMinusP0 = ImVec2(canvasP1.x - sbW, spdPlusP1.y + 2.0f);
    ImVec2 spdMinusP1 = ImVec2(canvasP1.x, spdMinusP0.y + btnSize);
    bool isHoverSpdMinus = canInteract && io.MousePos.x >= spdMinusP0.x && io.MousePos.x <= spdMinusP1.x &&
                           io.MousePos.y >= spdMinusP0.y && io.MousePos.y <= spdMinusP1.y;
    ImU32 spdMinusBg = isHoverSpdMinus ? (io.MouseDown[0] ? IM_COL32(0, 150, 160, 255) : IM_COL32(45, 55, 72, 255)) : IM_COL32(24, 30, 40, 255);
    drawList->AddRectFilled(spdMinusP0, spdMinusP1, spdMinusBg, 2.0f);
    drawList->AddRect(spdMinusP0, spdMinusP1, IM_COL32(42, 52, 68, 255), 2.0f);
    drawList->AddText(ImVec2(spdMinusP0.x + 4.5f, spdMinusP0.y + 0.5f), IM_COL32(220, 230, 245, 255), "-");
    if (isHoverSpdMinus) {
        ImGui::SetTooltip("Slower Waterfall Speed (-)\nCurrent Span: %.1fs", m_waterfallTimeSpan);
        if (io.MouseClicked[0]) {
            m_waterfallTimeSpan = std::min(8.0f, m_waterfallTimeSpan + 0.4f);
        }
    }

    // Speed Gauge Indicator on vertical track
    float trackStartY = spdMinusP1.y + 4.0f;
    float trackEndY = canvasP1.y - sbH - 4.0f;
    if (trackEndY > trackStartY + 12.0f) {
        float speedRatio = std::clamp((8.0f - m_waterfallTimeSpan) / 7.0f, 0.05f, 1.0f);
        float meterH = (trackEndY - trackStartY) * speedRatio;
        drawList->AddRectFilled(
            ImVec2(canvasP1.x - sbW + 5.0f, trackEndY - meterH),
            ImVec2(canvasP1.x - 5.0f, trackEndY),
            IM_COL32(0, 173, 181, 160),
            1.5f
        );
        drawList->AddRect(
            ImVec2(canvasP1.x - sbW + 4.0f, trackStartY),
            ImVec2(canvasP1.x - 4.0f, trackEndY),
            IM_COL32(40, 50, 65, 200),
            1.5f
        );
    }

    // -------------------------------------------------------------------------
    // 3. Bottom Horizontal Bar: Scroll X Track with [−] and [+] Key Zoom Buttons
    // -------------------------------------------------------------------------
    float hTrackRight = (canvasP1.x - sbW) - (btnSize * 2.0f + 4.0f);
    ImVec2 hTrackP0 = ImVec2(canvasP0.x, canvasP1.y - sbH);
    ImVec2 hTrackP1 = ImVec2(hTrackRight, canvasP1.y);

    drawList->AddRectFilled(hTrackP0, hTrackP1, IM_COL32(16, 20, 26, 255));
    drawList->AddRect(hTrackP0, hTrackP1, IM_COL32(32, 40, 52, 255));

    float hTrackW = hTrackP1.x - hTrackP0.x;
    if (totalKeysW > visualW && hTrackW > 30.0f) {
        float visibleRatio = std::clamp(visualW / totalKeysW, 0.06f, 1.0f);
        float thumbW = std::clamp(visibleRatio * hTrackW, 25.0f, hTrackW);
        float scrollRatio = (maxScrollX > 0.0f) ? std::clamp(m_waterfallScrollX / maxScrollX, 0.0f, 1.0f) : 0.0f;
        float thumbX0 = hTrackP0.x + scrollRatio * (hTrackW - thumbW);
        float thumbX1 = thumbX0 + thumbW;

        bool isHoverThumb = canInteract && io.MousePos.x >= thumbX0 && io.MousePos.x <= thumbX1 &&
                            io.MousePos.y >= hTrackP0.y && io.MousePos.y <= hTrackP1.y;
        bool isHoverTrack = canInteract && io.MousePos.x >= hTrackP0.x && io.MousePos.x <= hTrackP1.x &&
                            io.MousePos.y >= hTrackP0.y && io.MousePos.y <= hTrackP1.y;

        ImU32 thumbCol = isHoverThumb ? IM_COL32(75, 92, 118, 255) : IM_COL32(50, 62, 80, 255);
        if (canInteract && isHoverTrack && io.MouseClicked[0]) {
            m_isDraggingWaterfallThumbX = true;
        }

        if (m_isDraggingWaterfallThumbX) {
            if (io.MouseDown[0]) {
                thumbCol = IM_COL32(0, 173, 181, 255);
                float clickRatio = (io.MousePos.x - hTrackP0.x - thumbW * 0.5f) / std::max(1.0f, hTrackW - thumbW);
                clickRatio = std::clamp(clickRatio, 0.0f, 1.0f);
                m_waterfallScrollX = clickRatio * maxScrollX;
                scrollRatio = (maxScrollX > 0.0f) ? std::clamp(m_waterfallScrollX / maxScrollX, 0.0f, 1.0f) : 0.0f;
                thumbX0 = hTrackP0.x + scrollRatio * (hTrackW - thumbW);
                thumbX1 = thumbX0 + thumbW;
            } else {
                m_isDraggingWaterfallThumbX = false;
            }
        }

        drawList->AddRectFilled(ImVec2(thumbX0, hTrackP0.y + 2.0f), ImVec2(thumbX1, hTrackP1.y - 2.0f), thumbCol, 3.0f);
    } else {
        m_isDraggingWaterfallThumbX = false;
    }

    // Horizontal Zoom [−] Button (Narrows piano keys)
    ImVec2 zMinusP0 = ImVec2(hTrackRight + 2.0f, canvasP1.y - sbH);
    ImVec2 zMinusP1 = ImVec2(zMinusP0.x + btnSize, canvasP1.y);
    bool isHoverZMinus = canInteract && io.MousePos.x >= zMinusP0.x && io.MousePos.x <= zMinusP1.x &&
                         io.MousePos.y >= zMinusP0.y && io.MousePos.y <= zMinusP1.y;
    ImU32 zMinusBg = isHoverZMinus ? (io.MouseDown[0] ? IM_COL32(0, 150, 160, 255) : IM_COL32(45, 55, 72, 255)) : IM_COL32(24, 30, 40, 255);
    drawList->AddRectFilled(zMinusP0, zMinusP1, zMinusBg, 2.0f);
    drawList->AddRect(zMinusP0, zMinusP1, IM_COL32(42, 52, 68, 255), 2.0f);
    drawList->AddText(ImVec2(zMinusP0.x + 4.5f, zMinusP0.y + 0.5f), IM_COL32(220, 230, 245, 255), "-");
    if (isHoverZMinus) {
        ImGui::SetTooltip("Narrow Piano Keys (Zoom Out X)");
        if (io.MouseClicked[0]) {
            m_waterfallZoomX = std::max(1.0f, m_waterfallZoomX * 0.82f);
            if (m_waterfallZoomX <= 1.05f) {
                m_waterfallZoomX = 1.0f;
                m_waterfallScrollX = 0.0f;
            }
        }
    }

    // Horizontal Zoom [+] Button (Widens piano keys)
    ImVec2 zPlusP0 = ImVec2(zMinusP1.x + 2.0f, canvasP1.y - sbH);
    ImVec2 zPlusP1 = ImVec2(zPlusP0.x + btnSize, canvasP1.y);
    bool isHoverZPlus = canInteract && io.MousePos.x >= zPlusP0.x && io.MousePos.x <= zPlusP1.x &&
                        io.MousePos.y >= zPlusP0.y && io.MousePos.y <= zPlusP1.y;
    ImU32 zPlusBg = isHoverZPlus ? (io.MouseDown[0] ? IM_COL32(0, 150, 160, 255) : IM_COL32(45, 55, 72, 255)) : IM_COL32(24, 30, 40, 255);
    drawList->AddRectFilled(zPlusP0, zPlusP1, zPlusBg, 2.0f);
    drawList->AddRect(zPlusP0, zPlusP1, IM_COL32(42, 52, 68, 255), 2.0f);
    drawList->AddText(ImVec2(zPlusP0.x + 3.5f, zPlusP0.y + 0.5f), IM_COL32(220, 230, 245, 255), "+");
    if (isHoverZPlus) {
        ImGui::SetTooltip("Widen Piano Keys (Zoom In X)");
        if (io.MouseClicked[0]) {
            m_waterfallZoomX = std::min(4.0f, m_waterfallZoomX * 1.22f);
        }
    }

    // -------------------------------------------------------------------------
    // 4. Bottom-Right Corner Junction: Reset Zoom & Speed Vector Arrow
    // -------------------------------------------------------------------------
    ImVec2 cornerP0 = ImVec2(canvasP1.x - sbW, canvasP1.y - sbH);
    ImVec2 cornerP1 = ImVec2(canvasP1.x, canvasP1.y);
    bool isHoverCorner = canInteract && io.MousePos.x >= cornerP0.x && io.MousePos.x <= cornerP1.x &&
                         io.MousePos.y >= cornerP0.y && io.MousePos.y <= cornerP1.y;
    ImU32 cornerBg = isHoverCorner ? IM_COL32(36, 46, 62, 255) : IM_COL32(18, 22, 28, 255);
    drawList->AddRectFilled(cornerP0, cornerP1, cornerBg);

    ImVec2 cornerCenter = ImVec2((cornerP0.x + cornerP1.x) * 0.5f, (cornerP0.y + cornerP1.y) * 0.5f);
    ImU32 arrowCol = isHoverCorner ? IM_COL32(0, 240, 255, 255) : IM_COL32(140, 160, 185, 200);
    DrawCircularResetArrow(drawList, cornerCenter, 4.5f, arrowCol, 1.4f);

    if (isHoverCorner) {
        ImGui::SetTooltip("Reset Waterfall Zoom & Speed");
        if (io.MouseClicked[0]) {
            m_waterfallZoomX = 1.0f;
            m_waterfallScrollX = 0.0f;
            m_waterfallTimeSpan = 3.5f;
        }
    }
}

} // namespace vgmstudio
