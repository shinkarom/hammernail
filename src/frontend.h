#pragma once
#include <vector>
#include <atomic>
#include <cstring>
#include <iostream>
#include <raylib.h>
#include "ICore.h"

// ============================================================================
// Lock-Free SPSC Audio Ring Buffer
// ============================================================================
class LockFreeAudioBuffer {
public:
    explicit LockFreeAudioBuffer(size_t capacitySamples = 32768)
        : data(capacitySamples, 0), capacity(capacitySamples), head(0), tail(0) {}

    void push(std::span<const int16_t> samples) {
        size_t currentHead = head.load(std::memory_order_relaxed);
        size_t currentTail = tail.load(std::memory_order_acquire);

        for (int16_t sample : samples) {
            size_t nextHead = (currentHead + 1) % capacity;
            if (nextHead == currentTail) break; // Drop if full
            data[currentHead] = sample;
            currentHead = nextHead;
        }

        head.store(currentHead, std::memory_order_release);
    }

    void pop(int16_t* out, size_t sampleCount) {
        size_t currentTail = tail.load(std::memory_order_relaxed);
        size_t currentHead = head.load(std::memory_order_acquire);

        size_t samplesRead = 0;
        while (samplesRead < sampleCount && currentTail != currentHead) {
            out[samplesRead++] = data[currentTail];
            currentTail = (currentTail + 1) % capacity;
        }

        if (samplesRead < sampleCount) {
            std::memset(out + samplesRead, 0, (sampleCount - samplesRead) * sizeof(int16_t));
        }

        tail.store(currentTail, std::memory_order_release);
    }

private:
    std::vector<int16_t> data;
    size_t capacity;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
};

// ============================================================================
// Input Binding Structure
// ============================================================================
struct InputBinding {
    KeyboardKey key;
    GamepadButton gamepadBtn;
    VirtualButton virtualBtn;
};

// Standard Default Mappings
static const std::vector<InputBinding> DEFAULT_INPUT_MAPPINGS = {
    { KEY_UP,        GAMEPAD_BUTTON_LEFT_FACE_UP,       VirtualButton::Up },
    { KEY_DOWN,      GAMEPAD_BUTTON_LEFT_FACE_DOWN,     VirtualButton::Down },
    { KEY_LEFT,      GAMEPAD_BUTTON_LEFT_FACE_LEFT,     VirtualButton::Left },
    { KEY_RIGHT,     GAMEPAD_BUTTON_LEFT_FACE_RIGHT,    VirtualButton::Right },

    { KEY_Z,         GAMEPAD_BUTTON_RIGHT_FACE_DOWN,    VirtualButton::ActionA },
    { KEY_X,         GAMEPAD_BUTTON_RIGHT_FACE_RIGHT,   VirtualButton::ActionB },
    { KEY_A,         GAMEPAD_BUTTON_RIGHT_FACE_LEFT,    VirtualButton::ActionX },
    { KEY_S,         GAMEPAD_BUTTON_RIGHT_FACE_UP,      VirtualButton::ActionY },

    { KEY_ENTER,     GAMEPAD_BUTTON_MIDDLE_RIGHT,       VirtualButton::Start },
    { KEY_SPACE,     GAMEPAD_BUTTON_MIDDLE_LEFT,        VirtualButton::Select },

    { KEY_Q,         GAMEPAD_BUTTON_LEFT_TRIGGER_1,     VirtualButton::L1 },
    { KEY_W,         GAMEPAD_BUTTON_RIGHT_TRIGGER_1,    VirtualButton::R1 },
    { KEY_E,         GAMEPAD_BUTTON_LEFT_TRIGGER_2,     VirtualButton::L2 },
    { KEY_R,         GAMEPAD_BUTTON_RIGHT_TRIGGER_2,    VirtualButton::R2 },
};

// ============================================================================
// Reusable Application Frontend
// ============================================================================
class Frontend {
public:
    explicit Frontend(ICore& core) 
        : m_core(core), m_audioBuffer(32768) {
        s_activeBuffer = &m_audioBuffer;
    }

    ~Frontend() {
        s_activeBuffer = nullptr;
    }

    int run(int initialWidth = 640, int initialHeight = 480) {
        CoreConfig config = m_core.getConfig();

        // Window Initialization
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
        InitWindow(initialWidth, initialHeight, config.title.c_str());
        SetWindowMinSize(160, 120);

        // Audio Setup
        InitAudioDevice();
        SetAudioStreamBufferSizeDefault(2048);
        AudioStream audioStream = LoadAudioStream(config.sampleRate, 16, config.audioChannels);
        SetAudioStreamCallback(audioStream, [](void* bufferData, unsigned int frames) {
            if (s_activeBuffer) {
                // Determine channel multiplier
                s_activeBuffer->pop(static_cast<int16_t*>(bufferData), frames * 2);
            }
        });
        PlayAudioStream(audioStream);

        // Texture Setup
        Image blank = GenImageColor(config.vramWidth, config.vramHeight, BLANK);
        ImageFormat(&blank, config.pixelFormat);
        Texture2D fbTexture = LoadTextureFromImage(blank);
        UnloadImage(blank);
        SetTextureFilter(fbTexture, TEXTURE_FILTER_POINT);

        const double frameTime = 1.0 / config.targetFps;
        double accumulator = 0.0;

        // Main Loop
        while (!WindowShouldClose()) {
            if (IsKeyPressed(KEY_F11) || (IsKeyDown(KEY_LEFT_ALT) && IsKeyPressed(KEY_ENTER))) {
                ToggleFullscreen();
            }

            pollInputs();

            double delta = GetFrameTime();
            if (delta > 0.25) delta = 0.25;
            accumulator += delta;

            bool newFrame = false;
            while (accumulator >= frameTime) {
                m_core.stepFrame();
                accumulator -= frameTime;
                newFrame = true;

                m_audioBuffer.push(m_core.getAudioSamples());
                m_core.clearAudioSamples();
            }

            if (newFrame) {
                const void* pixels = m_core.getFramebuffer();
                if (pixels) {
                    UpdateTexture(fbTexture, pixels);
                }
            }

            // Display Region & Aspect Ratio Scaling
            DisplayRegion disp = m_core.getDisplayRegion();
            if (disp.width <= 0 || disp.height <= 0) {
                disp.x = 0; disp.y = 0;
                disp.width = config.vramWidth;
                disp.height = config.vramHeight;
            }

            Rectangle sourceRec = {
                static_cast<float>(disp.x),
                static_cast<float>(disp.y),
                static_cast<float>(disp.width),
                static_cast<float>(disp.height)
            };

            float screenW = static_cast<float>(GetScreenWidth());
            float screenH = static_cast<float>(GetScreenHeight());
            
            float targetAspect = config.targetAspectRatio;
            if (targetAspect <= 0.0f) {
                targetAspect = static_cast<float>(disp.width) / static_cast<float>(disp.height);
            }

            float targetW, targetH;
            float offsetX = 0.0f;
            float offsetY = 0.0f;

            if ((screenW / screenH) > targetAspect) {
                targetH = screenH;
                targetW = screenH * targetAspect;
                offsetX = (screenW - targetW) * 0.5f;
            } else {
                targetW = screenW;
                targetH = screenW / targetAspect;
                offsetY = (screenH - targetH) * 0.5f;
            }

            Rectangle destRec = { offsetX, offsetY, targetW, targetH };

            BeginDrawing();
            ClearBackground(BLACK);
            DrawTexturePro(fbTexture, sourceRec, destRec, Vector2{ 0.0f, 0.0f }, 0.0f, WHITE);
            EndDrawing();
        }

        // Cleanup
        UnloadTexture(fbTexture);
        UnloadAudioStream(audioStream);
        CloseAudioDevice();
        CloseWindow();

        return 0;
    }

private:
    void pollInputs() {
        constexpr int PORT = 0;
        bool gamepadAvailable = IsGamepadAvailable(0);

        for (const auto& map : DEFAULT_INPUT_MAPPINGS) {
            bool pressed = IsKeyDown(map.key);
            if (gamepadAvailable && IsGamepadButtonDown(0, map.gamepadBtn)) {
                pressed = true;
            }
            m_core.setButton(PORT, map.virtualBtn, pressed);
        }

        if (gamepadAvailable) {
            m_core.setButton(PORT, VirtualButton::L3, IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_THUMB));
            m_core.setButton(PORT, VirtualButton::R3, IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_THUMB));

            m_core.setAxis(PORT, VirtualAxis::LeftX,  GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X));
            m_core.setAxis(PORT, VirtualAxis::LeftY,  GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y));
            m_core.setAxis(PORT, VirtualAxis::RightX, GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X));
            m_core.setAxis(PORT, VirtualAxis::RightY, GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y));
        }
    }

    ICore& m_core;
    LockFreeAudioBuffer m_audioBuffer;
    static inline LockFreeAudioBuffer* s_activeBuffer = nullptr;
};