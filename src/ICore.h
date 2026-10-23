#pragma once
#include <span>
#include <cstdint>
#include <string>
#include <raylib.h>

// Standardized Inputs across all fantasy consoles/emulators
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
    float targetAspectRatio = 4.0f / 3.0f; // Set <= 0 for dynamic aspect ratio
    
    // Audio
    int sampleRate = 44100;
    int audioChannels = 2; // 1 = Mono, 2 = Stereo
};

class ICore {
public:
    virtual ~ICore() = default;

    // Configuration provided to the frontend at startup
    virtual CoreConfig getConfig() const = 0;

    // Called once per frame to step emulation CPU/GPU logic
    virtual void stepFrame() = 0;

    // Graphics
    virtual const void* getFramebuffer() const = 0;
    virtual DisplayRegion getDisplayRegion() const = 0;

    // Audio
    virtual std::span<const int16_t> getAudioSamples() = 0;
    virtual void clearAudioSamples() = 0;

    // Input Handling
    virtual void setButton(int port, VirtualButton btn, bool pressed) = 0;
    virtual void setAxis(int port, VirtualAxis axis, float value) = 0;
};