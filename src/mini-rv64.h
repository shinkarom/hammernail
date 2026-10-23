// ==============================================================================
// mini-rv64.h
// Complete 64-bit RISC-V Virtual Machine Core
// Extensions: RV64I + RV64M + RV64A + RV64C + Zba/b/c/bkb/s + Zicond + Syscalls
// ==============================================================================

#ifndef _MINI_RV64_H
#define _MINI_RV64_H

#include <stdint.h>
#include <string.h>

#ifndef MINIRV64_DECORATE
    #define MINIRV64_DECORATE static
#endif

#ifndef MINIRV64_RAM_IMAGE_OFFSET
    #define MINIRV64_RAM_IMAGE_OFFSET 0x80000000ULL
#endif

#ifndef MINI_RV64_RAM_SIZE
    #define MINI_RV64_RAM_SIZE (64 * 1024 * 1024)
#endif

// Default feature toggles (override before including header if needed)
#ifndef MINIRV64_ENABLE_A
    #define MINIRV64_ENABLE_A
#endif
#ifndef MINIRV64_ENABLE_C
    #define MINIRV64_ENABLE_C
#endif
#ifndef MINIRV64_ENABLE_BITMAN
    #define MINIRV64_ENABLE_BITMAN
#endif
#ifndef MINIRV64_ENABLE_ZICOND
    #define MINIRV64_ENABLE_ZICOND
#endif

struct MiniRV64State {
    uint64_t regs[32]; // x0-x31
    uint64_t pc;
    uint64_t cycle;
    uint64_t extraflags;
#ifdef MINIRV64_ENABLE_A
    uint64_t load_reservation; // Stores reserved memory address for LR/SC
#endif
};

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
        cycle++;
        uint64_t ofs_pc = pc - MINIRV64_RAM_IMAGE_OFFSET;

#ifndef MINIRV64_ENABLE_C
        if (ofs_pc >= MINI_RV64_RAM_SIZE || (ofs_pc & 3)) {
            state->pc = pc; state->cycle = cycle;
            return RV64_TRAP;
        }
        uint32_t ir = MINIRV64_LOAD4(ofs_pc);
        uint32_t ilen = 4;
#else
        if (ofs_pc >= MINI_RV64_RAM_SIZE || (ofs_pc & 1)) {
            state->pc = pc; state->cycle = cycle;
            return RV64_TRAP;
        }

        uint32_t ir = MINIRV64_LOAD2(ofs_pc);
        uint32_t ilen = 2;

        if ((ir & 3) == 3) { // 32-bit instruction
            if (ofs_pc + 2 >= MINI_RV64_RAM_SIZE) {
                state->pc = pc; state->cycle = cycle;
                return RV64_TRAP;
            }
            ir |= ((uint32_t)MINIRV64_LOAD2(ofs_pc + 2)) << 16;
            ilen = 4;
        } else { // 16-bit C-extension instruction decompression
            uint32_t quad = ir & 3;
            uint32_t funct3 = (ir >> 13) & 7;
            uint32_t r3_rd = 8 + ((ir >> 2) & 7);
            uint32_t r3_rs1 = 8 + ((ir >> 7) & 7);
            uint32_t r3_rs2 = 8 + ((ir >> 2) & 7);
            uint32_t raw_rd = (ir >> 7) & 0x1f;

            if (quad == 0) {
                switch (funct3) {
                    case 0: // C.ADDI4SPN -> addi rd', x2, imm
                    {
                        uint32_t imm = ((ir >> 6) & 1) << 2 | ((ir >> 5) & 1) << 3 | ((ir >> 11) & 3) << 4 | ((ir >> 7) & 0xf) << 6;
                        if (imm == 0) return RV64_TRAP;
                        ir = (imm << 20) | (2 << 15) | (0 << 12) | (r3_rd << 7) | 0x13;
                        break;
                    }
                    case 2: // C.LW -> lw rd', offset(rs1')
                    {
                        uint32_t imm = ((ir >> 6) & 1) << 2 | ((ir >> 10) & 7) << 3 | ((ir >> 5) & 1) << 6;
                        ir = (imm << 20) | (r3_rs1 << 15) | (2 << 12) | (r3_rd << 7) | 0x03;
                        break;
                    }
                    case 3: // C.LD -> ld rd', offset(rs1')
                    {
                        uint32_t imm = ((ir >> 10) & 7) << 3 | ((ir >> 5) & 3) << 6;
                        ir = (imm << 20) | (r3_rs1 << 15) | (3 << 12) | (r3_rd << 7) | 0x03;
                        break;
                    }
                    case 6: // C.SW -> sw rs2', offset(rs1')
                    {
                        uint32_t imm = ((ir >> 6) & 1) << 2 | ((ir >> 10) & 7) << 3 | ((ir >> 5) & 1) << 6;
                        ir = ((imm >> 5) << 25) | (r3_rs2 << 20) | (r3_rs1 << 15) | (2 << 12) | ((imm & 0x1f) << 7) | 0x23;
                        break;
                    }
                    case 7: // C.SD -> sd rs2', offset(rs1')
                    {
                        uint32_t imm = ((ir >> 10) & 7) << 3 | ((ir >> 5) & 3) << 6;
                        ir = ((imm >> 5) << 25) | (r3_rs2 << 20) | (r3_rs1 << 15) | (3 << 12) | ((imm & 0x1f) << 7) | 0x23;
                        break;
                    }
                    default: return RV64_TRAP;
                }
            } else if (quad == 1) {
                switch (funct3) {
                    case 0: // C.NOP / C.ADDI
                    {
                        int32_t imm = ((ir >> 2) & 0x1f) | (((ir >> 12) & 1) ? 0xfffffe00 : 0);
                        ir = ((uint32_t)imm << 20) | (raw_rd << 15) | (0 << 12) | (raw_rd << 7) | 0x13;
                        break;
                    }
                    case 1: // C.ADDIW -> addiw rd, rd, imm
                    {
                        if (raw_rd == 0) return RV64_TRAP;
                        int32_t imm = ((ir >> 2) & 0x1f) | (((ir >> 12) & 1) ? 0xfffffe00 : 0);
                        ir = ((uint32_t)imm << 20) | (raw_rd << 15) | (0 << 12) | (raw_rd << 7) | 0x1B;
                        break;
                    }
                    case 2: // C.LI -> addi rd, x0, imm
                    {
                        int32_t imm = ((ir >> 2) & 0x1f) | (((ir >> 12) & 1) ? 0xfffffe00 : 0);
                        ir = ((uint32_t)imm << 20) | (0 << 15) | (0 << 12) | (raw_rd << 7) | 0x13;
                        break;
                    }
                    case 3: // C.ADDI16SP / C.LUI
                    {
                        if (raw_rd == 2) { // C.ADDI16SP
                            int32_t imm = ((ir >> 6) & 1) << 4 | ((ir >> 2) & 1) << 5 | ((ir >> 5) & 1) << 6 | ((ir >> 3) & 3) << 7 | (((ir >> 12) & 1) ? 0xfffffe00 : 0);
                            if (imm == 0) return RV64_TRAP;
                            ir = ((uint32_t)imm << 20) | (2 << 15) | (0 << 12) | (2 << 7) | 0x13;
                        } else { // C.LUI
                            int32_t imm = ((ir >> 2) & 0x1f) | (((ir >> 12) & 1) ? 0xfffe0 : 0);
                            if (raw_rd == 0 || imm == 0) return RV64_TRAP;
                            ir = ((uint32_t)imm << 12) | (raw_rd << 7) | 0x37;
                        }
                        break;
                    }
                    case 4: // C.SRLI / C.SRAI / C.ANDI / ALU ops
                    {
                        uint32_t subf = (ir >> 10) & 3;
                        int32_t imm = ((ir >> 2) & 0x1f) | (((ir >> 12) & 1) ? 0xfffffe00 : 0);
                        uint32_t shamt = ((ir >> 2) & 0x1f) | (((ir >> 12) & 1) << 5);
                        if (subf == 0)      ir = (shamt << 20) | (r3_rs1 << 15) | (5 << 12) | (r3_rs1 << 7) | 0x13; // SRLI
                        else if (subf == 1) ir = (0x20 << 25) | (shamt << 20) | (r3_rs1 << 15) | (5 << 12) | (r3_rs1 << 7) | 0x13; // SRAI
                        else if (subf == 2) ir = ((uint32_t)imm << 20) | (r3_rs1 << 15) | (7 << 12) | (r3_rs1 << 7) | 0x13; // ANDI
                        else {
                            uint32_t subf2 = ((ir >> 12) & 1) << 2 | ((ir >> 5) & 3);
                            switch (subf2) {
                                case 0: ir = (0x20 << 25) | (r3_rs2 << 20) | (r3_rs1 << 15) | (0 << 12) | (r3_rs1 << 7) | 0x33; break; // SUB
                                case 1: ir = (r3_rs2 << 20) | (r3_rs1 << 15) | (4 << 12) | (r3_rs1 << 7) | 0x33; break; // XOR
                                case 2: ir = (r3_rs2 << 20) | (r3_rs1 << 15) | (6 << 12) | (r3_rs1 << 7) | 0x33; break; // OR
                                case 3: ir = (r3_rs2 << 20) | (r3_rs1 << 15) | (7 << 12) | (r3_rs1 << 7) | 0x33; break; // AND
                                case 4: ir = (0x20 << 25) | (r3_rs2 << 20) | (r3_rs1 << 15) | (0 << 12) | (r3_rs1 << 7) | 0x3B; break; // SUBW
                                case 5: ir = (r3_rs2 << 20) | (r3_rs1 << 15) | (0 << 12) | (r3_rs1 << 7) | 0x3B; break; // ADDW
                                default: return RV64_TRAP;
                            }
                        }
                        break;
                    }
                    case 5: // C.J -> jal x0, offset
                    {
                        int32_t offset = ((ir >> 3) & 7) << 1 | ((ir >> 11) & 1) << 4 | ((ir >> 2) & 1) << 5 | ((ir >> 7) & 1) << 6 | ((ir >> 6) & 1) << 7 | ((ir >> 9) & 3) << 8 | ((ir >> 8) & 1) << 10 | (((ir >> 12) & 1) ? 0xfffff800 : 0);
                        uint32_t imm = (uint32_t)offset;
                        ir = (((imm >> 20) & 1) << 31) | (((imm >> 1) & 0x3ff) << 21) | (((imm >> 11) & 1) << 20) | (((imm >> 12) & 0xff) << 12) | (0 << 7) | 0x6F;
                        break;
                    }
                    case 6: // C.BEQZ -> beq rs1', x0, offset
                    {
                        int32_t offset = ((ir >> 3) & 3) << 1 | ((ir >> 10) & 3) << 3 | ((ir >> 2) & 1) << 5 | ((ir >> 5) & 3) << 6 | (((ir >> 12) & 1) ? 0xffffff00 : 0);
                        uint32_t imm = (uint32_t)offset;
                        ir = (((imm >> 12) & 1) << 31) | (((imm >> 5) & 0x3f) << 25) | (0 << 20) | (r3_rs1 << 15) | (0 << 12) | (((imm >> 1) & 0xf) << 8) | (((imm >> 11) & 1) << 7) | 0x63;
                        break;
                    }
                    case 7: // C.BNEZ -> bne rs1', x0, offset
                    {
                        int32_t offset = ((ir >> 3) & 3) << 1 | ((ir >> 10) & 3) << 3 | ((ir >> 2) & 1) << 5 | ((ir >> 5) & 3) << 6 | (((ir >> 12) & 1) ? 0xffffff00 : 0);
                        uint32_t imm = (uint32_t)offset;
                        ir = (((imm >> 12) & 1) << 31) | (((imm >> 5) & 0x3f) << 25) | (0 << 20) | (r3_rs1 << 15) | (1 << 12) | (((imm >> 1) & 0xf) << 8) | (((imm >> 11) & 1) << 7) | 0x63;
                        break;
                    }
                    default: return RV64_TRAP;
                }
            } else if (quad == 2) {
                switch (funct3) {
                    case 0: // C.SLLI -> slli rd, rd, shamt
                    {
                        uint32_t shamt = ((ir >> 2) & 0x1f) | (((ir >> 12) & 1) << 5);
                        ir = (shamt << 20) | (raw_rd << 15) | (1 << 12) | (raw_rd << 7) | 0x13;
                        break;
                    }
                    case 2: // C.LWSP -> lw rd, offset(x2)
                    {
                        uint32_t imm = ((ir >> 4) & 7) << 2 | ((ir >> 12) & 1) << 5 | ((ir >> 2) & 3) << 6;
                        ir = (imm << 20) | (2 << 15) | (2 << 12) | (raw_rd << 7) | 0x03;
                        break;
                    }
                    case 3: // C.LDSP -> ld rd, offset(x2)
                    {
                        uint32_t imm = ((ir >> 3) & 7) << 3 | ((ir >> 12) & 1) << 6 | ((ir >> 2) & 3) << 7;
                        ir = (imm << 20) | (2 << 15) | (3 << 12) | (raw_rd << 7) | 0x03;
                        break;
                    }
                    case 4: // C.JR / C.MV / C.EBREAK / C.JALR / C.ADD
                    {
                        uint32_t rs2 = (ir >> 2) & 0x1f;
                        uint32_t bit12 = (ir >> 12) & 1;
                        if (!bit12 && rs2 == 0)      ir = (raw_rd << 15) | (0 << 12) | (0 << 7) | 0x67; // C.JR -> jalr x0, 0(rs1)
                        else if (!bit12 && rs2 != 0) ir = (rs2 << 20) | (0 << 15) | (0 << 12) | (raw_rd << 7) | 0x33; // C.MV -> add rd, x0, rs2
                        else if (bit12 && raw_rd == 0 && rs2 == 0) ir = 0x00100073; // C.EBREAK -> ebreak
                        else if (bit12 && rs2 == 0)  ir = (raw_rd << 15) | (0 << 12) | (1 << 7) | 0x67; // C.JALR -> jalr x1, 0(rs1)
                        else if (bit12 && rs2 != 0)  ir = (rs2 << 20) | (raw_rd << 15) | (0 << 12) | (raw_rd << 7) | 0x33; // C.ADD -> add rd, rd, rs2
                        else return RV64_TRAP;
                        break;
                    }
                    case 6: // C.SWSP -> sw rs2, offset(x2)
                    {
                        uint32_t imm = ((ir >> 9) & 0xf) << 2 | ((ir >> 7) & 3) << 6;
                        uint32_t rs2 = (ir >> 2) & 0x1f;
                        ir = ((imm >> 5) << 25) | (rs2 << 20) | (2 << 15) | (2 << 12) | ((imm & 0x1f) << 7) | 0x23;
                        break;
                    }
                    case 7: // C.SDSP -> sd rs2, offset(x2)
                    {
                        uint32_t imm = ((ir >> 10) & 7) << 3 | ((ir >> 7) & 7) << 6;
                        uint32_t rs2 = (ir >> 2) & 0x1f;
                        ir = ((imm >> 5) << 25) | (rs2 << 20) | (2 << 15) | (3 << 12) | ((imm & 0x1f) << 7) | 0x23;
                        break;
                    }
                    default: return RV64_TRAP;
                }
            }
        }
#endif

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

#ifdef MINIRV64_ENABLE_A
            case 0x2F: // RV64A (Atomic Instructions)
            {
                uint64_t rs1 = state->regs[(ir >> 15) & 0x1f];
                uint64_t rs2 = state->regs[(ir >> 20) & 0x1f];
                uint32_t funct3 = (ir >> 12) & 7;
                uint32_t funct5 = (ir >> 27) & 0x1f;
                uint64_t addy = rs1 - MINIRV64_RAM_IMAGE_OFFSET;

                if (funct3 == 2) { // 32-bit Atomic Operations
                    if (addy >= MINI_RV64_RAM_SIZE || (addy & 3)) return RV64_TRAP;
                    int32_t mem_val = MINIRV64_LOAD4_SIGNED(addy);
                    int32_t src_val = (int32_t)rs2;
                    int32_t res_val = 0;

                    switch (funct5) {
                        case 0x02: // LR.W
                            state->load_reservation = rs1;
                            rval = (int64_t)mem_val;
                            break;
                        case 0x03: // SC.W
                            if (state->load_reservation == rs1) {
                                MINIRV64_STORE4(addy, src_val);
                                rval = 0; // Success
                            } else {
                                rval = 1; // Failure
                            }
                            state->load_reservation = 0;
                            break;
                        case 0x01: res_val = src_val; break; // AMOSWAP.W
                        case 0x00: res_val = mem_val + src_val; break; // AMOADD.W
                        case 0x04: res_val = mem_val ^ src_val; break; // AMOXOR.W
                        case 0x0C: res_val = mem_val & src_val; break; // AMOAND.W
                        case 0x08: res_val = mem_val | src_val; break; // AMOOR.W
                        case 0x10: res_val = (mem_val < src_val) ? mem_val : src_val; break; // AMOMIN.W
                        case 0x14: res_val = (mem_val > src_val) ? mem_val : src_val; break; // AMOMAX.W
                        case 0x18: res_val = ((uint32_t)mem_val < (uint32_t)src_val) ? mem_val : src_val; break; // AMOMINU.W
                        case 0x1C: res_val = ((uint32_t)mem_val > (uint32_t)src_val) ? mem_val : src_val; break; // AMOMAXU.W
                        default: return RV64_TRAP;
                    }
                    if (funct5 != 0x02 && funct5 != 0x03) {
                        MINIRV64_STORE4(addy, res_val);
                        rval = (int64_t)mem_val;
                    }
                } else if (funct3 == 3) { // 64-bit Atomic Operations
                    if (addy >= MINI_RV64_RAM_SIZE || (addy & 7)) return RV64_TRAP;
                    uint64_t mem_val = MINIRV64_LOAD8(addy);
                    uint64_t src_val = rs2;
                    uint64_t res_val = 0;

                    switch (funct5) {
                        case 0x02: // LR.D
                            state->load_reservation = rs1;
                            rval = mem_val;
                            break;
                        case 0x03: // SC.D
                            if (state->load_reservation == rs1) {
                                MINIRV64_STORE8(addy, src_val);
                                rval = 0; // Success
                            } else {
                                rval = 1; // Failure
                            }
                            state->load_reservation = 0;
                            break;
                        case 0x01: res_val = src_val; break; // AMOSWAP.D
                        case 0x00: res_val = mem_val + src_val; break; // AMOADD.D
                        case 0x04: res_val = mem_val ^ src_val; break; // AMOXOR.D
                        case 0x0C: res_val = mem_val & src_val; break; // AMOAND.D
                        case 0x08: res_val = mem_val | src_val; break; // AMOOR.D
                        case 0x10: res_val = ((int64_t)mem_val < (int64_t)src_val) ? mem_val : src_val; break; // AMOMIN.D
                        case 0x14: res_val = ((int64_t)mem_val > (int64_t)src_val) ? mem_val : src_val; break; // AMOMAX.D
                        case 0x18: res_val = (mem_val < src_val) ? mem_val : src_val; break; // AMOMINU.D
                        case 0x1C: res_val = (mem_val > src_val) ? mem_val : src_val; break; // AMOMAXU.D
                        default: return RV64_TRAP;
                    }
                    if (funct5 != 0x02 && funct5 != 0x03) {
                        MINIRV64_STORE8(addy, res_val);
                        rval = mem_val;
                    }
                } else {
                    return RV64_TRAP;
                }
                break;
            }
#endif

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
                        case 4: if (rs2 == 0) rval = -1ULL; else rval = ((int64_t)rs1 == INT64_MIN && (int64_t)rs2 == -1) ? rs1 : ((int64_t)rs1 / (int64_t)rs2); break; // DIV
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

            case 0x73: // ECALL / EBREAK
            {
                if (((ir >> 12) & 0x7) == 0 && (ir >> 20) == 0) { // ECALL
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
#endif // _MINI_RV64_H