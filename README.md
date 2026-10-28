# Ultrapatcher

Free, open-source Patcher-style plugin host with custom Surface GUIs. AGPLv3.

Status: milestone 1. Hosts one VST3 plugin inside the plugin, with scan, load, state save/restore and a floating plugin window. With nothing loaded, audio and MIDI pass through.

## Build

Requires CMake 3.22+ and a C++17 compiler. JUCE is a git submodule.

    git submodule update --init
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target Ultrapatcher_Standalone
    cmake --build build --target Ultrapatcher_VST3

Linux needs the usual JUCE dev packages (ALSA, FreeType, X11, GL).
