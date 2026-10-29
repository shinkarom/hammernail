#pragma once
#include <vector>
#include <atomic>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <raylib.h>
#include "ICore.h"

// ============================================================================
// Lock-Free SPSC Audio Ring Buffer
// ============================================================================
class LockFreeAudioBuffer {
public:
    explicit LockFreeAudioBuffer(size_t capacitySamples = 32768)
        : m_data(capacitySamples, 0), m_capacity(capacitySamples), m_head(0), m_tail(0) {}

    void push(std::span<const int16_t> samples) {
        size_t currentHead = m_head.load(std::memory_order_relaxed);
        size_t currentTail = m_tail.load(std::memory_order_acquire);

        for (int16_t sample : samples) {
            size_t nextHead = (currentHead + 1) % m_capacity;
            if (nextHead == currentTail) break; // Buffer full, drop overflow
            m_data[currentHead] = sample;
            currentHead = nextHead;
        }

        m_head.store(currentHead, std::memory_order_release);
    }

    void pop(int16_t* out, size_t sampleCount) {
        size_t currentTail = m_tail.load(std::memory_order_relaxed);
        size_t currentHead = m_head.load(std::memory_order_acquire);

        size_t samplesRead = 0;
        while (samplesRead < sampleCount && currentTail != currentHead) {
            out[samplesRead++] = m_data[currentTail];
            currentTail = (currentTail + 1) % m_capacity;
        }

        // Fill remaining buffer space with silence if underflowed
        if (samplesRead < sampleCount) {
            std::memset(out + samplesRead, 0, (sampleCount - samplesRead) * sizeof(int16_t));
        }

        m_tail.store(currentTail, std::memory_order_release);
    }

    void clear() {
        m_head.store(0, std::memory_order_relaxed);
        m_tail.store(0, std::memory_order_release);
    }

private:
    std::vector<int16_t> m_data;
    size_t m_capacity;
    std::atomic<size_t> m_head;
    std::atomic<size_t> m_tail;
};

// ============================================================================
// Input Binding Structure
// ============================================================================
struct InputBinding {
    KeyboardKey key;
    GamepadButton gamepadBtn;
    VirtualButton virtualBtn;
};

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

    // Passes a file path string directly to the Core for loading
    bool loadRom(std::string_view filePath) {
        if (filePath.empty()) return false;
        
        bool success = m_core.loadRomFile(filePath);
        if (success) {
            m_audioBuffer.clear();
            m_isPaused = false;
            TraceLog(LOG_INFO, "Frontend: Successfully instructed core to load '%s'", std::string(filePath).c_str());
        } else {
            TraceLog(LOG_ERROR, "Frontend: Core failed to load '%s'", std::string(filePath).c_str());
        }
        return success;
    }

    // Run window loop with optional initial ROM path
    int run(std::string_view initialRomPath = "", int initialWidth = 960, int initialHeight = 720) {
        // Attempt loading initial ROM if provided
        if (!initialRomPath.empty()) {
            loadRom(initialRomPath);
        }

        CoreConfig config = m_core.getConfig();
        m_audioChannels = config.audioChannels > 0 ? config.audioChannels : 2;

        // Window Initialization
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
        InitWindow(initialWidth, initialHeight, config.title.c_str());
        SetWindowMinSize(320, 240);

        // Audio Setup
        InitAudioDevice();
        SetAudioStreamBufferSizeDefault(2048);
        AudioStream audioStream = LoadAudioStream(config.sampleRate, 16, m_audioChannels);
        
        s_audioChannels = m_audioChannels;
        SetAudioStreamCallback(audioStream, [](void* bufferData, unsigned int frames) {
            if (s_activeBuffer) {
                s_activeBuffer->pop(static_cast<int16_t*>(bufferData), frames * s_audioChannels);
            }
        });
        PlayAudioStream(audioStream);

        // Framebuffer Texture Setup
        Image blank = GenImageColor(config.vramWidth, config.vramHeight, BLANK);
        ImageFormat(&blank, config.pixelFormat);
        Texture2D fbTexture = LoadTextureFromImage(blank);
        UnloadImage(blank);
        SetTextureFilter(fbTexture, TEXTURE_FILTER_POINT);

        double targetFps = config.targetFps > 0.0 ? config.targetFps : 60.0;
        const double frameTime = 1.0 / targetFps;
        double accumulator = 0.0;

        // Main Loop
        while (!WindowShouldClose()) {
            // Drag-and-Drop: Forward dropped path directly to core
            handleFileDrop();

            // Window & Hotkeys
            if (IsKeyPressed(KEY_F11) || (IsKeyDown(KEY_LEFT_ALT) && IsKeyPressed(KEY_ENTER))) {
                ToggleFullscreen();
            }
            if (IsKeyPressed(KEY_F5) && m_core.isRomLoaded()) {
                m_core.reset();
                m_audioBuffer.clear();
            }
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_PAUSE)) {
                m_isPaused = !m_isPaused;
            }

            // Input Handling
            pollInputs();

            // Emulation Step Loop
            bool newFrame = false;
            if (m_core.isRomLoaded() && !m_isPaused) {
                double delta = GetFrameTime();
                if (delta > 0.25) delta = 0.25; // Prevent spiral of death
                accumulator += delta;

                while (accumulator >= frameTime) {
                    m_core.stepFrame();
                    accumulator -= frameTime;
                    newFrame = true;

                    // Stream audio from core
                    m_audioBuffer.push(m_core.getAudioSamples());
                    m_core.clearAudioSamples();
                }
            }

            // Texture Update
            if (newFrame) {
                const void* pixels = m_core.getFramebuffer();
                if (pixels) {
                    UpdateTexture(fbTexture, pixels);
                }
            }

            // Dynamic Viewport / Aspect Ratio Calculation
            DisplayRegion disp = m_core.getDisplayRegion();
            if (disp.width <= 0 || disp.height <= 0) {
                disp.x = 0; 
                disp.y = 0;
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

            // Render Frame
            BeginDrawing();
                ClearBackground(BLACK);

                if (m_core.isRomLoaded()) {
                    DrawTexturePro(fbTexture, sourceRec, destRec, Vector2{ 0.0f, 0.0f }, 0.0f, WHITE);
                    if (m_isPaused) {
                        DrawText("PAUSED", 10, 10, 20, YELLOW);
                    }
                } else {
                    const char* promptText = "No ROM Loaded. Drag and Drop a File to Start.";
                    int fontSize = 20;
                    int textWidth = MeasureText(promptText, fontSize);
                    DrawText(promptText, (GetScreenWidth() - textWidth) / 2, GetScreenHeight() / 2 - 10, fontSize, GRAY);
                }
            EndDrawing();
        }

        // Cleanup
        UnloadAudioStream(audioStream);
        CloseAudioDevice();
        UnloadTexture(fbTexture);
        CloseWindow();

        return 0;
    }

private:
    void handleFileDrop() {
        if (IsFileDropped()) {
            FilePathList droppedFiles = LoadDroppedFiles();
            if (droppedFiles.count > 0) {
                // Pass path directly to core
                loadRom(droppedFiles.paths[0]);
            }
            UnloadDroppedFiles(droppedFiles);
        }
    }

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
    int m_audioChannels = 2;
    bool m_isPaused = false;

    static inline LockFreeAudioBuffer* s_activeBuffer = nullptr;
    static inline int s_audioChannels = 2;
};