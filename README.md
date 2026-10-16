# VGM Studio

A modern, high-performance **VGM / VGZ Chiptune Workstation, Player, and Analyzer**.

VGMStudio brings together retro video game music playback, per-channel oscilloscope visualization, FM patch / preset extraction, PCM sample bank slicing with piano audition, instruction-level disassembly, and real-time mixing in a responsive, dark-themed workstation.

---

## Features

### Chiptune & Sound Chip Audio Engine
- **Powered by libvgm cores**: Cycle-accurate sound chip emulation with band-limited audio resampling.
- **Vast Sound Chip Support**:
  - **Yamaha FM**: YM2612 (OPN2 / Sega Genesis/Mega Drive), YM2151 (OPM), YM2203 (OPN), YM2608 (OPNA), YM2610 (OPNB / Neo Geo), YM2413 (OPLL / MSX / Master System), YM3812 (OPL2 / AdLib), YMF262 (OPL3 / Sound Blaster 16), YMF271, YMF278B (OPL4).
  - **PSG & Square Waves**: SN76489 / SN76496 (SEGA PSG), AY-3-8910 / YM2149 (SSG), SAA1099.
  - **PCM / ADPCM / Wavetable**: SegaPCM, Ricoh RF5C68 / RF5C164 (Sega CD), Yamaha YMW258 (MultiPCM), HuC6280 (PC Engine / TurboGrafx-16), OKI MSM6295, OKI MSM6258, Capcom QSound, Sega SCSP, Konami K051649 (SCC).
  - **Console APUs**: Nintendo Entertainment System APU (with FDS sound extension), Game Boy DMG APU.

### Real-Time Channel Rack & Mixer
- **Individual Mute / Solo Controls**: Instant isolation of individual audio channels, including separate FM and DAC handling on hybrid chips like YM2612.
- **Volume & Stereo Pan**: Per-channel gain and pan adjustments.
- **Live Channel Oscilloscopes**: Real-time waveform rendering for every active sound chip channel.
- **Stereo Master Peak Meters**: Accurate dB/RMS meter monitoring.

### Sample Viewer & Slice Inspector
- **PCM Bank & Slice Detection**: Automatically inspects PCM Data Blocks, sound banks, and in-song slice regions across SegaPCM, RF5C164, MultiPCM, and YM2612 DAC.
- **Waveform Visualizer**: Zoomable and scrollable waveform view displaying sample rate, length, loop start/end points, and slice boundaries.
- **Interactive Piano Audition**: Play-test sample slices across chromatic musical notes using an on-screen keyboard or computer keyboard.

### VGM Presets & Patches Viewer
- **FM Patch Extraction**: Extract operator parameters (Algorithm, Feedback, Total Level, Multiple, Attack Rate, Decay Rate, Sustain Level, Release Rate, Detune, SSG-EG) directly from songs.
- **Live Patch Audition**: Test FM voices in real-time with an interactive polyphonic keyboard supporting chords.
- **Computer Keyboard Mapping**: Play notes using `A S D F` for white keys and `Q W E R T Y` for black keys, with octave shifting.

### Instruction Disassembly & Event Log
- **VGM Command Inspector**: Disassemble and inspect all VGM commands with sample timestamps.
- **Filter by Sound Chip & Channel**: View global events or isolate commands specifically directed to `[YM2612] Ch 6 / DAC`, `[SEGA PSG] Square 1`, etc.
- **Key-On / Key-Off Indicators**: Quick visual cues for note activation and silence triggers.

### ⚡ Playback Controls & Pitch / Speed
- **Variable Playback Speed**: Smooth tempo control from 0.25x to 4.0x with optional pitch preservation.
- **Musical Transposition**: Shift pitch in real-time up or down by ±12 semitones.
- **Looping & Seeking**: Seamless seek bar, loop counter, and smooth fade-out.

---

## ⌨️ Controls & Shortcuts

| Action | Shortcut |
|---|---|
| **Play / Pause** | `Space` |
| **Rewind to Beginning** | `Home` or `R` |
| **Seek -5s / +5s** | `Left Arrow` / `Right Arrow` |
| **Seek -10s / +10s** | `Shift + Left Arrow` / `Shift + Right Arrow` |
| **Mute Channel** | Click `M` on channel strip |
| **Solo Channel** | Click `S` on channel strip |

### 🎹 Piano Audition Keyboard Layout
When auditing in **Sample Viewer** or **VGM Presets & Patches**:
- **White Keys**: `A` (C), `S` (D), `D` (E), `F` (F), `G` (G), `H` (A), `J` (B), `K` (C+1), `L` (D+1), `;` (E+1)...
- **Black Keys**: `W` (C#), `E` (D#), `T` (F#), `Y` (G#), `U` (A#), `O` (C#+1), `P` (D#+1)...
- **Octave Down / Up**: `Z` / `X`

---

## 🛠️ Building from Source

### Prerequisites

- **C++17 Compiler** (`g++` >= 9 or `clang++` >= 10)
- **CMake** (>= 3.16)
- **SDL2** Development Libraries (`SDL2` >= 2.0.12)
- **OpenGL** Development Headers & Mesa
- **zlib** (`zlib` headers, or use bundled version in `vendor/zlib`)

---

### 🐧 Linux Build

#### 1. Install Dependencies

**Ubuntu / Debian / Linux Mint:**
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libsdl2-dev libgl1-mesa-dev zlib1g-dev
```

**Arch Linux / Manjaro:**
```bash
sudo pacman -S --needed base-devel cmake sdl2 zlib mesa
```

**Fedora / RHEL:**
```bash
sudo dnf install -y gcc-c++ cmake SDL2-devel mesa-libGL-devel zlib-devel
```

#### 2. Build

```bash
# Clone the repository
git clone https://github.com/kevinadhaikal/vgmstudio.git
cd vgmstudio

# Build using the Makefile wrapper
make -j$(nproc)

# Or build manually using CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run VGM Studio
./vgmstudio
```

---

### 🪟 Windows Build

#### Option A: Cross-Compiling from Linux using MinGW-w64
VGM Studio includes a MinGW-w64 toolchain and bundled Windows binaries for SDL2 and zlib in `vendor/`.

1. Install MinGW-w64:
   ```bash
   sudo apt-get install mingw-w64   # Ubuntu/Debian
   sudo pacman -S mingw-w64-gcc     # Arch Linux
   ```
2. Build Windows executable:
   ```bash
   make windows
   ```
3. The compiled `vgmstudio.exe` and `SDL2.dll` will be output in `build-win/`.

#### Option B: Native Windows Build using MSYS2
1. Open **MSYS2 MinGW 64-bit** terminal:
   ```bash
   pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-make mingw-w64-x86_64-SDL2
   ```
2. Build:
   ```bash
   cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j
   ```
3. Run `build/vgmstudio.exe`.

---

## 📁 Project Structure

```
vgmstudio/
├── CMakeLists.txt        # Main CMake configuration
├── Makefile              # Convenient build wrapper (make, make windows, make clean)
├── README.md             # Project documentation & guide
├── cmake/                # CMake toolchain definitions (MinGW-w64 cross-compile)
├── samples/              # Curated VGZ test samples across various chips
├── src/
│   ├── app/              # Application lifecycle, SDL2/GL window, theme manager
│   ├── audio/            # AudioEngine, SDL audio callback, mixer, pitch shifting
│   ├── chips/            # libvgm sound chip emulator implementations
│   ├── emu/              # Resamplers, sound device dispatchers, channel mixer
│   ├── ui/               # ImGui views (Channel Rack, Sample Viewer, Presets, Disassembly...)
│   └── vgm/              # VGM/VGZ file parsers, player core, note tracker, patch extractor
└── vendor/               # Bundled third-party libraries & dependencies
    ├── imgui/            # Dear ImGui (docking branch) with SDL2 & OpenGL3 backends
    ├── sdl2-windows/     # Bundled 64-bit Windows SDL2 development headers & DLL
    └── zlib/             # Bundled zlib compression library
```

---

## 💖 Credits & Acknowledgments

VGM Studio is built with gratitude to the open-source chiptune and emulation community:

- **[libvgm](https://github.com/ValleyBell/libvgm)** by Valley Bell and contributors – World-class chiptune sound core emulators and VGM playback framework.
- **[Dear ImGui](https://github.com/ocornut/imgui)** by Omar Cornut and the ImGui community – High-performance immediate-mode GUI with docking.
- **[Simple DirectMedia Layer (SDL2)](https://www.libsdl.org/)** – Cross-platform low-level audio, windowing, and input handling.
- **[zlib](https://zlib.net/)** by Jean-loup Gailly and Mark Adler – Industry-standard decompression library for VGZ support.
- **[Furnace Tracker](https://github.com/tildearrow/furnace)** by tildearrow – Inspiration for the color palette, channel rack aesthetic, and chiptune design.

---

## 📜 License

This project is licensed under the **GNU General Public License v2.0 or later** (GPLv2+), respecting the licensing of the incorporated libvgm sound emulation cores. See individual source files for third-party module licenses.
