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
    #define MINIRV64_LOAD1(ofs)         (*(uint8_t*)(image + (ofs)))
    #define MINIRV64_LOAD2(ofs)         (*(uint16_t*)(image + (ofs)))
    #define MINIRV64_LOAD4(ofs)         (*(uint32_t*)(image + (ofs)))
    #define MINIRV64_LOAD8(ofs)         (*(uint64_t*)(image + (ofs)))
    #define MINIRV64_LOAD1_SIGNED(ofs)  (*(int8_t*)(image + (ofs)))
    #define MINIRV64_LOAD2_SIGNED(ofs)  (*(int16_t*)(image + (ofs)))
    #define MINIRV64_LOAD4_SIGNED(ofs)  (*(int32_t*)(image + (ofs)))

    #define MINIRV64_STORE1(ofs, val)   (*(uint8_t*)(image + (ofs))  = (uint8_t)(val))
    #define MINIRV64_STORE2(ofs, val)   (*(uint16_t*)(image + (ofs)) = (uint16_t)(val))
    #define MINIRV64_STORE4(ofs, val)   (*(uint32_t*)(image + (ofs)) = (uint32_t)(val))
    #define MINIRV64_STORE8(ofs, val)   (*(uint64_t*)(image + (ofs)) = (uint64_t)(val))
#endif

MINIRV64_DECORATE int32_t MiniRV64Step(struct MiniRV64State *state, uint8_t *image, int count);

#ifdef MINIRV64_IMPLEMENTATION

#define MINIRV64_MSTATUS_MIE  (1ULL << 3)
#define MINIRV64_MSTATUS_MPIE (1ULL << 7)
#define MINIRV64_MSTATUS_MPP  (3ULL << 11)
#define MINIRV64_MIP_MASK     ((1ULL << MINIRV64_IRQ_MSIP) | (1ULL << MINIRV64_IRQ_MTIP) | (1ULL << MINIRV64_IRQ_MEIP))

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

    // Standard machine interrupt priority for this minimal core: external, software, timer.
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

MINIRV64_DECORATE int32_t MiniRV64Step(struct MiniRV64State *state, uint8_t *image, int count) {
    uint64_t pc = state->pc;
    uint64_t cycle = state->cycle;

    for (int icount = 0; icount < count; icount++) {
        // Interrupts are sampled at instruction boundaries, before the next fetch.
        if (minirv64_take_interrupt(state, &pc)) {
            state->pc = pc;
            // Interrupt entry consumes this step; the handler starts on the next step.
            cycle++;
            state->cycle = cycle;
            state->regs[0] = 0;
            continue;
        }
        cycle++;
        uint64_t ofs_pc = pc - MINIRV64_RAM_IMAGE_OFFSET;

        if (ofs_pc >= MINI_RV64_RAM_SIZE || (ofs_pc & 3) ||
            MINI_RV64_RAM_SIZE - ofs_pc < 4) {
            state->pc = pc; state->cycle = cycle;
            return RV64_TRAP;
        }
        uint32_t ir = MINIRV64_LOAD4(ofs_pc);
        const uint32_t ilen = 4;

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
                int64_t rel = (int64_t)(((int32_t)(ir & 0x80000000) >> 11) |
                                        (ir & 0xff000) |
                                        ((ir >> 9) & 0x800) |
                                        ((ir >> 20) & 0x7fe));
                rval = pc + ilen;
                pc = pc + rel - ilen;
                break;
            }

            case 0x67: // JALR
            {
                int64_t imm = (int64_t)(int32_t)ir >> 20;
                rval = pc + ilen;
                pc = ((state->regs[(ir >> 15) & 0x1f] + imm) & ~1ULL) - ilen;
                break;
            }

            case 0x63: // Branches
            {
                int64_t imm = ((ir & 0x80000000) ? 0xfffffffffffffe00ULL : 0) |
                              ((ir >> 19) & 0x1000) | ((ir >> 20) & 0x7e0) |
                              ((ir >> 7) & 0x1e) | ((ir << 4) & 0x800);
                uint64_t rs1 = state->regs[(ir >> 15) & 0x1f];
                uint64_t rs2 = state->regs[(ir >> 20) & 0x1f];
                uint64_t target = pc + imm - ilen;
                rdid = 0;
                switch ((ir >> 12) & 0x7) {
                    case 0: if (rs1 == rs2) pc = target; break;
                    case 1: if (rs1 != rs2) pc = target; break;
                    case 4: if ((int64_t)rs1 < (int64_t)rs2) pc = target; break;
                    case 5: if ((int64_t)rs1 >= (int64_t)rs2) pc = target; break;
                    case 6: if (rs1 < rs2) pc = target; break;
                    case 7: if (rs1 >= rs2) pc = target; break;
                    default: return RV64_TRAP;
                }
                break;
            }

            case 0x03: // Loads
            {
                uint64_t rs1 = state->regs[(ir >> 15) & 0x1f];
                int64_t imm = (int64_t)(int32_t)ir >> 20;
                uint64_t addy = rs1 + imm - MINIRV64_RAM_IMAGE_OFFSET;
                if (addy >= MINI_RV64_RAM_SIZE) return RV64_TRAP;

                switch ((ir >> 12) & 0x7) {
                    case 0: rval = (int64_t)MINIRV64_LOAD1_SIGNED(addy); break;
                    case 1: rval = (int64_t)MINIRV64_LOAD2_SIGNED(addy); break;
                    case 2: rval = (int64_t)MINIRV64_LOAD4_SIGNED(addy); break;
                    case 3: rval = MINIRV64_LOAD8(addy); break;
                    case 4: rval = MINIRV64_LOAD1(addy); break;
                    case 5: rval = MINIRV64_LOAD2(addy); break;
                    case 6: rval = (uint64_t)MINIRV64_LOAD4(addy); break;
                    default: return RV64_TRAP;
                }
                break;
            }

            case 0x23: // Stores
            {
                uint64_t rs1 = state->regs[(ir >> 15) & 0x1f];
                uint64_t rs2 = state->regs[(ir >> 20) & 0x1f];
                int64_t imm = (((int64_t)(int32_t)ir >> 20) & ~0x1fULL) | ((ir >> 7) & 0x1f);
                uint64_t addy = rs1 + imm - MINIRV64_RAM_IMAGE_OFFSET;
                rdid = 0;
                if (addy >= MINI_RV64_RAM_SIZE) return RV64_TRAP;

                switch ((ir >> 12) & 0x7) {
                    case 0: MINIRV64_STORE1(addy, rs2); break;
                    case 1: MINIRV64_STORE2(addy, rs2); break;
                    case 2: MINIRV64_STORE4(addy, rs2); break;
                    case 3: MINIRV64_STORE8(addy, rs2); break;
                    default: return RV64_TRAP;
                }
                break;
            }

            case 0x13: // OP-IMM
            {
                int64_t imm = (int64_t)(int32_t)ir >> 20;
                uint64_t rs1 = state->regs[(ir >> 15) & 0x1f];
                uint32_t funct3 = (ir >> 12) & 0x7;
                uint32_t funct7 = (ir >> 25) & 0x7f;
                uint32_t shamt = (ir >> 20) & 0x3f;

                switch (funct3) {
                    case 0: rval = rs1 + imm; break;                                       // ADDI
                    case 1:
                        if (funct7 == 0x00) rval = rs1 << shamt;                           // SLLI
#ifdef MINIRV64_ENABLE_BITMAN
                        else if (funct7 == 0x14) rval = rs1 | (1ULL << shamt);             // BSETI (Zbs)
                        else if (funct7 == 0x24) rval = rs1 & ~(1ULL << shamt);            // BCLRI (Zbs)
                        else if (funct7 == 0x34) rval = rs1 ^ (1ULL << shamt);             // BINVI (Zbs)
                        else if ((ir >> 20) == 0x600) rval = rv64_clz(rs1);                // CLZ (Zbb)
                        else if ((ir >> 20) == 0x601) rval = rv64_ctz(rs1);                // CTZ (Zbb)
                        else if ((ir >> 20) == 0x602) rval = rv64_cpop(rs1);               // CPOP (Zbb)
                        else if ((ir >> 20) == 0x604) rval = (int64_t)(int8_t)rs1;         // SEXT.B (Zbb)
                        else if ((ir >> 20) == 0x605) rval = (int64_t)(int16_t)rs1;        // SEXT.H (Zbb)
#endif
                        else return RV64_TRAP;
                        break;
                    case 2: rval = ((int64_t)rs1 < imm) ? 1 : 0; break;                    // SLTI
                    case 3: rval = (rs1 < (uint64_t)imm) ? 1 : 0; break;                   // SLTIU
                    case 4: rval = rs1 ^ imm; break;                                       // XORI
                    case 5:
                        if (funct7 == 0x00) rval = rs1 >> shamt;                           // SRLI
                        else if (funct7 == 0x20) rval = (int64_t)rs1 >> shamt;             // SRAI
#ifdef MINIRV64_ENABLE_BITMAN
                        else if (funct7 == 0x24) rval = (rs1 >> shamt) & 1ULL;             // BEXTI (Zbs)
                        else if (funct7 == 0x30) rval = (rs1 >> shamt) | (rs1 << ((64 - shamt) & 63)); // RORI (Zbb)
                        else if ((ir >> 20) == 0x287) rval = rv64_orcb(rs1);               // ORC.B (Zbb)
                        else if ((ir >> 20) == 0x687) rval = rv64_brev8(rs1);              // BREV8 (Zbkb)
                        else if ((ir >> 20) == 0x6B8) rval = rv64_rev8(rs1);               // REV8 (Zbb/Zbkb)
#endif
                        else return RV64_TRAP;
                        break;
                    case 6: rval = rs1 | imm; break;                                       // ORI
                    case 7: rval = rs1 & imm; break;                                       // ANDI
                }
                break;
            }

            case 0x1B: // OP-IMM-32
            {
                int64_t imm = (int64_t)(int32_t)ir >> 20;
                uint32_t rs1 = (uint32_t)state->regs[(ir >> 15) & 0x1f];
                uint32_t funct3 = (ir >> 12) & 0x7;
                uint32_t funct7 = (ir >> 25) & 0x7f;
                uint32_t shamt = (ir >> 20) & 0x1f;

                switch (funct3) {
                    case 0: rval = (int64_t)(int32_t)(rs1 + imm); break;                  // ADDIW
                    case 1:
                        if (funct7 == 0x00) rval = (int64_t)(int32_t)(rs1 << shamt);      // SLLIW
#ifdef MINIRV64_ENABLE_BITMAN
                        else if (funct7 == 0x04) rval = (uint64_t)rs1 << shamt;           // SLLI.UW (Zba)
                        else if ((ir >> 20) == 0x600) rval = rv64_clzw(rs1);              // CLZW (Zbb)
                        else if ((ir >> 20) == 0x601) rval = rv64_ctzw(rs1);              // CTZW (Zbb)
                        else if ((ir >> 20) == 0x602) rval = rv64_cpopw(rs1);             // CPOPW (Zbb)
#endif
                        else return RV64_TRAP;
                        break;
                    case 5:
                        if (funct7 == 0x00) rval = (int64_t)(int32_t)(rs1 >> shamt);      // SRLIW
                        else if (funct7 == 0x20) rval = (int64_t)(int32_t)((int32_t)rs1 >> shamt); // SRAIW
#ifdef MINIRV64_ENABLE_BITMAN
                        else if (funct7 == 0x30) rval = (int64_t)(int32_t)((rs1 >> shamt) | (rs1 << ((32 - shamt) & 31))); // RORIW (Zbb)
#endif
                        else return RV64_TRAP;
                        break;
                    default: return RV64_TRAP;
                }
                break;
            }

            case 0x33: // OP
            {
                uint64_t rs1 = state->regs[(ir >> 15) & 0x1f];
                uint64_t rs2 = state->regs[(ir >> 20) & 0x1f];
                uint32_t funct3 = (ir >> 12) & 0x7;
                uint32_t funct7 = (ir >> 25) & 0x7f;

                if (funct7 == 0x00) {
                    switch (funct3) {
                        case 0: rval = rs1 + rs2; break;                                   // ADD
                        case 1: rval = rs1 << (rs2 & 63); break;                          // SLL
                        case 2: rval = ((int64_t)rs1 < (int64_t)rs2) ? 1 : 0; break;       // SLT
                        case 3: rval = (rs1 < rs2) ? 1 : 0; break;                        // SLTU
                        case 4: rval = rs1 ^ rs2; break;                                   // XOR
                        case 5: rval = rs1 >> (rs2 & 63); break;                          // SRL
                        case 6: rval = rs1 | rs2; break;                                   // OR
                        case 7: rval = rs1 & rs2; break;                                   // AND
                    }
                } else if (funct7 == 0x20) {
                    switch (funct3) {
                        case 0: rval = rs1 - rs2; break;                                   // SUB
                        case 5: rval = (int64_t)rs1 >> (rs2 & 63); break;                 // SRA
#ifdef MINIRV64_ENABLE_BITMAN
                        case 4: rval = ~(rs1 ^ rs2); break;                                // XNOR (Zbb)
                        case 6: rval = rs1 | ~rs2; break;                                 // ORN (Zbb)
                        case 7: rval = rs1 & ~rs2; break;                                 // ANDN (Zbb)
#endif
                        default: return RV64_TRAP;
                    }
                } else if (funct7 == 0x01) { // RV64M
                    switch (funct3) {
                        case 0: rval = rs1 * rs2; break;                                   // MUL
                        case 1: rval = (uint64_t)(((unsigned __int128)(int64_t)rs1 * (int64_t)rs2) >> 64); break; // MULH
                        case 2: rval = (uint64_t)(((unsigned __int128)(int64_t)rs1 * rs2) >> 64); break;          // MULHSU
                        case 3: rval = (uint64_t)(((unsigned __int128)rs1 * rs2) >> 64); break;                   // MULHU
                        case 4: if (rs2 == 0) rval = -1ULL; else rval = ((int64_t)rs1 == INT64_MIN && (int64_t)rs2 == -1) ? (uint64_t)rs1 : (uint64_t)((int64_t)rs1 / (int64_t)rs2); break; // DIV
                        case 5: if (rs2 == 0) rval = -1ULL; else rval = rs1 / rs2; break; // DIVU
                        case 6: if (rs2 == 0) rval = rs1; else rval = ((int64_t)rs1 == INT64_MIN && (int64_t)rs2 == -1) ? 0 : ((int64_t)rs1 % (int64_t)rs2); break;     // REM
                        case 7: if (rs2 == 0) rval = rs1; else rval = rs1 % rs2; break;   // REMU
                    }
#ifdef MINIRV64_ENABLE_BITMAN
                } else if (funct7 == 0x04) { // Zbkb Pack
                    switch (funct3) {
                        case 4: rval = (rs1 & 0xFFFFFFFFULL) | (rs2 << 32); break;        // PACK (Zbkb/Zbb)
                        case 7: rval = (rs1 & 0xFFULL) | ((rs2 & 0xFFULL) << 8); break;   // PACKH (Zbkb/Zbb)
                        default: return RV64_TRAP;
                    }
                } else if (funct7 == 0x05) { // Zbb + Zbc
                    switch (funct3) {
                        case 1: rval = rv64_clmul(rs1, rs2); break;                        // CLMUL (Zbc)
                        case 2: rval = rv64_clmulr(rs1, rs2); break;                       // CLMULR (Zbc)
                        case 3: rval = rv64_clmulh(rs1, rs2); break;                       // CLMULH (Zbc)
                        case 4: rval = ((int64_t)rs1 < (int64_t)rs2) ? rs1 : rs2; break;  // MIN (Zbb)
                        case 5: rval = (rs1 < rs2) ? rs1 : rs2; break;                     // MINU (Zbb)
                        case 6: rval = ((int64_t)rs1 > (int64_t)rs2) ? rs1 : rs2; break;  // MAX (Zbb)
                        case 7: rval = (rs1 > rs2) ? rs1 : rs2; break;                     // MAXU (Zbb)
                    }
                } else if (funct7 == 0x10) { // Zba
                    switch (funct3) {
                        case 2: rval = (rs1 << 1) + rs2; break;                            // SH1ADD (Zba)
                        case 4: rval = (rs1 << 2) + rs2; break;                            // SH2ADD (Zba)
                        case 6: rval = (rs1 << 3) + rs2; break;                            // SH3ADD (Zba)
                        default: return RV64_TRAP;
                    }
                } else if (funct7 == 0x14 && funct3 == 1) {
                    rval = rs1 | (1ULL << (rs2 & 63));                                     // BSET (Zbs)
                } else if (funct7 == 0x24) {
                    if (funct3 == 1)      rval = rs1 & ~(1ULL << (rs2 & 63));              // BCLR (Zbs)
                    else if (funct3 == 5) rval = (rs1 >> (rs2 & 63)) & 1ULL;              // BEXT (Zbs)
                    else return RV64_TRAP;
                } else if (funct7 == 0x34 && funct3 == 1) {
                    rval = rs1 ^ (1ULL << (rs2 & 63));                                     // BINV (Zbs)
                } else if (funct7 == 0x30) {
                    if (funct3 == 1)      rval = (rs1 << (rs2 & 63)) | (rs1 >> ((64 - (rs2 & 63)) & 63)); // ROL (Zbb)
                    else if (funct3 == 5) rval = (rs1 >> (rs2 & 63)) | (rs1 << ((64 - (rs2 & 63)) & 63)); // ROR (Zbb)
                    else return RV64_TRAP;
#endif
#ifdef MINIRV64_ENABLE_ZICOND
                } else if (funct7 == 0x07) { // Zicond
                    switch (funct3) {
                        case 5: rval = (rs2 == 0) ? 0 : rs1; break;                        // CZERO.EQZ
                        case 7: rval = (rs2 != 0) ? 0 : rs1; break;                        // CZERO.NEZ
                        default: return RV64_TRAP;
                    }
#endif
                } else {
                    return RV64_TRAP;
                }
                break;
            }

            case 0x3B: // OP-32
            {
                uint32_t rs1 = (uint32_t)state->regs[(ir >> 15) & 0x1f];
                uint32_t rs2 = (uint32_t)state->regs[(ir >> 20) & 0x1f];
                uint64_t full_rs2 = state->regs[(ir >> 20) & 0x1f];
                uint32_t funct3 = (ir >> 12) & 0x7;
                uint32_t funct7 = (ir >> 25) & 0x7f;

                if (funct7 == 0x00) {
                    switch (funct3) {
                        case 0: rval = (int64_t)(int32_t)(rs1 + rs2); break;               // ADDW
                        case 1: rval = (int64_t)(int32_t)(rs1 << (rs2 & 31)); break;       // SLLW
                        case 5: rval = (int64_t)(int32_t)(rs1 >> (rs2 & 31)); break;       // SRLW
                        default: return RV64_TRAP;
                    }
                } else if (funct7 == 0x20) {
                    switch (funct3) {
                        case 0: rval = (int64_t)(int32_t)(rs1 - rs2); break;               // SUBW
                        case 5: rval = (int64_t)(int32_t)((int32_t)rs1 >> (rs2 & 31)); break; // SRAW
                        default: return RV64_TRAP;
                    }
                } else if (funct7 == 0x01) { // RV64M-32
                    switch (funct3) {
                        case 0: rval = (int64_t)(int32_t)(rs1 * rs2); break;               // MULW
                        case 4: if (rs2 == 0) rval = -1ULL; else rval = (int64_t)(int32_t)(((int32_t)rs1 == INT32_MIN && (int32_t)rs2 == -1) ? (int32_t)rs1 : ((int32_t)rs1 / (int32_t)rs2)); break; // DIVW
                        case 5: if (rs2 == 0) rval = -1ULL; else rval = (int64_t)(int32_t)(rs1 / rs2); break; // DIVUW
                        case 6: if (rs2 == 0) rval = (int64_t)(int32_t)rs1; else rval = (int64_t)(int32_t)(((int32_t)rs1 == INT32_MIN && (int32_t)rs2 == -1) ? 0 : ((int32_t)rs1 % (int32_t)rs2)); break; // REMW
                        case 7: if (rs2 == 0) rval = (int64_t)(int32_t)rs1; else rval = (int64_t)(int32_t)(rs1 % rs2); break; // REMUW
                    }
#ifdef MINIRV64_ENABLE_BITMAN
                } else if (funct7 == 0x04) {
                    if (funct3 == 0)      rval = (uint64_t)rs1 + full_rs2;                 // ADD.UW (Zba)
                    else if (funct3 == 4) rval = (int64_t)(int32_t)((rs1 & 0xFFFF) | ((rs2 & 0xFFFF) << 16)); // PACKW / ZEXT.H (Zbkb)
                    else return RV64_TRAP;
                } else if (funct7 == 0x10) { // Zba-32
                    switch (funct3) {
                        case 2: rval = ((uint64_t)rs1 << 1) + full_rs2; break;            // SH1ADD.UW (Zba)
                        case 4: rval = ((uint64_t)rs1 << 2) + full_rs2; break;            // SH2ADD.UW (Zba)
                        case 6: rval = ((uint64_t)rs1 << 3) + full_rs2; break;            // SH3ADD.UW (Zba)
                        default: return RV64_TRAP;
                    }
                } else if (funct7 == 0x30) { // Zbb-32
                    if (funct3 == 1)      rval = (int64_t)(int32_t)((rs1 << (rs2 & 31)) | (rs1 >> ((32 - (rs2 & 31)) & 31))); // ROLW (Zbb)
                    else if (funct3 == 5) rval = (int64_t)(int32_t)((rs1 >> (rs2 & 31)) | (rs1 << ((32 - (rs2 & 31)) & 31))); // RORW (Zbb)
                    else return RV64_TRAP;
#endif
                } else {
                    return RV64_TRAP;
                }
                break;
            }

            case 0x73: // SYSTEM: ECALL, EBREAK, MRET, and machine CSR instructions
            {
                uint32_t funct3 = (ir >> 12) & 7;
                uint32_t csr = ir >> 20;
                if (funct3 == 0) {
                    if (ir == 0x00000073) { // ECALL: preserve the existing host-syscall API
                        pc += ilen;
                        state->pc = pc; state->cycle = cycle;
                        #ifdef MINIRV64_HANDLE_SYSCALL
                            if (MINIRV64_HANDLE_SYSCALL(state, state->regs[17])) return RV64_SYSCALL;
                        #else
                            return RV64_SYSCALL;
                        #endif
                        state->regs[0] = 0;
                        continue;
                    }
                    if (ir == 0x00100073) { // EBREAK
                        state->pc = pc; state->cycle = cycle;
                        return RV64_TRAP;
                    }
                    if (ir == 0x30200073) { // MRET
                        uint64_t old_status = state->mstatus;
                        state->mstatus = (old_status & ~MINIRV64_MSTATUS_MIE) |
                            ((old_status & MINIRV64_MSTATUS_MPIE) ? MINIRV64_MSTATUS_MIE : 0) |
                            MINIRV64_MSTATUS_MPIE | MINIRV64_MSTATUS_MPP;
                        pc = state->mepc - ilen;
                        rdid = 0;
                        break;
                    }
                    state->pc = pc; state->cycle = cycle;
                    return RV64_TRAP;
                }

                uint64_t old_csr;
                switch (csr) {
                    case 0x300: old_csr = state->mstatus; break;
                    case 0x304: old_csr = state->mie; break;
                    case 0x305: old_csr = state->mtvec; break;
                    case 0x341: old_csr = state->mepc; break;
                    case 0x342: old_csr = state->mcause; break;
                    case 0x343: old_csr = state->mtval; break;
                    case 0x344: old_csr = state->mip; break;
                    default:
                        state->pc = pc; state->cycle = cycle;
                        return RV64_TRAP;
                }
                uint32_t rs1 = (ir >> 15) & 0x1f;
                uint64_t source = (funct3 & 4) ? rs1 : state->regs[rs1];
                uint64_t new_csr = old_csr;
                int write_csr = 1;
                switch (funct3) {
                    case 1: case 5: new_csr = source; break; // CSRRW[I]
                    case 2: case 6: if (source) new_csr = old_csr | source; else write_csr = 0; break; // CSRRS[I]
                    case 3: case 7: if (source) new_csr = old_csr & ~source; else write_csr = 0; break; // CSRRC[I]
                    default:
                        state->pc = pc; state->cycle = cycle;
                        return RV64_TRAP;
                }
                if (write_csr) {
                    switch (csr) {
                        case 0x300:
                            state->mstatus = (new_csr & (MINIRV64_MSTATUS_MIE | MINIRV64_MSTATUS_MPIE)) | MINIRV64_MSTATUS_MPP;
                            break;
                        case 0x304: state->mie = new_csr & MINIRV64_MIP_MASK; break;
                        case 0x305: state->mtvec = (new_csr & ~3ULL) | ((new_csr & 3ULL) == 1 ? 1ULL : 0ULL); break;
                        case 0x341: state->mepc = new_csr & ~3ULL; break;
                        case 0x342: state->mcause = new_csr; break;
                        case 0x343: state->mtval = new_csr; break;
                        case 0x344:
                            // Device-level MTIP/MEIP are controlled by the emulator, not guest CSR writes.
                            state->mip = (state->mip & ~(1ULL << MINIRV64_IRQ_MSIP)) |
                                          (new_csr & (1ULL << MINIRV64_IRQ_MSIP));
                            break;
                    }
                }
                rval = old_csr;
                break;
            }

            default: return RV64_TRAP;
        }

        if (rdid) state->regs[rdid] = rval;
        state->regs[0] = 0;
        pc += ilen;
    }

    state->pc = pc; state->cycle = cycle;
    return RV64_OK;
}

#endif // MINIRV64_IMPLEMENTATION

#ifdef __cplusplus
}
#endif

#endif // _MINI_RV64_H	