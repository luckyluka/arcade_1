/* ARCADE-1 — Milestone 09 test harness.
 *
 * Instruction-level tests for the RV32I interpreter. Links exec.c + cpu.c +
 * memory.c (no libretro, no machine, no .so), proving the CPU executes without
 * any frontend. Programs are hand-assembled with the same field encoders the
 * decoder tests use, serialised little-endian into ROM, then stepped one
 * instruction at a time with results checked against the register file / PC /
 * bus.
 *
 * Covers what the roadmap requires: arithmetic, logical, shifts, branches,
 * jumps, loads, stores, immediate instructions, and the x0 == 0 rule.
 */

#include "exec.h"

#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

/* --- Instruction assemblers (fields -> 32-bit word) ---------------------- */

static uint32_t enc_r(uint32_t opcode, uint32_t funct3, uint32_t funct7,
                      uint32_t rd, uint32_t rs1, uint32_t rs2)
{
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) |
           (funct3 << 12) | (rd << 7) | opcode;
}

static uint32_t enc_i(uint32_t opcode, uint32_t funct3,
                      uint32_t rd, uint32_t rs1, int32_t imm)
{
    return ((uint32_t)imm << 20) | (rs1 << 15) | (funct3 << 12) |
           (rd << 7) | opcode;
}

static uint32_t enc_s(uint32_t funct3, uint32_t rs1, uint32_t rs2, int32_t imm)
{
    uint32_t u = (uint32_t)imm;
    return ((u >> 5) << 25) | (rs2 << 20) | (rs1 << 15) |
           (funct3 << 12) | ((u & 0x1Fu) << 7) | 0x23u;
}

static uint32_t enc_b(uint32_t funct3, uint32_t rs1, uint32_t rs2, int32_t imm)
{
    uint32_t u = (uint32_t)imm;
    return (((u >> 12) & 1u) << 31) | (((u >> 5) & 0x3Fu) << 25) |
           (rs2 << 20) | (rs1 << 15) | (funct3 << 12) |
           (((u >> 1) & 0xFu) << 8) | (((u >> 11) & 1u) << 7) | 0x63u;
}

static uint32_t enc_u(uint32_t opcode, uint32_t rd, uint32_t imm20)
{
    return (imm20 << 12) | (rd << 7) | opcode;
}

static uint32_t enc_j(uint32_t rd, int32_t imm)
{
    uint32_t u = (uint32_t)imm;
    return (((u >> 20) & 1u) << 31) | (((u >> 1) & 0x3FFu) << 21) |
           (((u >> 11) & 1u) << 20) | (((u >> 12) & 0xFFu) << 12) |
           (rd << 7) | 0x6Fu;
}

/* --- Fixture ------------------------------------------------------------- */

static arcade1_bus_t bus;   /* Static: the bus embeds 8 KiB. */
static arcade1_cpu_t cpu;

/* Serialise instruction words little-endian into ROM and reset the CPU.
 * Endian-safe: bytes are written explicitly, so it does not depend on the
 * host's byte order. */
static void load(const uint32_t *prog, size_t n)
{
    uint8_t img[4 * 64];

    assert(n <= 64 && "program too large for harness buffer");
    for (size_t i = 0; i < n; i++) {
        img[4 * i + 0] = (uint8_t)(prog[i] & 0xFFu);
        img[4 * i + 1] = (uint8_t)((prog[i] >> 8) & 0xFFu);
        img[4 * i + 2] = (uint8_t)((prog[i] >> 16) & 0xFFu);
        img[4 * i + 3] = (uint8_t)((prog[i] >> 24) & 0xFFu);
    }
    arcade1_bus_init(&bus);
    arcade1_load_rom(&bus, img, 4 * n);
    arcade1_cpu_reset(&cpu);
}

int main(void)
{
    /* === Reset state ==================================================== */
    arcade1_bus_init(&bus);
    arcade1_cpu_reset(&cpu);
    assert(cpu.pc == ARCADE1_ROM_BASE && "reset PC must be ROM base");
    for (int i = 0; i < 32; i++)
        assert(cpu.regs[i] == 0 && "reset must clear all registers");

    /* === Arithmetic + x0 rule =========================================== */
    {
        const uint32_t prog[] = {
            enc_i(0x13, 0, 1, 0, 100),     /* ADDI x1, x0, 100 -> 100       */
            enc_i(0x13, 0, 2, 0, 23),      /* ADDI x2, x0, 23  -> 23        */
            enc_r(0x33, 0, 0x00, 3, 1, 2), /* ADD  x3, x1, x2  -> 123       */
            enc_r(0x33, 0, 0x20, 4, 1, 2), /* SUB  x4, x1, x2  -> 77        */
            enc_r(0x33, 0, 0x00, 0, 1, 2), /* ADD  x0, x1, x2  -> discarded */
        };
        load(prog, 5);
        for (int i = 0; i < 5; i++) arcade1_cpu_step(&cpu, &bus);

        /* ADDI x1, x0, 100 reading x0 as 0 already proves x0 reads as 0. */
        assert(cpu.regs[1] == 100 && "ADDI x1, x0, 100 -> 100");
        assert(cpu.regs[2] == 23   && "ADDI x2, x0, 23 -> 23");
        assert(cpu.regs[3] == 123  && "ADD x3, x1, x2 -> 123");
        assert(cpu.regs[4] == 77   && "SUB x4, x1, x2 -> 77");
        assert(cpu.regs[0] == 0    && "write to x0 must be discarded (x0==0)");
        assert(cpu.pc == 20        && "PC must advance 4 per instruction");
    }

    /* === Logical + shifts =============================================== */
    {
        const uint32_t prog[] = {
            enc_i(0x13, 0, 1, 0, 0x0FF),   /* ADDI x1, x0, 255              */
            enc_i(0x13, 0, 2, 0, 0x0F0),   /* ADDI x2, x0, 240              */
            enc_r(0x33, 7, 0, 3, 1, 2),    /* AND  x3, x1, x2 -> 240        */
            enc_r(0x33, 6, 0, 4, 1, 2),    /* OR   x4, x1, x2 -> 255        */
            enc_r(0x33, 4, 0, 5, 1, 2),    /* XOR  x5, x1, x2 -> 15         */
            enc_i(0x13, 0, 6, 0, -16),     /* ADDI x6, x0, -16 -> 0xFFFFFFF0 */
            enc_r(0x13, 5, 0x20, 7, 6, 2), /* SRAI x7, x6, 2 -> 0xFFFFFFFC  */
            enc_i(0x13, 5, 8, 6, 2),       /* SRLI x8, x6, 2 -> 0x3FFFFFFC  */
            enc_i(0x13, 1, 9, 1, 4),       /* SLLI x9, x1, 4 -> 4080        */
            enc_i(0x13, 0, 10, 0, 4),      /* ADDI x10, x0, 4               */
            enc_r(0x33, 5, 0x20, 11, 6, 10), /* SRA x11, x6, x10 -> -4      */
            enc_r(0x33, 5, 0x00, 12, 6, 10), /* SRL x12, x6, x10 -> 0x3FFFFFFC */
            enc_r(0x33, 1, 0x00, 13, 1, 10), /* SLL x13, x1, x10 -> 4096    */
            enc_i(0x13, 7, 14, 6, 0x0F0),  /* ANDI x14, x6, 240 -> 240      */
            enc_i(0x13, 6, 15, 6, 0x0F0),  /* ORI  x15, x6, 240 -> 0xFFFFFFF0 */
            enc_i(0x13, 4, 16, 6, 0x0F0),  /* XORI x16, x6, 240 -> 0xFFFFFF00 */
        };
        load(prog, 16);
        for (int i = 0; i < 16; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[3]  == 240              && "AND");
        assert(cpu.regs[4]  == 255              && "OR");
        assert(cpu.regs[5]  == 15               && "XOR");
        assert(cpu.regs[6]  == 0xFFFFFFF0u       && "ADDI -16");
        assert(cpu.regs[7]  == 0xFFFFFFFCu       && "SRAI arithmetic shift");
        assert(cpu.regs[8]  == 0x3FFFFFFCu       && "SRLI logical shift");
        assert(cpu.regs[9]  == 4080             && "SLLI");
        assert(cpu.regs[11] == 0xFFFFFFFFu       && "SRA by register (-16>>4)");
        assert(cpu.regs[12] == 0x0FFFFFFFu       && "SRL by register (-16>>4)");
        assert(cpu.regs[13] == 4080             && "SLL by register");
        assert(cpu.regs[14] == 240              && "ANDI");
        assert(cpu.regs[15] == 0xFFFFFFF0u       && "ORI (0xFFFFFFF0 | 0xF0)");
        assert(cpu.regs[16] == 0xFFFFFF00u       && "XORI (0xFFFFFFF0 ^ 0xF0)");
        assert(cpu.pc == 64);
    }

    /* === Immediate comparisons (SLTI/SLTIU) + register forms ============ */
    {
        const uint32_t prog[] = {
            enc_i(0x13, 0, 1, 0, -1),      /* ADDI x1, x0, -1 -> 0xFFFFFFFF */
            enc_i(0x13, 2, 2, 1, 0),       /* SLTI x2, x1, 0  -> 1 (signed)  */
            enc_i(0x13, 3, 3, 1, 0),       /* SLTIU x3, x1, 0 -> 0 (unsigned)*/
            enc_r(0x33, 2, 0, 4, 1, 0),    /* SLT  x4, x1, x0 -> 1 (signed)  */
            enc_r(0x33, 3, 0, 5, 1, 0),    /* SLTU x5, x1, x0 -> 0 (unsigned)*/
        };
        load(prog, 5);
        for (int i = 0; i < 5; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[2] == 1 && "SLTI: -1 < 0 signed -> 1");
        assert(cpu.regs[3] == 0 && "SLTIU: 0xFFFFFFFF < 0 unsigned -> 0");
        assert(cpu.regs[4] == 1 && "SLT: -1 < 0 signed -> 1");
        assert(cpu.regs[5] == 0 && "SLTU: 0xFFFFFFFF < 0 unsigned -> 0");
    }

    /* === LUI + AUIPC ==================================================== */
    {
        const uint32_t prog[] = {
            enc_u(0x37, 1, 0x12345),       /* LUI   x1, 0x12345            */
            enc_u(0x17, 2, 0x1),           /* AUIPC x2, 0x1 (at PC=4)      */
        };
        load(prog, 2);
        for (int i = 0; i < 2; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[1] == 0x12345000u && "LUI places imm in upper 20 bits");
        assert(cpu.regs[2] == 4u + 0x1000u && "AUIPC = PC + imm (PC=4)");
    }

    /* === Branches: taken / not-taken / signed vs unsigned =============== */
    {
        /* BEQ taken (skip ADDI x3), then BNE taken (skip nothing after). */
        const uint32_t prog[] = {
            enc_i(0x13, 0, 1, 0, 5),       /* pc0  ADDI x1, x0, 5           */
            enc_i(0x13, 0, 2, 0, 5),       /* pc4  ADDI x2, x0, 5           */
            enc_b(0, 1, 2, 8),             /* pc8  BEQ  x1, x2, +8 -> pc16  */
            enc_i(0x13, 0, 3, 0, 1),       /* pc12 ADDI x3 (skipped)        */
            enc_i(0x13, 0, 4, 0, 2),       /* pc16 ADDI x4, x0, 2           */
        };
        load(prog, 5);
        for (int i = 0; i < 4; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[3] == 0 && "BEQ taken must skip ADDI x3");
        assert(cpu.regs[4] == 2 && "BEQ taken lands on ADDI x4");
        assert(cpu.pc == 20);
    }
    {
        /* Signed BLT taken vs unsigned BLTU not taken for x1=-1, x2=1. */
        const uint32_t prog[] = {
            enc_i(0x13, 0, 1, 0, -1),      /* pc0  ADDI x1, x0, -1          */
            enc_i(0x13, 0, 2, 0, 1),       /* pc4  ADDI x2, x0, 1           */
            enc_b(4, 1, 2, 8),             /* pc8  BLT  x1, x2, +8 -> taken */
            enc_i(0x13, 0, 3, 0, 1),       /* pc12 ADDI x3 (skipped)        */
            enc_b(6, 1, 2, 8),             /* pc16 BLTU x1, x2, +8 -> NOT   */
            enc_i(0x13, 0, 4, 0, 1),       /* pc20 ADDI x4 (executed)       */
        };
        load(prog, 6);
        for (int i = 0; i < 5; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[3] == 0 && "BLT signed -1<1 taken skips x3");
        assert(cpu.regs[4] == 1 && "BLTU unsigned 0xFFFFFFFF<1 not taken");
        assert(cpu.pc == 24);
    }
    {
        /* BGE and BGEU both taken for x1 == x2 == 5. */
        const uint32_t prog[] = {
            enc_i(0x13, 0, 1, 0, 5),       /* pc0  ADDI x1, x0, 5           */
            enc_i(0x13, 0, 2, 0, 5),       /* pc4  ADDI x2, x0, 5           */
            enc_b(5, 1, 2, 8),             /* pc8  BGE  x1, x2, +8 -> pc16  */
            enc_i(0x13, 0, 3, 0, 1),       /* pc12 ADDI x3 (skipped)        */
            enc_b(7, 1, 2, 8),             /* pc16 BGEU x1, x2, +8 -> pc24  */
            enc_i(0x13, 0, 4, 0, 1),       /* pc20 ADDI x4 (skipped)        */
            enc_i(0x13, 0, 5, 0, 9),       /* pc24 ADDI x5, x0, 9           */
        };
        load(prog, 7);
        for (int i = 0; i < 5; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[3] == 0 && "BGE taken skips x3");
        assert(cpu.regs[4] == 0 && "BGEU taken skips x4");
        assert(cpu.regs[5] == 9 && "BGE/BGEU land on ADDI x5");
        assert(cpu.pc == 28);
    }
    {
        /* Backward branch loop: count i from 0 up to 3. */
        const uint32_t prog[] = {
            enc_i(0x13, 0, 1, 0, 0),       /* pc0  ADDI x1, x0, 0  (i=0)    */
            enc_i(0x13, 0, 2, 0, 3),       /* pc4  ADDI x2, x0, 3  (limit)  */
            enc_i(0x13, 0, 1, 1, 1),       /* pc8  ADDI x1, x1, 1  (i++)    */
            enc_b(1, 1, 2, -4),            /* pc12 BNE  x1, x2, -4 -> pc8   */
        };
        load(prog, 4);
        for (int i = 0; i < 8; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[1] == 3 && "loop must run until i == 3");
        assert(cpu.pc == 16 && "loop must exit forward past BNE");
    }

    /* === Jumps ========================================================== */
    {
        const uint32_t prog[] = {
            enc_j(1, 8),                   /* pc0  JAL  x1, +8 -> pc8, x1=4 */
            enc_i(0x13, 0, 2, 0, 1),       /* pc4  ADDI x2 (skipped)        */
            enc_i(0x13, 0, 3, 0, 2),       /* pc8  ADDI x3, x0, 2           */
        };
        load(prog, 3);
        for (int i = 0; i < 2; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[1] == 4  && "JAL link register = PC + 4");
        assert(cpu.regs[2] == 0  && "JAL taken skips ADDI x2");
        assert(cpu.regs[3] == 2  && "JAL lands on ADDI x3");
    }
    {
        const uint32_t prog[] = {
            enc_i(0x13, 0, 5, 0, 12),      /* pc0  ADDI x5, x0, 12          */
            enc_i(0x67, 0, 1, 5, 0),       /* pc4  JALR x1, 0(x5) -> pc12   */
            enc_i(0x13, 0, 2, 0, 1),       /* pc8  ADDI x2 (skipped)        */
            enc_i(0x13, 0, 3, 0, 2),       /* pc12 ADDI x3, x0, 2           */
        };
        load(prog, 4);
        for (int i = 0; i < 3; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[5] == 12 && "JALR base register");
        assert(cpu.regs[1] == 8   && "JALR link = PC + 4");
        assert(cpu.regs[2] == 0   && "JALR taken skips ADDI x2");
        assert(cpu.regs[3] == 2   && "JALR lands on ADDI x3");
        assert(cpu.pc == 16);
    }

    /* === Loads and stores =============================================== */
    {
        const uint32_t prog[] = {
            enc_u(0x37, 2, 0x1),           /* pc0  LUI  x2, 0x1 -> 0x1000   */
            enc_u(0x37, 1, 0x8),           /* pc4  LUI  x1, 0x8 -> 0x8000   */
            enc_s(1, 2, 1, 0),             /* pc8  SH   x1, 0(x2)           */
            enc_i(0x03, 1, 3, 2, 0),       /* pc12 LH   x3, 0(x2)           */
            enc_i(0x03, 5, 4, 2, 0),       /* pc16 LHU  x4, 0(x2)           */
            enc_i(0x13, 0, 5, 0, 0x7F),    /* pc20 ADDI x5, x0, 127         */
            enc_s(0, 2, 5, 4),             /* pc24 SB   x5, 4(x2)           */
            enc_i(0x03, 0, 6, 2, 4),       /* pc28 LB   x6, 4(x2)           */
            enc_i(0x03, 4, 7, 2, 4),       /* pc32 LBU  x7, 4(x2)           */
            enc_i(0x13, 0, 8, 0, -1),      /* pc36 ADDI x8, x0, -1          */
            enc_s(0, 2, 8, 5),             /* pc40 SB   x8, 5(x2)           */
            enc_i(0x03, 0, 9, 2, 5),       /* pc44 LB   x9, 5(x2)           */
            enc_i(0x03, 4, 10, 2, 5),      /* pc48 LBU  x10, 5(x2)          */
            enc_i(0x13, 0, 11, 0, 0x123),  /* pc52 ADDI x11, x0, 0x123      */
            enc_s(2, 2, 11, 8),            /* pc56 SW   x11, 8(x2)          */
            enc_i(0x03, 2, 12, 2, 8),      /* pc60 LW   x12, 8(x2)          */
        };
        load(prog, 16);
        for (int i = 0; i < 16; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[2]  == 0x1000u          && "LUI RAM base");
        assert(cpu.regs[1]  == 0x8000u          && "LUI 0x8000");
        assert(cpu.regs[3]  == 0xFFFF8000u      && "LH sign-extends 0x8000");
        assert(cpu.regs[4]  == 0x8000u          && "LHU zero-extends 0x8000");
        assert(cpu.regs[6]  == 127              && "LB positive byte");
        assert(cpu.regs[7]  == 127              && "LBU positive byte");
        assert(cpu.regs[8]  == 0xFFFFFFFFu       && "ADDI -1");
        assert(cpu.regs[9]  == 0xFFFFFFFFu       && "LB sign-extends 0xFF");
        assert(cpu.regs[10] == 0xFF             && "LBU zero-extends 0xFF");
        assert(cpu.regs[12] == 0x123            && "LW round-trip");
        assert(cpu.pc == 64);

        /* Verify the stored bytes landed in RAM as expected. */
        assert(arcade1_read16(&bus, 0x1000u) == 0x8000u && "SH to RAM");
        assert(arcade1_read8(&bus, 0x1004u) == 0x7Fu    && "SB to RAM");
        assert(arcade1_read8(&bus, 0x1005u) == 0xFFu    && "SB to RAM (high)");
        assert(arcade1_read32(&bus, 0x1008u) == 0x123u  && "SW to RAM");
    }

    /* === Unknown / system words act as NOP (PC advances) ================ */
    {
        const uint32_t prog[] = {
            enc_i(0x13, 0, 1, 0, 5),       /* pc0  ADDI x1, x0, 5           */
            0x00000073u,                   /* pc4  ECALL -> NOP             */
            0x00000000u,                   /* pc8  zero word -> UNKNOWN NOP */
            enc_i(0x13, 0, 2, 0, 6),       /* pc12 ADDI x2, x0, 6           */
        };
        load(prog, 4);
        for (int i = 0; i < 4; i++) arcade1_cpu_step(&cpu, &bus);

        assert(cpu.regs[1] == 5 && "ADDI before NOPs");
        assert(cpu.regs[2] == 6 && "execution continues after NOPs");
        assert(cpu.pc == 16 && "NOPs must advance PC by 4");
    }

    printf("milestone09: all assertions passed\n");
    printf("  verified: x0==0, arithmetic, logical, shifts, comparisons,\n");
    printf("            LUI/AUIPC, all 6 branches (taken/skipped, sgn/uns),\n");
    printf("            backward loop, JAL/JALR, byte/half/word loads+stores,\n");
    printf("            immediates, PC advance, NOP for unknown/system\n");

    return 0;
}
