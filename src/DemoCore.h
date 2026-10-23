#pragma once
#include <vector>
#include <cmath>
#include "ICore.h"

class DemoCore : public ICore {
public:
    DemoCore() : framebuffer(320 * 240, 0xFF000000) {}

    CoreConfig getConfig() const override {
        CoreConfig config;
        config.title = "Frontend Test (Use Arrow Keys & Z)";
        config.targetFps = 60.0;
        config.vramWidth = 320;
        config.vramHeight = 240;
        config.targetAspectRatio = 4.0f / 3.0f;
        config.pixelFormat = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        config.sampleRate = 44100;
        config.audioChannels = 2;
        return config;
    }

    void stepFrame() override {
        if (btnUp && playerY > 10) playerY -= 3;
        if (btnDown && playerY < 240 - 40) playerY += 3;
        if (btnLeft && playerX > 10) playerX -= 3;
        if (btnRight && playerX < 320 - 40) playerX += 3;

        frameCounter++;

        // Render Background Plasma
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 320; ++x) {
                uint8_t r = static_cast<uint8_t>((std::sin(x * 0.05 + frameCounter * 0.05) + 1.0) * 64);
                uint8_t g = static_cast<uint8_t>((std::cos(y * 0.05 + frameCounter * 0.05) + 1.0) * 64);
                uint8_t b = static_cast<uint8_t>(128);
                framebuffer[y * 320 + x] = 0xFF000000 | (b << 16) | (g << 8) | r;
            }
        }

        // Render Square
        uint32_t color = btnActionA ? 0xFF00FFFF : 0xFF00FF00;
        for (int py = 0; py < 30; ++py) {
            for (int px = 0; px < 30; ++px) {
                int drawX = playerX + px;
                int drawY = playerY + py;
                if (drawX >= 0 && drawX < 320 && drawY >= 0 && drawY < 240) {
                    framebuffer[drawY * 320 + drawX] = color;
                }
            }
        }

        // Generate Audio Tone on Z press
        audioBuffer.clear();
        int samplesPerFrame = 44100 / 60;
        double frequency = btnActionA ? 440.0 : 220.0;

        for (int i = 0; i < samplesPerFrame; ++i) {
            int16_t sample = 0;
            if (btnActionA) {
                sample = static_cast<int16_t>(std::sin(audioPhase) * 8000.0);
                audioPhase += (2.0 * 3.141592653589793 * frequency) / 44100.0;
                if (audioPhase >= 2.0 * 3.141592653589793) audioPhase -= 2.0 * 3.141592653589793;
            }
            audioBuffer.push_back(sample); // L
            audioBuffer.push_back(sample); // R
        }
    }

    const void* getFramebuffer() const override { return framebuffer.data(); }
    DisplayRegion getDisplayRegion() const override { return { 0, 0, 320, 240 }; }

    std::span<const int16_t> getAudioSamples() override { return audioBuffer; }
    void clearAudioSamples() override { audioBuffer.clear(); }

    void setButton(int port, VirtualButton btn, bool pressed) override {
        if (port != 0) return;
        switch (btn) {
            case VirtualButton::Up:      btnUp = pressed; break;
            case VirtualButton::Down:    btnDown = pressed; break;
            case VirtualButton::Left:    btnLeft = pressed; break;
            case VirtualButton::Right:   btnRight = pressed; break;
            case VirtualButton::ActionA: btnActionA = pressed; break;
            default: break;
        }
    }

    void setAxis(int port, VirtualAxis axis, float value) override {}

private:
    std::vector<uint32_t> framebuffer;
    std::vector<int16_t> audioBuffer;
    int playerX = 145;
    int playerY = 105;
    uint32_t frameCounter = 0;
    double audioPhase = 0.0;

    bool btnUp = false;
    bool btnDown = false;
    bool btnLeft = false;
    bool btnRight = false;
    bool btnActionA = false;
};