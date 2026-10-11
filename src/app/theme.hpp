#pragma once

namespace vgmstudio {
namespace theme {

// Theme presets
enum class ThemePreset {
    Furnace = 0,
    ClassicTracker = 1,
    Cyberpunk = 2,
    Monokai = 3
};

void ApplyFurnaceTheme();
void ApplyClassicTrackerTheme();
void ApplyCyberpunkTheme();
void ApplyMonokaiTheme();
void ApplyTheme(int presetIndex);

} // namespace theme
} // namespace vgmstudio
