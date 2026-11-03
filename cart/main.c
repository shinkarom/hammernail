#include <stdint.h>

// --- Hardware Memory Map ---
#define VRAM         ((volatile uint32_t*)0x80200000ULL)
#define REG_INPUT    (*(volatile const uint32_t*)0x80300000ULL)
#define REG_FRAME    (*(volatile const uint32_t*)0x80300004ULL)

#define SCREEN_W     320
#define SCREEN_H     240

// --- VirtualButton Bitmasks (matches ICore.h enum) ---
#define BTN_UP       (1u << 0)
#define BTN_DOWN     (1u << 1)
#define BTN_LEFT     (1u << 2)
#define BTN_RIGHT    (1u << 3)
#define BTN_A        (1u << 4)
#define BTN_B        (1u << 5)

// Helper macro to pack R, G, B, A into a 32-bit little-endian pixel
#define RGBA(r, g, b, a) \
    ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

// --- Syscall Wrappers ---
static inline void sys_putc(char c) {
    register uint64_t a0 __asm__("a0") = (uint64_t)c;
    register uint64_t a7 __asm__("a7") = 64; // SYS_write
    __asm__ volatile ("ecall" : : "r"(a0), "r"(a7) : "memory");
}

static inline void sys_vblank_yield(void) {
    register uint64_t a7 __asm__("a7") = 1;  // SYS_vblank_yield
    __asm__ volatile ("ecall" : : "r"(a7) : "memory");
}

static void print_str(const char* s) {
    while (*s) sys_putc(*s++);
}

// --- Cartridge Entry Point ---
int main(void) {
    print_str("Hello from RISC-V 64 Cartridge!\n");

    int player_x = SCREEN_W / 2 - 10;
    int player_y = SCREEN_H / 2 - 10;

    while (1) {
        uint32_t input = REG_INPUT;
        uint32_t frame = REG_FRAME;

        // Move player square
        if (input & BTN_UP)    player_y -= 2;
        if (input & BTN_DOWN)  player_y += 2;
        if (input & BTN_LEFT)  player_x -= 2;
        if (input & BTN_RIGHT) player_x += 2;

        uint32_t box_color = (input & BTN_A) ? RGBA(255, 60, 60, 255) : RGBA(255, 255, 255, 255);

        // Draw background + player box
        for (int y = 0; y < SCREEN_H; ++y) {
            for (int x = 0; x < SCREEN_W; ++x) {
                if (x >= player_x && x < player_x + 20 &&
                    y >= player_y && y < player_y + 20) {
                    VRAM[y * SCREEN_W + x] = box_color;
                } else {
                    uint8_t v = (uint8_t)((x + frame) ^ (y + frame));
                    VRAM[y * SCREEN_W + x] = RGBA(v >> 2, v >> 1, v, 255);
                }
            }
        }

        // Wait for next 60Hz frame
        sys_vblank_yield();
    }

    return 0;
}