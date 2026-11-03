// ==============================================================================
// mini-rv64.h
// Complete 64-bit RISC-V Virtual Machine Core
// Extensions: RV64I + RV64M + selected bit-manipulation extensions + Zicond + Syscalls
// ==============================================================================

#ifndef _MINI_RV64_H
#define _MINI_RV64_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MINIRV64_DECORATE
    #define MINIRV64_DECORATE
#endif

#ifndef MINIRV64_RAM_IMAGE_OFFSET
    #define MINIRV64_RAM_IMAGE_OFFSET 0x80000000ULL
#endif

#ifndef MINI_RV64_RAM_SIZE
    #define MINI_RV64_RAM_SIZE (64 * 1024 * 1024)
#endif

// Default feature toggles (override before including header if needed)
#ifndef MINIRV64_ENABLE_BITMAN
    #define MINIRV64_ENABLE_BITMAN
#endif
#ifndef MINIRV64_ENABLE_ZICOND
    #define MINIRV64_ENABLE_ZICOND
#endif

// Machine-mode interrupt/CSR state. This core currently models M-mode only.
struct MiniRV64State {
    uint64_t regs[32]; // x0-x31
    uint64_t pc;
    uint64_t cycle;
    uint64_t extraflags;
    uint64_t mstatus;
    uint64_t mie;
    uint64_t mtvec;
    uint64_t mscratch;
    uint64_t mepc;
    uint64_t mcause;
    uint64_t mtval;
    uint64_t mip; // Device-pending interrupt bits; set through MiniRV64SetInterrupt.
};

#define MINIRV64_IRQ_MSIP 3  // Machine software interrupt
#define MINIRV64_IRQ_MTIP 7  // Machine timer interrupt
#define MINIRV64_IRQ_MEIP 11 // Machine external interrupt

// Assert/deassert a machine interrupt line. The emulator owns device timing.
MINIRV64_DECORATE void MiniRV64SetInterrupt(struct MiniRV64State *state, uint32_t cause, int asserted);

#define RV64_OK       0
#define RV64_SYSCALL  1
#define RV64_TRAP     2

#ifndef MINIRV64_CUSTOM_MEMORY_BUS
    static inline uint8_t  minirv64_load1(const uint8_t *image, uint64_t ofs) { return image[ofs]; }
    static inline uint16_t minirv64_load2(const uint8_t *image, uint64_t ofs) { uint16_t v; memcpy(&v, image + ofs, 2); return v; }
    static inline uint32_t minirv64_load4(const uint8_t *image, uint64_t ofs) { uint32_t v; memcpy(&v, image + ofs, 4); return v; }
    static inline uint64_t minirv64_load8(const uint8_t *image, uint64_t ofs) { uint64_t v; memcpy(&v, image + ofs, 8); return v; }

    static inline void minirv64_store1(uint8_t *image, uint64_t ofs, uint8_t  v) { image[ofs] = v; }
    static inline void minirv64_store2(uint8_t *image, uint64_t ofs, uint16_t v) { memcpy(image + ofs, &v, 2); }
    static inline void minirv64_store4(uint8_t *image, uint64_t ofs, uint32_t v) { memcpy(image + ofs, &v, 4); }
    static inline void minirv64_store8(uint8_t *image, uint64_t ofs, uint64_t v) { memcpy(image + ofs, &v, 8); }

    #define MINIRV64_LOAD1(ofs)         minirv64_load1(image, (ofs))
    #define MINIRV64_LOAD2(ofs)         minirv64_load2(image, (ofs))
    #define MINIRV64_LOAD4(ofs)         minirv64_load4(image, (ofs))
    #define MINIRV64_LOAD8(ofs)         minirv64_load8(image, (ofs))
    #define MINIRV64_LOAD1_SIGNED(ofs)  ((int8_t)minirv64_load1(image, (ofs)))
    #define MINIRV64_LOAD2_SIGNED(ofs)  ((int16_t)minirv64_load2(image, (ofs)))
    #define MINIRV64_LOAD4_SIGNED(ofs)  ((int32_t)minirv64_load4(image, (ofs)))

    #define MINIRV64_STORE1(ofs, val)   minirv64_store1(image, (ofs), (uint8_t)(val))
    #define MINIRV64_STORE2(ofs, val)   minirv64_store2(image, (ofs), (uint16_t)(val))
    #define MINIRV64_STORE4(ofs, val)   minirv64_store4(image, (ofs), (uint32_t)(val))
    #define MINIRV64_STORE8(ofs, val)   minirv64_store8(image, (ofs), (uint64_t)(val))
#endif

MINIRV64_DECORATE int32_t MiniRV64Step(struct MiniRV64State *state, uint8_t *image, int count);

#ifdef MINIRV64_IMPLEMENTATION

#define MINIRV64_MSTATUS_MIE  (1ULL << 3)
#define MINIRV64_MSTATUS_MPIE (1ULL << 7)
#define MINIRV64_MSTATUS_MPP  (3ULL << 11)
#define MINIRV64_MIP_MASK     ((1ULL << MINIRV64_IRQ_MSIP) | (1ULL << MINIRV64_IRQ_MTIP) | (1ULL << MINIRV64_IRQ_MEIP))

#define MINIRV64_RETURN_TRAP() do { state->pc = pc; state->cycle = cycle; return RV64_TRAP; } while (0)

MINIRV64_DECORATE void MiniRV64SetInterrupt(struct MiniRV64State *state, uint32_t cause, int asserted) {
    if (!state || cause >= 64) return;
    uint64_t bit = 1ULL << cause;
    if (asserted) state->mip |= bit;
    else state->mip &= ~bit;
}

// Enter a machine-mode interrupt. Returns nonzero if one was taken.
static inline int minirv64_take_interrupt(struct MiniRV64State *state, uint64_t *pc) {
    uint64_t pending = state->mip & state->mie;
    if (!(state->mstatus & MINIRV64_MSTATUS_MIE) || !pending) return 0;

    // Standard machine interrupt priority: external (11), software (3), timer (7).
    uint32_t cause = (pending & (1ULL << MINIRV64_IRQ_MEIP)) ? MINIRV64_IRQ_MEIP :
                     (pending & (1ULL << MINIRV64_IRQ_MSIP)) ? MINIRV64_IRQ_MSIP :
                     (pending & (1ULL << MINIRV64_IRQ_MTIP)) ? MINIRV64_IRQ_MTIP : 64;
    if (cause == 64) return 0;

    state->mepc = *pc;
    state->mcause = (1ULL << 63) | cause;
    state->mtval = 0;
    uint64_t old_status = state->mstatus;
    state->mstatus = (old_status & ~MINIRV64_MSTATUS_MIE) |
                     ((old_status & MINIRV64_MSTATUS_MIE) ? MINIRV64_MSTATUS_MPIE : 0) |
                     MINIRV64_MSTATUS_MPP;
    uint64_t base = state->mtvec & ~3ULL;
    *pc = base + (((state->mtvec & 3ULL) == 1) ? (4ULL * cause) : 0ULL);
    return 1;
}

#ifdef MINIRV64_ENABLE_BITMAN
// --- BIT-MANIPULATION HELPERS ---
static inline uint64_t rv64_clz(uint64_t x)    { return x ? (uint64_t)__builtin_clzll(x) : 64; }
static inline uint64_t rv64_ctz(uint64_t x)    { return x ? (uint64_t)__builtin_ctzll(x) : 64; }
static inline uint64_t rv64_cpop(uint64_t x)   { return (uint64_t)__builtin_popcountll(x); }
static inline uint64_t rv64_clzw(uint32_t x)   { return x ? (uint64_t)__builtin_clz(x) : 32; }
static inline uint64_t rv64_ctzw(uint32_t x)   { return x ? (uint64_t)__builtin_ctz(x) : 32; }
static inline uint64_t rv64_cpopw(uint32_t x)  { return (uint64_t)__builtin_popcount(x); }
static inline uint64_t rv64_rev8(uint64_t x)   { return __builtin_bswap64(x); }

static inline uint64_t rv64_brev8(uint64_t x) {
    uint64_t r = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t b = (x >> (i * 8)) & 0xFF;
        b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
        b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
        b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
        r |= ((uint64_t)b) << (i * 8);
    }
    return r;
}

static inline uint64_t rv64_orcb(uint64_t x) {
    uint64_t res = 0;
    for (int i = 0; i < 8; i++) {
        if ((x >> (i * 8)) & 0xFF) res |= (0xFFULL << (i * 8));
    }
    return res;
}

static inline uint64_t rv64_clmul(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 64; i++) if ((b >> i) & 1) r ^= (a << i);
    return r;
}
static inline uint64_t rv64_clmulh(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 1; i < 64; i++) if ((b >> i) & 1) r ^= (a >> (64 - i));
    return r;
}
static inline uint64_t rv64_clmulr(uint64_t a, uint64_t b) {
    uint64_t r = 0;
    for (int i = 0; i < 64; i++) if ((b >> i) & 1) r ^= (a >> (63 - i));
    return r;
}
#endif

// Replace MiniRV64Step in mini-rv64.h with this optimized version:

#if defined(__GNUC__) || defined(__clang__)
    #define MINIRV64_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define MINIRV64_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #define MINIRV64_RESTRICT    __restrict__
#elif defined(_MSC_VER)
    #define MINIRV64_LIKELY(x)   (x)
    #define MINIRV64_UNLIKELY(x) (x)
    #define MINIRV64_RESTRICT    __restrict
#else
    #define MINIRV64_LIKELY(x)   (x)
    #define MINIRV64_UNLIKELY(x) (x)
    #define MINIRV64_RESTRICT
#endif

MINIRV64_DECORATE int32_t MiniRV64Step(
    struct MiniRV64State * MINIRV64_RESTRICT state,
    uint8_t * MINIRV64_RESTRICT image,
    int count
) {
    uint64_t pc = state->pc;
    uint64_t cycle = state->cycle;
    uint64_t * MINIRV64_RESTRICT regs = state->regs;
    regs[0] = 0;

    // Check pending interrupts once on entry instead of every instruction
    if (MINIRV64_UNLIKELY((state->mip & state->mie) && (state->mstatus & MINIRV64_MSTATUS_MIE))) {
        if (minirv64_take_interrupt(state, &pc)) {
            cycle++;
        }
    }

    for (int icount = 0; icount < count; icount++) {
        cycle++;
        uint64_t ofs_pc = pc - MINIRV64_RAM_IMAGE_OFFSET;

        // Single branch check for both bounds and 4-byte alignment
        if (MINIRV64_UNLIKELY((ofs_pc | (ofs_pc & 3) * MINI_RV64_RAM_SIZE) >= MINI_RV64_RAM_SIZE)) {
            MINIRV64_RETURN_TRAP();
        }

        uint32_t ir = MINIRV64_LOAD4(ofs_pc);
        uint32_t rdid = (ir >> 7) & 0x1f;
        uint64_t rval = 0;

        switch (ir & 0x7f) {
            case 0x37: // LUI
                rval = (int64_t)(int32_t)(ir & 0xfffff000);
                break;

            case 0x17: // AUIPC
                rval = pc + (int64_t)(int32_t)(ir & 0xfffff000);
                break;

            case 0x6F: // JAL
            {
                int64_t rel = (int64_t)(int32_t)(
                    ((int32_t)(ir & 0x80000000) >> 11) |
                    (ir & 0xff000) |
                    ((ir >> 9) & 0x800) |
                    ((ir >> 20) & 0x7fe)
                );
                rval = pc + 4;
                pc = pc + rel - 4;
                break;
            }

            case 0x67: // JALR
            {
                int64_t imm = (int64_t)(int32_t)ir >> 20;
                rval = pc + 4;
                pc = ((regs[(ir >> 15) & 0x1f] + imm) & ~1ULL) - 4;
                break;
            }

            case 0x63: // Branches
            {
                int64_t imm = (int64_t)(int32_t)(
                    ((int32_t)(ir & 0x80000000) >> 19) |
                    ((ir << 4) & 0x800) |
                    ((ir >> 20) & 0x7e0) |
                    ((ir >> 7) & 0x1e)
                );
                uint64_t rs1 = regs[(ir >> 15) & 0x1f];
                uint64_t rs2 = regs[(ir >> 20) & 0x1f];
                uint64_t target = pc + imm - 4;
                rdid = 0;
                switch ((ir >> 12) & 0x7) {
                    case 0: if (rs1 == rs2) pc = target; break;
                    case 1: if (rs1 != rs2) pc = target; break;
                    case 4: if ((int64_t)rs1 < (int64_t)rs2) pc = target; break;
                    case 5: if ((int64_t)rs1 >= (int64_t)rs2) pc = target; break;
                    case 6: if (rs1 < rs2) pc = target; break;
                    case 7: if (rs1 >= rs2) pc = target; break;
                    default: MINIRV64_RETURN_TRAP();
                }
                break;
            }

            case 0x03: // Loads
            {
                uint64_t rs1 = regs[(ir >> 15) & 0x1f];
                int64_t imm = (int64_t)(int32_t)ir >> 20;
                uint64_t addy = rs1 + imm - MINIRV64_RAM_IMAGE_OFFSET;
                if (MINIRV64_UNLIKELY(addy >= MINI_RV64_RAM_SIZE)) {
                    MINIRV64_RETURN_TRAP();
                }

                switch ((ir >> 12) & 0x7) {
                    case 0: rval = (int64_t)MINIRV64_LOAD1_SIGNED(addy); break;
                    case 1: rval = (int64_t)MINIRV64_LOAD2_SIGNED(addy); break;
                    case 2: rval = (int64_t)MINIRV64_LOAD4_SIGNED(addy); break;
                    case 3: rval = MINIRV64_LOAD8(addy); break;
                    case 4: rval = MINIRV64_LOAD1(addy); break;
                    case 5: rval = MINIRV64_LOAD2(addy); break;
                    case 6: rval = (uint64_t)MINIRV64_LOAD4(addy); break;
                    default: MINIRV64_RETURN_TRAP();
                }
                break;
            }

            case 0x23: // Stores
            {
                uint64_t rs1 = regs[(ir >> 15) & 0x1f];
                uint64_t rs2 = regs[(ir >> 20) & 0x1f];
                int64_t imm = (((int64_t)(int32_t)ir >> 20) & ~0x1fULL) | ((ir >> 7) & 0x1f);
                uint64_t addy = rs1 + imm - MINIRV64_RAM_IMAGE_OFFSET;
                rdid = 0;
                if (MINIRV64_UNLIKELY(addy >= MINI_RV64_RAM_SIZE)) {
                    MINIRV64_RETURN_TRAP();
                }

                switch ((ir >> 12) & 0x7) {
                    case 0: MINIRV64_STORE1(addy, rs2); break;
                    case 1: MINIRV64_STORE2(addy, rs2); break;
                    case 2: MINIRV64_STORE4(addy, rs2); break;
                    case 3: MINIRV64_STORE8(addy, rs2); break;
                    default: MINIRV64_RETURN_TRAP();
                }
                break;
            }

            case 0x0F: // MISC-MEM: FENCE
                rdid = 0;
                break;

            case 0x13: // OP-IMM
            {
                int64_t imm = (int64_t)(int32_t)ir >> 20;
                uint64_t rs1 = regs[(ir >> 15) & 0x1f];
                uint32_t funct3 = (ir >> 12) & 0x7;
                uint32_t funct6 = (ir >> 26) & 0x3f;
                uint32_t shamt = (ir >> 20) & 0x3f;

                switch (funct3) {
                    case 0: rval = rs1 + imm; break;
                    case 1:
                        if (MINIRV64_LIKELY(funct6 == 0x00)) rval = rs1 << shamt;
#ifdef MINIRV64_ENABLE_BITMAN
                        else if (funct6 == 0x0A) rval = rs1 | (1ULL << shamt);
                        else if (funct6 == 0x12) rval = rs1 & ~(1ULL << shamt);
                        else if (funct6 == 0x1A) rval = rs1 ^ (1ULL << shamt);
                        else if ((ir >> 20) == 0x600) rval = rv64_clz(rs1);
                        else if ((ir >> 20) == 0x601) rval = rv64_ctz(rs1);
                        else if ((ir >> 20) == 0x602) rval = rv64_cpop(rs1);
                        else if ((ir >> 20) == 0x604) rval = (int64_t)(int8_t)rs1;
                        else if ((ir >> 20) == 0x605) rval = (int64_t)(int16_t)rs1;
#endif
                        else MINIRV64_RETURN_TRAP();
                        break;
                    case 2: rval = ((int64_t)rs1 < imm) ? 1 : 0; break;
                    case 3: rval = (rs1 < (uint64_t)imm) ? 1 : 0; break;
                    case 4: rval = rs1 ^ imm; break;
                    case 5:
                        if (MINIRV64_LIKELY(funct6 == 0x00)) rval = rs1 >> shamt;
                        else if (funct6 == 0x10) rval = (int64_t)rs1 >> shamt;
#ifdef MINIRV64_ENABLE_BITMAN
                        else if (funct6 == 0x12) rval = (rs1 >> shamt) & 1ULL;
                        else if (funct6 == 0x18) rval = (rs1 >> shamt) | (rs1 << ((64 - shamt) & 63));
                        else if ((ir >> 20) == 0x287) rval = rv64_orcb(rs1);
                        else if ((ir >> 20) == 0x687) rval = rv64_brev8(rs1);
                        else if ((ir >> 20) == 0x6B8) rval = rv64_rev8(rs1);
#endif
                        else MINIRV64_RETURN_TRAP();
                        break;
                    case 6: rval = rs1 | imm; break;
                    case 7: rval = rs1 & imm; break;
                }
                break;
            }

            case 0x1B: // OP-IMM-32
            {
                int64_t imm = (int64_t)(int32_t)ir >> 20;
                uint32_t rs1 = (uint32_t)regs[(ir >> 15) & 0x1f];
                uint32_t funct3 = (ir >> 12) & 0x7;
                uint32_t funct7 = (ir >> 25) & 0x7f;
                uint32_t funct6 = (ir >> 26) & 0x3f;
                uint32_t shamt5 = (ir >> 20) & 0x1f;
                uint32_t shamt6 = (ir >> 20) & 0x3f;

                switch (funct3) {
                    case 0: rval = (int64_t)(int32_t)(rs1 + imm); break;
                    case 1:
                        if (MINIRV64_LIKELY(funct7 == 0x00)) rval = (int64_t)(int32_t)(rs1 << shamt5);
#ifdef MINIRV64_ENABLE_BITMAN
                        else if (funct6 == 0x02) rval = (uint64_t)rs1 << shamt6;
                        else if ((ir >> 20) == 0x600) rval = rv64_clzw(rs1);
                        else if ((ir >> 20) == 0x601) rval = rv64_ctzw(rs1);
                        else if ((ir >> 20) == 0x602) rval = rv64_cpopw(rs1);
#endif
                        else MINIRV64_RETURN_TRAP();
                        break;
                    case 5:
                        if (MINIRV64_LIKELY(funct7 == 0x00)) rval = (int64_t)(int32_t)(rs1 >> shamt5);
                        else if (funct7 == 0x20) rval = (int64_t)(int32_t)((int32_t)rs1 >> shamt5);
#ifdef MINIRV64_ENABLE_BITMAN
                        else if (funct7 == 0x30) rval = (int64_t)(int32_t)((rs1 >> shamt5) | (rs1 << ((32 - shamt5) & 31)));
#endif
                        else MINIRV64_RETURN_TRAP();
                        break;
                    default: MINIRV64_RETURN_TRAP();
                }
                break;
            }

            case 0x33: // OP
            {
                uint64_t rs1 = regs[(ir >> 15) & 0x1f];
                uint64_t rs2 = regs[(ir >> 20) & 0x1f];
                uint32_t funct3 = (ir >> 12) & 0x7;
                uint32_t funct7 = (ir >> 25) & 0x7f;

                if (MINIRV64_LIKELY(funct7 == 0x00)) {
                    switch (funct3) {
                        case 0: rval = rs1 + rs2; break;
                        case 1: rval = rs1 << (rs2 & 63); break;
                        case 2: rval = ((int64_t)rs1 < (int64_t)rs2) ? 1 : 0; break;
                        case 3: rval = (rs1 < rs2) ? 1 : 0; break;
                        case 4: rval = rs1 ^ rs2; break;
                        case 5: rval = rs1 >> (rs2 & 63); break;
                        case 6: rval = rs1 | rs2; break;
                        case 7: rval = rs1 & rs2; break;
                    }
                } else if (funct7 == 0x20) {
                    switch (funct3) {
                        case 0: rval = rs1 - rs2; break;
                        case 5: rval = (int64_t)rs1 >> (rs2 & 63); break;
#ifdef MINIRV64_ENABLE_BITMAN
                        case 4: rval = ~(rs1 ^ rs2); break;
                        case 6: rval = rs1 | ~rs2; break;
                        case 7: rval = rs1 & ~rs2; break;
#endif
                        default: MINIRV64_RETURN_TRAP();
                    }
                } else if (funct7 == 0x01) { // RV64M
                    switch (funct3) {
                        case 0: rval = rs1 * rs2; break;
                        case 1: rval = (uint64_t)(((unsigned __int128)(int64_t)rs1 * (int64_t)rs2) >> 64); break;
                        case 2: rval = (uint64_t)(((unsigned __int128)(int64_t)rs1 * rs2) >> 64); break;
                        case 3: rval = (uint64_t)(((unsigned __int128)rs1 * rs2) >> 64); break;
                        case 4: if (rs2 == 0) rval = -1ULL; else rval = ((int64_t)rs1 == INT64_MIN && (int64_t)rs2 == -1) ? (uint64_t)rs1 : (uint64_t)((int64_t)rs1 / (int64_t)rs2); break;
                        case 5: if (rs2 == 0) rval = -1ULL; else rval = rs1 / rs2; break;
                        case 6: if (rs2 == 0) rval = rs1; else rval = ((int64_t)rs1 == INT64_MIN && (int64_t)rs2 == -1) ? 0 : (uint64_t)((int64_t)rs1 % (int64_t)rs2); break;
                        case 7: if (rs2 == 0) rval = rs1; else rval = rs1 % rs2; break;
                    }
#ifdef MINIRV64_ENABLE_BITMAN
                } else if (funct7 == 0x10) { // Zba (Very frequent in Clang/Zig code!)
                    switch (funct3) {
                        case 2: rval = (rs1 << 1) + rs2; break;
                        case 4: rval = (rs1 << 2) + rs2; break;
                        case 6: rval = (rs1 << 3) + rs2; break;
                        default: MINIRV64_RETURN_TRAP();
                    }
                } else if (funct7 == 0x05) { // Zbb + Zbc
                    switch (funct3) {
                        case 1: rval = rv64_clmul(rs1, rs2); break;
                        case 2: rval = rv64_clmulr(rs1, rs2); break;
                        case 3: rval = rv64_clmulh(rs1, rs2); break;
                        case 4: rval = ((int64_t)rs1 < (int64_t)rs2) ? rs1 : rs2; break;
                        case 5: rval = (rs1 < rs2) ? rs1 : rs2; break;
                        case 6: rval = ((int64_t)rs1 > (int64_t)rs2) ? rs1 : rs2; break;
                        case 7: rval = (rs1 > rs2) ? rs1 : rs2; break;
                        default: MINIRV64_RETURN_TRAP();
                    }
                } else if (funct7 == 0x04) {
                    switch (funct3) {
                        case 4: rval = (rs1 & 0xFFFFFFFFULL) | (rs2 << 32); break;
                        case 7: rval = (rs1 & 0xFFULL) | ((rs2 & 0xFFULL) << 8); break;
                        default: MINIRV64_RETURN_TRAP();
                    }
                } else if (funct7 == 0x14 && funct3 == 1) {
                    rval = rs1 | (1ULL << (rs2 & 63));
                } else if (funct7 == 0x24) {
                    if (funct3 == 1)      rval = rs1 & ~(1ULL << (rs2 & 63));
                    else if (funct3 == 5) rval = (rs1 >> (rs2 & 63)) & 1ULL;
                    else MINIRV64_RETURN_TRAP();
                } else if (funct7 == 0x34 && funct3 == 1) {
                    rval = rs1 ^ (1ULL << (rs2 & 63));
                } else if (funct7 == 0x30) {
                    if (funct3 == 1)      rval = (rs1 << (rs2 & 63)) | (rs1 >> ((64 - (rs2 & 63)) & 63));
                    else if (funct3 == 5) rval = (rs1 >> (rs2 & 63)) | (rs1 << ((64 - (rs2 & 63)) & 63));
                    else MINIRV64_RETURN_TRAP();
#endif
#ifdef MINIRV64_ENABLE_ZICOND
                } else if (funct7 == 0x07) {
                    switch (funct3) {
                        case 5: rval = (rs2 == 0) ? 0 : rs1; break;
                        case 7: rval = (rs2 != 0) ? 0 : rs1; break;
                        default: MINIRV64_RETURN_TRAP();
                    }
#endif
                } else {
                    MINIRV64_RETURN_TRAP();
                }
                break;
            }

            case 0x3B: // OP-32
            {
                uint32_t rs1 = (uint32_t)regs[(ir >> 15) & 0x1f];
                uint32_t rs2 = (uint32_t)regs[(ir >> 20) & 0x1f];
                uint64_t full_rs2 = regs[(ir >> 20) & 0x1f];
                uint32_t funct3 = (ir >> 12) & 0x7;
                uint32_t funct7 = (ir >> 25) & 0x7f;

                if (MINIRV64_LIKELY(funct7 == 0x00)) {
                    switch (funct3) {
                        case 0: rval = (int64_t)(int32_t)(rs1 + rs2); break;
                        case 1: rval = (int64_t)(int32_t)(rs1 << (rs2 & 31)); break;
                        case 5: rval = (int64_t)(int32_t)(rs1 >> (rs2 & 31)); break;
                        default: MINIRV64_RETURN_TRAP();
                    }
                } else if (funct7 == 0x20) {
                    switch (funct3) {
                        case 0: rval = (int64_t)(int32_t)(rs1 - rs2); break;
                        case 5: rval = (int64_t)(int32_t)((int32_t)rs1 >> (rs2 & 31)); break;
                        default: MINIRV64_RETURN_TRAP();
                    }
                } else if (funct7 == 0x01) {
                    switch (funct3) {
                        case 0: rval = (int64_t)(int32_t)(rs1 * rs2); break;
                        case 4: if (rs2 == 0) rval = -1ULL; else rval = (int64_t)(int32_t)(((int32_t)rs1 == INT32_MIN && (int32_t)rs2 == -1) ? (int32_t)rs1 : ((int32_t)rs1 / (int32_t)rs2)); break;
                        case 5: if (rs2 == 0) rval = -1ULL; else rval = (int64_t)(int32_t)(rs1 / rs2); break;
                        case 6: if (rs2 == 0) rval = (int64_t)(int32_t)rs1; else rval = (int64_t)(int32_t)(((int32_t)rs1 == INT32_MIN && (int32_t)rs2 == -1) ? 0 : ((int32_t)rs1 % (int32_t)rs2)); break;
                        case 7: if (rs2 == 0) rval = (int64_t)(int32_t)rs1; else rval = (int64_t)(int32_t)(rs1 % rs2); break;
                        default: MINIRV64_RETURN_TRAP();
                    }
#ifdef MINIRV64_ENABLE_BITMAN
                } else if (funct7 == 0x04) {
                    if (funct3 == 0)      rval = (uint64_t)rs1 + full_rs2;
                    else if (funct3 == 4) rval = (int64_t)(int32_t)((rs1 & 0xFFFF) | ((rs2 & 0xFFFF) << 16));
                    else MINIRV64_RETURN_TRAP();
                } else if (funct7 == 0x10) {
                    switch (funct3) {
                        case 2: rval = ((uint64_t)rs1 << 1) + full_rs2; break;
                        case 4: rval = ((uint64_t)rs1 << 2) + full_rs2; break;
                        case 6: rval = ((uint64_t)rs1 << 3) + full_rs2; break;
                        default: MINIRV64_RETURN_TRAP();
                    }
                } else if (funct7 == 0x30) {
                    if (funct3 == 1)      rval = (int64_t)(int32_t)((rs1 << (rs2 & 31)) | (rs1 >> ((32 - (rs2 & 31)) & 31)));
                    else if (funct3 == 5) rval = (int64_t)(int32_t)((rs1 >> (rs2 & 31)) | (rs1 << ((32 - (rs2 & 31)) & 31)));
                    else MINIRV64_RETURN_TRAP();
#endif
                } else {
                    MINIRV64_RETURN_TRAP();
                }
                break;
            }

            case 0x73: // SYSTEM
            {
                uint32_t funct3 = (ir >> 12) & 7;
                uint32_t csr = ir >> 20;
                if (funct3 == 0) {
                    if (ir == 0x00000073) { // ECALL
                        pc += 4;
                        state->pc = pc; state->cycle = cycle;
                        #ifdef MINIRV64_HANDLE_SYSCALL
                            if (MINIRV64_HANDLE_SYSCALL(state, regs[17])) return RV64_SYSCALL;
                        #else
                            return RV64_SYSCALL;
                        #endif
                        regs[0] = 0;
                        continue;
                    }
                    if (ir == 0x00100073) { // EBREAK
                        MINIRV64_RETURN_TRAP();
                    }
                    if (ir == 0x30200073) { // MRET
                        uint64_t old_status = state->mstatus;
                        state->mstatus = (old_status & ~MINIRV64_MSTATUS_MIE) |
                            ((old_status & MINIRV64_MSTATUS_MPIE) ? MINIRV64_MSTATUS_MIE : 0) |
                            MINIRV64_MSTATUS_MPIE | MINIRV64_MSTATUS_MPP;
                        pc = state->mepc;
                        if (MINIRV64_UNLIKELY((state->mip & state->mie) && (state->mstatus & MINIRV64_MSTATUS_MIE))) {
                            minirv64_take_interrupt(state, &pc);
                        }
                        pc -= 4;
                        rdid = 0;
                        break;
                    }
                    if (ir == 0x10500073) { // WFI
                        rdid = 0;
                        break;
                    }
                    MINIRV64_RETURN_TRAP();
                }

                uint64_t old_csr = 0;
                int csr_readonly = 0;
                switch (csr) {
                    case 0x300: old_csr = state->mstatus; break;
                    case 0x301: old_csr = (2ULL << 62) | (1ULL << 8) | (1ULL << 12); csr_readonly = 1; break;
                    case 0x304: old_csr = state->mie; break;
                    case 0x305: old_csr = state->mtvec; break;
                    case 0x340: old_csr = state->mscratch; break;
                    case 0x341: old_csr = state->mepc; break;
                    case 0x342: old_csr = state->mcause; break;
                    case 0x343: old_csr = state->mtval; break;
                    case 0x344: old_csr = state->mip; break;
                    case 0xB00: case 0xC00:
                    case 0xB02: case 0xC02: old_csr = cycle; csr_readonly = (csr >= 0xC00); break;
                    case 0xF11: case 0xF12: case 0xF13: case 0xF14: old_csr = 0; csr_readonly = 1; break;
                    default: MINIRV64_RETURN_TRAP();
                }

                uint32_t rs1 = (ir >> 15) & 0x1f;
                uint64_t source = (funct3 & 4) ? rs1 : regs[rs1];
                uint64_t new_csr = old_csr;
                int write_csr = 1;
                switch (funct3) {
                    case 1: case 5: new_csr = source; break;
                    case 2: case 6: if (rs1) new_csr = old_csr | source; else write_csr = 0; break;
                    case 3: case 7: if (rs1) new_csr = old_csr & ~source; else write_csr = 0; break;
                    default: MINIRV64_RETURN_TRAP();
                }

                if (write_csr) {
                    if (csr_readonly) MINIRV64_RETURN_TRAP();
                    switch (csr) {
                        case 0x300:
                            state->mstatus = (new_csr & (MINIRV64_MSTATUS_MIE | MINIRV64_MSTATUS_MPIE)) | MINIRV64_MSTATUS_MPP;
                            break;
                        case 0x304: state->mie = new_csr & MINIRV64_MIP_MASK; break;
                        case 0x305: state->mtvec = (new_csr & ~3ULL) | ((new_csr & 3ULL) == 1 ? 1ULL : 0ULL); break;
                        case 0x340: state->mscratch = new_csr; break;
                        case 0x341: state->mepc = new_csr & ~3ULL; break;
                        case 0x342: state->mcause = new_csr; break;
                        case 0x343: state->mtval = new_csr; break;
                        case 0x344:
                            state->mip = (state->mip & ~(1ULL << MINIRV64_IRQ_MSIP)) |
                                          (new_csr & (1ULL << MINIRV64_IRQ_MSIP));
                            break;
                        case 0xB00: case 0xB02: cycle = new_csr; break;
                    }
                    // Only re-check interrupts if a CSR write actually modified CSR state
                    if (rdid) regs[rdid] = old_csr;
                    pc += 4;
                    if (MINIRV64_UNLIKELY((state->mip & state->mie) && (state->mstatus & MINIRV64_MSTATUS_MIE))) {
                        minirv64_take_interrupt(state, &pc);
                    }
                    continue;
                }
                rval = old_csr;
                break;
            }

            default: MINIRV64_RETURN_TRAP();
        }

        // Writeback only when rdid != 0; regs[0] is never modified so no need to zero it every instruction
        if (rdid) regs[rdid] = rval;
        pc += 4;
    }

    state->pc = pc; state->cycle = cycle;
    return RV64_OK;
}

#endif // MINIRV64_IMPLEMENTATION

#ifdef __cplusplus
}
#endif

#endif // _MINI_RV64_H