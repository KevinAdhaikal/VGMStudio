cmake_minimum_required(VERSION 3.16)
project(vgmstudio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Global compiler definitions
add_definitions(-DHAVE_STDINT_H -DVGM_LITTLE_ENDIAN)

if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function)
endif()

# Include directories
include_directories(
    src
    src/emu
    src/chips
    src/vgm
    src/audio
    src/ui
    src/third_party/imgui
    src/third_party/imgui/backends
)

# ImGui sources
set(IMGUI_SRCS
    src/third_party/imgui/imgui.cpp
    src/third_party/imgui/imgui_demo.cpp
    src/third_party/imgui/imgui_draw.cpp
    src/third_party/imgui/imgui_tables.cpp
    src/third_party/imgui/imgui_widgets.cpp
    src/third_party/imgui/backends/imgui_impl_sdl2.cpp
    src/third_party/imgui/backends/imgui_impl_opengl3.cpp
)

# Application & UI sources
set(APP_SRCS
    src/main.cpp
    src/app/app.cpp
    src/app/theme.cpp
    src/ui/icons.cpp
    src/ui/file_dialog.cpp
    src/ui/transport_bar.cpp
    src/ui/channel_rack.cpp
    src/ui/song_info.cpp
    src/ui/disassembly_view.cpp
    src/ui/piano_roll.cpp
    src/ui/edit_song_dialog.cpp
    src/ui/settings_dialog.cpp
    src/ui/speed_pitch_view.cpp
    src/ui/sample_viewer.cpp
    src/ui/oscilloscope_view.cpp
    src/ui/vgm_events_mixer.cpp
    src/ui/vgm_patch_viewer.cpp
    src/vgm/vgm_patch_extractor.cpp
    src/audio/audio_engine.cpp
)

# Sound Chip sources
file(GLOB ALL_CHIP_C_SRCS src/chips/*.c)
list(FILTER ALL_CHIP_C_SRCS EXCLUDE REGEX "adlibemu_opl_inc\\.c$")
list(FILTER ALL_CHIP_C_SRCS EXCLUDE REGEX "scsplfo\\.c$")

set(CHIP_CPP_SRCS
    src/chips/ymfmintf.cpp
    src/chips/ymfm/ymfm_opz.cpp
)

# Emu dispatcher and audio helper sources
set(EMU_C_SRCS
    src/emu/SoundEmu.c
    src/emu/Resampler.c
    src/emu/dac_control.c
    src/emu/panning.c
    src/emu/logging.c
    src/emu/channel_mixer.c
)

# VGM player engine sources
set(VGM_C_SRCS
    src/vgm/dblk_compr.c
    src/vgm/helper.c
    src/vgm/DataLoader.c
    src/vgm/FileLoader.c
    src/vgm/MemoryLoader.c
    src/vgm/StrUtils.c
)

set(VGM_CPP_SRCS
    src/vgm/vgmplayer.cpp
    src/vgm/vgmplayer_cmdhandler.cpp
    src/vgm/playerbase.cpp
    src/vgm/vgm_info.cpp
    src/vgm/vgm_disassembly.cpp
    src/vgm/note_tracker.cpp
)

set(ALL_SRCS
    ${IMGUI_SRCS}
    ${APP_SRCS}
    ${ALL_CHIP_C_SRCS}
    ${CHIP_CPP_SRCS}
    ${EMU_C_SRCS}
    ${VGM_C_SRCS}
    ${VGM_CPP_SRCS}
)

add_executable(vgmstudio ${ALL_SRCS})

if(WIN32)
    # Check for bundled SDL2 MinGW development package
    set(SDL2_WIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2-windows/x86_64-w64-mingw32")
    if(EXISTS "${SDL2_WIN_DIR}")
        target_include_directories(vgmstudio PRIVATE "${SDL2_WIN_DIR}/include/SDL2")
        target_link_directories(vgmstudio PRIVATE "${SDL2_WIN_DIR}/lib")
        target_link_libraries(vgmstudio PRIVATE mingw32 SDL2main SDL2)

        # Copy SDL2.dll next to output .exe
        add_custom_command(TARGET vgmstudio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL2_WIN_DIR}/bin/SDL2.dll"
            "$<TARGET_FILE_DIR:vgmstudio>/SDL2.dll"
            COMMENT "Copying SDL2.dll to binary directory"
        )
    else()
        find_package(SDL2 REQUIRED)
        target_include_directories(vgmstudio PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(vgmstudio PRIVATE ${SDL2_LIBRARIES})
    endif()

    # Build bundled zlib as static library
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/CMakeLists.txt")
        set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/zlib EXCLUDE_FROM_ALL)
        target_include_directories(vgmstudio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib" "${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib")
        target_link_libraries(vgmstudio PRIVATE zlibstatic)
    else()
        find_package(ZLIB REQUIRED)
        target_include_directories(vgmstudio PRIVATE ${ZLIB_INCLUDE_DIRS})
        target_link_libraries(vgmstudio PRIVATE ${ZLIB_LIBRARIES})
    endif()

    # Windows system libraries
    target_link_libraries(vgmstudio PRIVATE
        opengl32
        winmm
        imm32
        ole32
        oleaut32
        shell32
        version
        uuid
        setupapi
        m
        pthread
    )

    # Static link runtime so the .exe runs standalone without missing MinGW DLLs
    if(MINGW)
        target_link_options(vgmstudio PRIVATE -static)
    endif()

else()
    # Native Linux / Unix build
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(OPENGL REQUIRED gl)
    pkg_check_modules(ZLIB REQUIRED zlib)

    target_include_directories(vgmstudio PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${OPENGL_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIRS}
    )

    target_link_libraries(vgmstudio PRIVATE
        ${SDL2_LIBRARIES}
        ${OPENGL_LIBRARIES}
        ${ZLIB_LIBRARIES}
        m
        pthread
    )
endif()
