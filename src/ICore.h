#pragma once
#include <span>
#include <cstdint>
#include <string>
#include <string_view>
#include <raylib.h>

enum class VirtualButton {
    Up, Down, Left, Right,
    ActionA, ActionB, ActionX, ActionY,
    Start, Select,
    L1, R1, L2, R2, L3, R3
};

enum class VirtualAxis {
    LeftX, LeftY, RightX, RightY
};

struct DisplayRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct CoreConfig {
    std::string title = "Fantasy Console";
    double targetFps = 60.0;
    
    // Video
    int vramWidth = 320;
    int vramHeight = 240;
    PixelFormat pixelFormat = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    float targetAspectRatio = 4.0f / 3.0f;
    
    // Audio
    int sampleRate = 44100;
    int audioChannels = 2;
};

class ICore {
public:
    virtual ~ICore() = default;

    // --- Core File / Memory Management ---

    // The core opens, parses, and loads the file path into its internal memory/registers
    virtual bool loadRomFile(std::string_view filePath) = 0;

    // Optional: Load directly from memory (useful for WebAssembly or embedded files)
    virtual bool loadRomData(std::span<const uint8_t> data) { return false; }

    // Unload game, clear RAM/registers
    virtual void unloadRom() = 0;

    // Check if system is ready to step
    virtual bool isRomLoaded() const = 0;

    // Reset CPU/RAM state without unloading the ROM
    virtual void reset() = 0;

    // --- System Logic ---
    virtual CoreConfig getConfig() const = 0;
    virtual void stepFrame() = 0;

    // --- Graphics ---
    virtual const void* getFramebuffer() const = 0;
    virtual DisplayRegion getDisplayRegion() const = 0;

    // --- Audio ---
    virtual std::span<const int16_t> getAudioSamples() = 0;
    virtual void clearAudioSamples() = 0;

    // --- Input ---
    virtual void setButton(int port, VirtualButton btn, bool pressed) = 0;
    virtual void setAxis(int port, VirtualAxis axis, float value) = 0;
};