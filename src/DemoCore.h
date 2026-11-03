#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ICore.h"
#include "mini-rv64.h"

/*
 ==============================================================================
  RISC-V 64 Fantasy Console Memory Map
 ==============================================================================
  Guest Address Base: 0x80000000 (MINIRV64_RAM_IMAGE_OFFSET)
  Total RAM Size:     64 MB contiguous buffer

  Memory Layout (Guest Physical Addresses):
    0x8000_0000 - 0x801F_FFFF : Program ROM & RAM Code Space (2 MB)
    0x8020_0000 - 0x8024_B000 : Framebuffer RGBA8888 (320x240 @ 4bpp = 307,200 bytes)
    0x8030_0000 - 0x8030_0003 : Controller Input Word (32-bit bitmask)
    0x8030_0004 - 0x8030_0007 : Hardware Frame Counter (32-bit uint)
    0x8040_0000 - 0x8400_0000 : Heap & Stack Space (Stack grows down from 0x84000000)
 ==============================================================================
*/

class DemoCore : public ICore {
public:
    DemoCore() {
        m_ram.resize(RAM_SIZE + 8, 0);
        m_audioBuffer.reserve((SAMPLE_RATE / static_cast<int>(TARGET_FPS)) * AUDIO_CH);

        // Load built-in CPU benchmark ROM by default
        loadBuiltInRom();
    }

    ~DemoCore() override = default;

    // --- Configuration ---
    CoreConfig getConfig() const override {
        CoreConfig config;
        config.title = "RISC-V 64 Fantasy Console";
        config.targetFps = TARGET_FPS;
        config.vramWidth = VRAM_W;
        config.vramHeight = VRAM_H;
        config.targetAspectRatio = 4.0f / 3.0f;
        config.pixelFormat = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        config.sampleRate = SAMPLE_RATE;
        config.audioChannels = AUDIO_CH;
        return config;
    }

    // --- ROM Lifecycle Management ---
    bool loadRomFile(std::string_view filePath) override {
        std::ifstream file(std::string(filePath), std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "[DemoCore] Error: Failed to open ROM file '" << filePath << "'\n";
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size <= 0 || static_cast<size_t>(size) > MAX_ROM_SIZE) {
            std::cerr << "[DemoCore] Error: ROM size (" << size << " bytes) exceeds limit (" << MAX_ROM_SIZE << " bytes)\n";
            return false;
        }

        std::vector<uint8_t> romData(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(romData.data()), size)) {
            std::cerr << "[DemoCore] Error reading ROM contents\n";
            return false;
        }

        if (!loadRomData(romData)) {
            return false;
        }

        std::cout << "[DemoCore] Successfully loaded RISC-V binary (" << size << " bytes)\n";
        return true;
    }

    bool loadRomData(std::span<const uint8_t> data) override {
        if (data.empty() || data.size() > MAX_ROM_SIZE) return false;

        // Cache the clean ROM image so reset() can restore it
        m_romCache.assign(data.begin(), data.end());
        m_romLoaded = true;

        reset();
        return true;
    }

    void unloadRom() override {
        loadBuiltInRom();
    }

    bool isRomLoaded() const override { return m_romLoaded; }

    void reset() override {
        std::memset(&m_cpuState, 0, sizeof(m_cpuState));
        std::fill(m_ram.begin(), m_ram.end(), 0);

        // Restore cached ROM into guest RAM
        if (!m_romCache.empty()) {
            std::memcpy(m_ram.data() + ROM_OFFSET, m_romCache.data(), m_romCache.size());
        }

        m_cpuState.pc = MINIRV64_RAM_IMAGE_OFFSET + ROM_OFFSET;        // PC = 0x80000000
        m_cpuState.regs[2] = MINIRV64_RAM_IMAGE_OFFSET + STACK_OFFSET; // Stack Pointer sp = x2 (0x84000000)

        m_frameCounter = 0;
        m_trapLogged = false;
        m_audioBuffer.clear();
    }

    // --- Emulation Step ---
    void stepFrame() override {
        if (!m_romLoaded) return;

        // Update guest MMIO registers
        updateInputWord();
        incrementFrameCounter();

        // Execute frame instructions in slices so syscalls (like SYS_write)
        // don't prematurely end the entire 16.6ms frame.
        int remainingInsns = INSNS_PER_FRAME;
        while (remainingInsns > 0 && !m_trapLogged) {
            int slice = std::min(remainingInsns, INSN_SLICE_SIZE);
            int status = MiniRV64Step(&m_cpuState, m_ram.data(), slice);
            remainingInsns -= slice;

            if (status == RV64_TRAP) {
                std::cerr << "[DemoCore] Warning: CPU Trapped at PC: 0x"
                          << std::hex << m_cpuState.pc << std::dec << "\n";
                m_trapLogged = true;
                break;
            } else if (status == RV64_SYSCALL) {
                if (handleSyscall()) {
                    // Guest requested VBlank yield; finish frame early
                    break;
                }
            }
        }

        int samplesPerFrame = SAMPLE_RATE / static_cast<int>(TARGET_FPS);
        m_audioBuffer.assign(samplesPerFrame * AUDIO_CH, 0);
    }

    // --- Graphics ---
    const void* getFramebuffer() const override {
        return m_ram.data() + FB_OFFSET;
    }

    DisplayRegion getDisplayRegion() const override {
        return { 0, 0, VRAM_W, VRAM_H };
    }

    // --- Audio ---
    std::span<const int16_t> getAudioSamples() override { return m_audioBuffer; }
    void clearAudioSamples() override { m_audioBuffer.clear(); }

    // --- Input ---
    void setButton(int port, VirtualButton btn, bool pressed) override {
        if (port != 0) return;

        uint32_t mask = (1u << static_cast<uint32_t>(btn));
        if (pressed) {
            m_inputBitmask |= mask;
        } else {
            m_inputBitmask &= ~mask;
        }
    }

    void setAxis(int /*port*/, VirtualAxis /*axis*/, float /*value*/) override {}

private:
    void updateInputWord() {
        std::memcpy(m_ram.data() + INPUT_OFFSET, &m_inputBitmask, sizeof(uint32_t));
    }

    void incrementFrameCounter() {
        m_frameCounter++;
        std::memcpy(m_ram.data() + FRAME_COUNT_OFFSET, &m_frameCounter, sizeof(uint32_t));
    }

    // Returns true if the syscall should yield the rest of the frame (Wait for VBlank)
    bool handleSyscall() {
        uint64_t syscallId = m_cpuState.regs[17]; // a7
        if (syscallId == 1) {                     // Custom SYS_vblank_yield
            return true;
        } else if (syscallId == 64) {             // SYS_write (single char in a0)
            char c = static_cast<char>(m_cpuState.regs[10]); // a0
            std::cout << c << std::flush;
        }
        return false;
    }

    void loadBuiltInRom() {
        /*
           Verified RISC-V 64 Framebuffer Fill Loop
           =========================================
           0x80000000 : auipc x5, 0x00200    (x5 = 0x80200000 VRAM)
           0x80000004 : auipc x7, 0x00300    (x7 = 0x80300004 MMIO Frame Count)
           0x80000008 : lw    x10, 0(x7)     (Read frame counter)
           0x8000000c : slli  x10, x10, 8    (Color shift)
           0x80000010 : lui   x11, 0xff000   (Alpha mask = 0xFF000000)
           0x80000014 : or    x10, x10, x11  (Color |= Alpha)
           0x80000018 : mv    x6, x5         (x6 = VRAM ptr)
           0x8000001c : lui   x12, 0x00013   (x12 = 0x13000 = 77,824)
           0x80000020 : addi  x12, x12, -1024(x12 = 77,824 - 1024 = 76,800 pixels)
           0x80000024 : sw    x10, 0(x6)     (*VRAM = color)
           0x80000028 : addi  x6, x6, 4      (VRAM ptr += 4)
           0x8000002c : addi  x10, x10, 1    (color++)
           0x80000030 : addi  x12, x12, -1   (pixel_count--)
           0x80000034 : bne   x12, x0, -16   (if count != 0 goto 0x80000024)
           0x80000038 : jal   x0, -48        (goto 0x80000008 frame_loop)
        */
        static constexpr uint32_t builtInCode[] = {
            0x00200297, // 0x00: auipc x5, 0x00200
            0x00300397, // 0x04: auipc x7, 0x00300
            0x0003a503, // 0x08: lw    x10, 0(x7)
            0x00851513, // 0x0c: slli  x10, x10, 8
            0xff0005b7, // 0x10: lui   x11, 0xff000
            0x00b56533, // 0x14: or    x10, x10, x11
            0x00028313, // 0x18: mv    x6, x5
            0x00013637, // 0x1c: lui   x12, 0x00013
            0xc0060613, // 0x20: addi  x12, x12, -1024
            0x00a32023, // 0x24: sw    x10, 0(x6)
            0x00430313, // 0x28: addi  x6, x6, 4
            0x00150513, // 0x2c: addi  x10, x10, 1
            0xfff60613, // 0x30: addi  x12, x12, -1
            0xfe0618e3, // 0x34: bne   x12, x0, -16
            0xfd1ff06f  // 0x38: jal   x0, -48
        };

        std::span<const uint8_t> codeBytes(
            reinterpret_cast<const uint8_t*>(builtInCode),
            sizeof(builtInCode)
        );
        loadRomData(codeBytes);
    }

private:
    static constexpr double TARGET_FPS = 60.0;
    static constexpr int VRAM_W = 320;
    static constexpr int VRAM_H = 240;
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int AUDIO_CH = 2;

    // --- CPU BENCHMARK SPEED ---
    // Change this value to test host execution limits!
    // 10,000,000 instructions = ~600 MIPS
    static constexpr int INSNS_PER_FRAME = 500000;
    static constexpr int INSN_SLICE_SIZE = 50000;

    static constexpr size_t RAM_SIZE     = 64 * 1024 * 1024;
    static constexpr size_t MAX_ROM_SIZE = 2 * 1024 * 1024;

    static constexpr uint32_t ROM_OFFSET         = 0x00000000;
    static constexpr uint32_t FB_OFFSET          = 0x00200000;
    static constexpr uint32_t INPUT_OFFSET       = 0x00300000;
    static constexpr uint32_t FRAME_COUNT_OFFSET = 0x00300004;
    static constexpr uint32_t STACK_OFFSET       = 0x04000000; // Top of 64MB RAM

    MiniRV64State m_cpuState{};
    std::vector<uint8_t> m_ram;
    std::vector<uint8_t> m_romCache;
    std::vector<int16_t> m_audioBuffer;

    uint32_t m_inputBitmask = 0;
    uint32_t m_frameCounter = 0;
    bool m_romLoaded = false;
    bool m_trapLogged = false;
};