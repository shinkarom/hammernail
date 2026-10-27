// DemoCore_amd64_console.h
#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "ICore.h"
#define USE_AMD64
extern "C" {
	#include "amd64.h"
}

// Minimal “house-ISA console” integration for tiny386’s amd64 core.
//
// Memory map (guest-visible physical addresses):
//   0x0010_0000  ROM entrypoint / code (start RIP)
//   0x0020_0000  Framebuffer RGBA8 320x240 (307200 bytes)
//   0x0030_0000  Input word (u32 bitmask)
//
// Input bitmask (u32 at INPUT_ADDR):
//   bit0 Up, bit1 Down, bit2 Left, bit3 Right, bit4 ActionA
//
// Notes:
// - This setup is MMIO-free: framebuffer + input live in normal RAM.
// - Port I/O callbacks are stubbed (MMIO-only / RAM-only design).
// - Audio is silence here; you can add a guest audio buffer later.

class DemoCore : public ICore {
public:
    DemoCore() {
        framebuffer.resize(VRAM_W * VRAM_H, 0xFF000000);
        audioBuffer.reserve((SAMPLE_RATE / TARGET_FPS) * AUDIO_CH);

        // Allocate physical memory for the guest.
        physMem.assign(PHYS_MEM_SIZE, 0);

        // Create CPU + callback table.
        cpu = cpuamd64_new(0, (char*)physMem.data(), (long)physMem.size(), &cb);
        if (!cpu || !cb) {
            // If you want, throw; for now just leave cpu==nullptr and render nothing.
            cpu = nullptr;
            cb = nullptr;
            return;
        }

        // Fill callback table.
        cb->pic = nullptr;
        cb->pic_read_irq = &pic_read_irq_stub;

        cb->io = nullptr; // port I/O not used
        cb->io_read8 = &io_read8_stub;
        cb->io_write8 = &io_write8_stub;
        cb->io_read16 = &io_read16_stub;
        cb->io_write16 = &io_write16_stub;
        cb->io_read32 = &io_read32_stub;
        cb->io_write32 = &io_write32_stub;
        cb->io_read_string = &io_read_string_stub;
        cb->io_write_string = &io_write_string_stub;

        bus.mem = physMem.data();
        bus.size = physMem.size();

        cb->iomem = &bus;
        cb->iomem_read8  = &iomem_read8;
        cb->iomem_write8 = &iomem_write8;
        cb->iomem_read16  = &iomem_read16;
        cb->iomem_write16 = &iomem_write16;
        cb->iomem_read32  = &iomem_read32;
        cb->iomem_write32 = &iomem_write32;
        cb->iomem_read64  = &iomem_read64;
        cb->iomem_write64 = &iomem_write64;
        cb->iomem_write_string = &iomem_write_string;

        cpuamd64_set_vendor(cpu, "FANTASY64");

        // Load a tiny built-in ROM so you see *something* immediately.
        // It fills the framebuffer with solid green once, then spins.
        loadBuiltInRom();

        // Reset CPU into long mode, starting execution at ROM_ADDR.
        cpuamd64_reset_pm(cpu, (uint32_t)ROM_ADDR);
    }

    ~DemoCore() override {
        if (cpu) cpuamd64_delete(cpu);
        cpu = nullptr;
        cb = nullptr;
    }

    CoreConfig getConfig() const override {
        CoreConfig config;
        config.title = "AMD64 House-ISA Console (Assembly-friendly)";
        config.targetFps = TARGET_FPS;
        config.vramWidth = VRAM_W;
        config.vramHeight = VRAM_H;
        config.targetAspectRatio = 4.0f / 3.0f;
        config.pixelFormat = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        config.sampleRate = SAMPLE_RATE;
        config.audioChannels = AUDIO_CH;
        return config;
    }

    void stepFrame() override {
        if (!cpu) {
            // CPU init failed; show black + silence.
            std::fill(framebuffer.begin(), framebuffer.end(), 0xFF000000);
            audioBuffer.assign((SAMPLE_RATE / (int)TARGET_FPS) * AUDIO_CH, 0);
            return;
        }

        // Update guest input word (u32 bitmask in RAM).
        uint32_t in = 0;
        in |= (btnUp      ? (1u << 0) : 0);
        in |= (btnDown    ? (1u << 1) : 0);
        in |= (btnLeft    ? (1u << 2) : 0);
        in |= (btnRight   ? (1u << 3) : 0);
        in |= (btnActionA ? (1u << 4) : 0);
        bus.store_le<uint32_t>(INPUT_ADDR, in);

        // Run guest some amount for this host frame.
        // (stepcount semantics are core-defined; treat it as “instructions-ish”.)
        cpuamd64_step(cpu, INSNS_PER_FRAME);

        // Copy framebuffer from guest RAM into frontend framebuffer.
        static_assert(sizeof(uint32_t) == 4);
        std::memcpy(framebuffer.data(), physMem.data() + FB_ADDR, framebuffer.size() * 4);

        // Silence audio for now (easy to extend by reading a guest audio buffer).
        int samplesPerFrame = SAMPLE_RATE / (int)TARGET_FPS;
        audioBuffer.assign(samplesPerFrame * AUDIO_CH, 0);
    }

    const void* getFramebuffer() const override { return framebuffer.data(); }
    DisplayRegion getDisplayRegion() const override { return { 0, 0, VRAM_W, VRAM_H }; }

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

    void setAxis(int /*port*/, VirtualAxis /*axis*/, float /*value*/) override {}

private:
    // -------- Console constants --------
    static constexpr double TARGET_FPS = 60.0;
    static constexpr int VRAM_W = 320;
    static constexpr int VRAM_H = 240;
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int AUDIO_CH = 2;

    static constexpr size_t PHYS_MEM_SIZE = 64ull * 1024 * 1024;

    static constexpr uint32_t ROM_ADDR   = 0x0010'0000;
    static constexpr uint32_t FB_ADDR    = 0x0020'0000;
    static constexpr uint32_t INPUT_ADDR = 0x0030'0000;

    static constexpr int INSNS_PER_FRAME = 500000; // tune this

    // -------- Minimal bus (RAM-only) --------
    struct Bus {
        uint8_t* mem = nullptr;
        size_t size = 0;

        inline bool ok(uint64_t addr, size_t n) const {
            return addr + n <= size;
        }

        template<typename T>
        T load_le(uint64_t addr) const {
            if (!ok(addr, sizeof(T))) return 0;
            T v{};
            std::memcpy(&v, mem + addr, sizeof(T));

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            if constexpr (sizeof(T) == 2) v = (T)__builtin_bswap16((uint16_t)v);
            if constexpr (sizeof(T) == 4) v = (T)__builtin_bswap32((uint32_t)v);
            if constexpr (sizeof(T) == 8) v = (T)__builtin_bswap64((uint64_t)v);
#endif
            return v;
        }

        template<typename T>
        void store_le(uint64_t addr, T v) {
            if (!ok(addr, sizeof(T))) return;

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            if constexpr (sizeof(T) == 2) v = (T)__builtin_bswap16((uint16_t)v);
            if constexpr (sizeof(T) == 4) v = (T)__builtin_bswap32((uint32_t)v);
            if constexpr (sizeof(T) == 8) v = (T)__builtin_bswap64((uint64_t)v);
#endif
            std::memcpy(mem + addr, &v, sizeof(T));
        }
    };

    // -------- Callback stubs --------
    static int pic_read_irq_stub(void*) { return -1; }

    static u8  io_read8_stub (void*, int) { return 0xFF; }
    static void io_write8_stub (void*, int, u8) {}
    static u16 io_read16_stub(void*, int) { return 0xFFFF; }
    static void io_write16_stub(void*, int, u16) {}
    static u32 io_read32_stub(void*, int) { return 0xFFFF'FFFFu; }
    static void io_write32_stub(void*, int, u32) {}
    static int io_read_string_stub (void*, int, uint8_t*, int, int) { return 0; }
    static int io_write_string_stub(void*, int, uint8_t*, int, int) { return 0; }

    // -------- iomem callbacks -> Bus --------
    static u8  iomem_read8 (void* p, uword a) { return ((Bus*)p)->load_le<u8 >( (uint64_t)a ); }
    static u16 iomem_read16(void* p, uword a) { return ((Bus*)p)->load_le<u16>( (uint64_t)a ); }
    static u32 iomem_read32(void* p, uword a) { return ((Bus*)p)->load_le<u32>( (uint64_t)a ); }
    static u64 iomem_read64(void* p, uword a) { return ((Bus*)p)->load_le<u64>( (uint64_t)a ); }

    static void iomem_write8 (void* p, uword a, u8  v) { ((Bus*)p)->store_le<u8 >( (uint64_t)a, v ); }
    static void iomem_write16(void* p, uword a, u16 v) { ((Bus*)p)->store_le<u16>( (uint64_t)a, v ); }
    static void iomem_write32(void* p, uword a, u32 v) { ((Bus*)p)->store_le<u32>( (uint64_t)a, v ); }
    static void iomem_write64(void* p, uword a, u64 v) { ((Bus*)p)->store_le<u64>( (uint64_t)a, v ); }

    static bool iomem_write_string(void* p, uword addr, uint8_t* src, int len) {
        auto* b = (Bus*)p;
        if (len < 0) return false;
        if (!b->ok((uint64_t)addr, (size_t)len)) return false;
        std::memcpy(b->mem + (uint64_t)addr, src, (size_t)len);
        return true;
    }

    void loadBuiltInRom() {
        // x86-64 machine code (long mode), fills framebuffer with green and loops:
        //
        //   cld
        //   mov edi, FB_ADDR          ; dest (zero-extended to RDI)
        //   mov ecx, 320*240          ; dword count
        //   mov eax, 0xFF00FF00       ; AABBGGRR = solid green in RGBA8 memory
        //   rep stosd
        //   jmp $
        //
        // Encodings:
        //   FC
        //   BF imm32
        //   B9 imm32
        //   B8 imm32
        //   F3 AB
        //   EB FE
        const uint8_t rom[] = {
            0xFC,
            0xBF, (uint8_t)(FB_ADDR & 0xFF), (uint8_t)((FB_ADDR >> 8) & 0xFF),
                  (uint8_t)((FB_ADDR >> 16) & 0xFF), (uint8_t)((FB_ADDR >> 24) & 0xFF),
            0xB9, 0x00, 0x2C, 0x01, 0x00,                 // 0x12C00 = 76800
            0xB8, 0x00, 0xFF, 0x33, 0xFF,                 // 0xFF00FF00
            0xF3, 0xAB,
            0xEB, 0xFE
        };
        static_assert(sizeof(rom) == 20);

        if (ROM_ADDR + sizeof(rom) <= physMem.size())
            std::memcpy(physMem.data() + ROM_ADDR, rom, sizeof(rom));
    }

private:
    std::vector<uint32_t> framebuffer;
    std::vector<int16_t> audioBuffer;

    bool btnUp = false;
    bool btnDown = false;
    bool btnLeft = false;
    bool btnRight = false;
    bool btnActionA = false;

    // Guest machine
    std::vector<uint8_t> physMem;
    Bus bus;

    CPUAMD64* cpu = nullptr;
    CPU_CB* cb = nullptr;
};