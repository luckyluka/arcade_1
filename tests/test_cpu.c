/* ARCADE-1 — Milestone 08 test harness (extended at milestone 16).
 *
 * Unit tests for the RV32IM decoder. Links ONLY src/arcade1/cpu.c (no
 * libretro, no machine, no .so), proving it is independently testable.
 *
 * Covers what the roadmap requires: opcode extraction, register fields,
 * immediate decoding, instruction formats, branch offsets, and load/store
 * offsets — plus the M extension (MUL..REMU) added at M16. Instructions are
 * hand-assembled from their fields so the expected decode is known exactly.
 */

#include "cpu.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>

/* --- Instruction assemblers (encode fields -> 32-bit word) --------------- */

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

int main(void)
{
    arcade1_insn_t d;

    /* === Opcode extraction ============================================== */
    d = arcade1_decode(enc_r(0x33, 0, 0, 1, 2, 3));   /* ADD */
    assert(d.opcode == 0x33 && "ADD opcode must be 0x33");

    /* === Register fields ================================================ */
    /* ADD x5, x6, x7  -> rd=5, rs1=6, rs2=7 */
    d = arcade1_decode(enc_r(0x33, 0, 0, 5, 6, 7));
    assert(d.rd == 5 && d.rs1 == 6 && d.rs2 == 7 &&
           "ADD must extract rd=5, rs1=6, rs2=7");
    assert(d.funct3 == 0 && d.funct7 == 0 &&
           "ADD funct3/funct7 must be 0");
    assert(d.fmt == ARCADE1_FMT_R && d.op == ARCADE1_OP_ADD &&
           "ADD must classify as R-format ADD");

    /* SUB x5, x6, x7 -> funct7=0x20 */
    d = arcade1_decode(enc_r(0x33, 0, 0x20, 5, 6, 7));
    assert(d.op == ARCADE1_OP_SUB && "SUB must classify via funct7=0x20");

    /* Register fields are 5 bits: x31 max. */
    d = arcade1_decode(enc_r(0x33, 0, 0, 31, 31, 31));
    assert(d.rd == 31 && d.rs1 == 31 && d.rs2 == 31 &&
           "register fields must be 5 bits (x31)");

    /* === Immediate decoding (I-type) ==================================== */
    /* ADDI x1, x2, 0x123 -> imm = 0x123 */
    d = arcade1_decode(enc_i(0x13, 0, 1, 2, 0x123));
    assert(d.imm == 0x123 && d.rd == 1 && d.rs1 == 2 &&
           "ADDI positive immediate must decode to 0x123");
    assert(d.fmt == ARCADE1_FMT_I && d.op == ARCADE1_OP_ADDI &&
           "ADDI must classify as I-format ADDI");

    /* ADDI with negative immediate: -1 -> 0xFFF sign-extends to -1. */
    d = arcade1_decode(enc_i(0x13, 0, 1, 2, -1));
    assert(d.imm == -1 && "ADDI -1 must sign-extend to -1");

    /* ADDI -2048 (most negative I-type) */
    d = arcade1_decode(enc_i(0x13, 0, 1, 2, -2048));
    assert(d.imm == -2048 && "ADDI -2048 must decode correctly");

    /* SLTIU compares against zero-extended immediate but the field is still
     * sign-extended into imm; verify classification. */
    d = arcade1_decode(enc_i(0x13, 3, 1, 2, 5));
    assert(d.op == ARCADE1_OP_SLTIU && "SLTIU must classify (funct3=3)");

    /* === Load offsets (I-type) ========================================== */
    /* LW x3, 8(x4): opcode=0x03, funct3=2, imm=8 (the load offset). */
    d = arcade1_decode(enc_i(0x03, 2, 3, 4, 8));
    assert(d.op == ARCADE1_OP_LW && d.fmt == ARCADE1_FMT_I &&
           "LW must classify as I-format LW");
    assert(d.imm == 8 && d.rd == 3 && d.rs1 == 4 &&
           "LW offset must decode to +8 into rd from base rs1");

    /* LB x1, -1(x2): negative load offset. */
    d = arcade1_decode(enc_i(0x03, 0, 1, 2, -1));
    assert(d.op == ARCADE1_OP_LB && d.imm == -1 &&
           "LB negative offset must sign-extend");

    /* LBU funct3=4, LHU funct3=5 */
    assert(arcade1_decode(enc_i(0x03, 4, 1, 2, 0)).op == ARCADE1_OP_LBU &&
           "LBU must classify (funct3=4)");
    assert(arcade1_decode(enc_i(0x03, 5, 1, 2, 0)).op == ARCADE1_OP_LHU &&
           "LHU must classify (funct3=5)");

    /* === Store offsets (S-type) ========================================= */
    /* SW x3, 12(x4): opcode=0x23, funct3=2, imm=12 (the store offset). */
    d = arcade1_decode(enc_s(2, 4, 3, 12));
    assert(d.op == ARCADE1_OP_SW && d.fmt == ARCADE1_FMT_S &&
           "SW must classify as S-format SW");
    assert(d.imm == 12 && d.rs1 == 4 && d.rs2 == 3 &&
           "SW offset must decode to +12 (base rs1, value rs2)");

    /* SH funct3=1, SB funct3=0. */
    assert(arcade1_decode(enc_s(1, 4, 3, 0)).op == ARCADE1_OP_SH &&
           "SH must classify (funct3=1)");
    assert(arcade1_decode(enc_s(0, 4, 3, 0)).op == ARCADE1_OP_SB &&
           "SB must classify (funct3=0)");

    /* S-type negative offset, and the split imm field (imm[4:0] in rd slot). */
    d = arcade1_decode(enc_s(2, 4, 3, -4));
    assert(d.imm == -4 && "SW negative offset must sign-extend");

    /* === Branch offsets (B-type) ======================================== */
    /* BEQ x1, x2, +16: funct3=0, imm=+16 (byte offset, already >>1 encoded). */
    d = arcade1_decode(enc_b(0, 1, 2, 16));
    assert(d.op == ARCADE1_OP_BEQ && d.fmt == ARCADE1_FMT_B &&
           "BEQ must classify as B-format BEQ");
    assert(d.imm == 16 && d.rs1 == 1 && d.rs2 == 2 &&
           "BEQ offset must decode to +16");

    /* BEQ with negative offset -16. */
    d = arcade1_decode(enc_b(0, 1, 2, -16));
    assert(d.imm == -16 && "BEQ negative offset must sign-extend");

    /* BLT funct3=4, BGE funct3=5, BLTU funct3=6, BGEU funct3=7, BNE funct3=1. */
    assert(arcade1_decode(enc_b(1, 1, 2, 0)).op == ARCADE1_OP_BNE &&
           "BNE must classify (funct3=1)");
    assert(arcade1_decode(enc_b(4, 1, 2, 0)).op == ARCADE1_OP_BLT &&
           "BLT must classify (funct3=4)");
    assert(arcade1_decode(enc_b(5, 1, 2, 0)).op == ARCADE1_OP_BGE &&
           "BGE must classify (funct3=5)");
    assert(arcade1_decode(enc_b(6, 1, 2, 0)).op == ARCADE1_OP_BLTU &&
           "BLTU must classify (funct3=6)");
    assert(arcade1_decode(enc_b(7, 1, 2, 0)).op == ARCADE1_OP_BGEU &&
           "BGEU must classify (funct3=7)");

    /* === U-type (LUI/AUIPC) ============================================= */
    /* LUI x5, 0x12345 -> imm = 0x12345000. */
    d = arcade1_decode(enc_u(0x37, 5, 0x12345));
    assert(d.op == ARCADE1_OP_LUI && d.fmt == ARCADE1_FMT_U &&
           "LUI must classify as U-format LUI");
    assert(d.imm == (int32_t)0x12345000 && d.rd == 5 &&
           "LUI immediate must be imm20<<12");

    /* AUIPC opcode=0x17. */
    assert(arcade1_decode(enc_u(0x17, 5, 0x1)).op == ARCADE1_OP_AUIPC &&
           "AUIPC must classify (opcode=0x17)");

    /* === J-type (JAL) =================================================== */
    /* JAL x1, +0x1000 */
    d = arcade1_decode(enc_j(1, 0x1000));
    assert(d.op == ARCADE1_OP_JAL && d.fmt == ARCADE1_FMT_J &&
           "JAL must classify as J-format JAL");
    assert(d.imm == 0x1000 && d.rd == 1 && "JAL offset must decode to +0x1000");

    /* JAL negative offset. */
    d = arcade1_decode(enc_j(1, -8));
    assert(d.imm == -8 && "JAL negative offset must sign-extend");

    /* === JALR (I-format) ================================================ */
    d = arcade1_decode(enc_i(0x67, 0, 1, 2, 4));
    assert(d.op == ARCADE1_OP_JALR && d.fmt == ARCADE1_FMT_I &&
           "JALR must classify as I-format JALR");

    /* === Shifts / OP-IMM sub-classes ==================================== */
    assert(arcade1_decode(enc_i(0x13, 1, 1, 2, 3)).op == ARCADE1_OP_SLLI &&
           "SLLI must classify (funct3=1, funct7=0)");
    assert(arcade1_decode(enc_i(0x13, 5, 1, 2, 3)).op == ARCADE1_OP_SRLI &&
           "SRLI must classify (funct3=5, funct7=0)");
    /* SRAI sets funct7=0x20 (bit 30). */
    assert(arcade1_decode(enc_r(0x13, 5, 0x20, 1, 2, 3)).op == ARCADE1_OP_SRAI &&
           "SRAI must classify (funct3=5, funct7=0x20)");

    /* === M extension (funct7 = 0000001 on OP) ============================ */
    {
        static const arcade1_op_t mops[8] = {
            ARCADE1_OP_MUL, ARCADE1_OP_MULH, ARCADE1_OP_MULHSU,
            ARCADE1_OP_MULHU, ARCADE1_OP_DIV, ARCADE1_OP_DIVU,
            ARCADE1_OP_REM, ARCADE1_OP_REMU,
        };
        for (uint32_t f3 = 0; f3 < 8; f3++) {
            d = arcade1_decode(enc_r(0x33, f3, 0x01, 5, 6, 7));
            assert(d.op == mops[f3] && "funct7=0x01 must classify MUL..REMU");
            assert(d.fmt == ARCADE1_FMT_R && "M ops are R-format");
            assert(d.rd == 5 && d.rs1 == 6 && d.rs2 == 7 &&
                   "M op must extract rd/rs1/rs2");
            assert(d.funct3 == f3 && d.funct7 == 0x01 &&
                   "M op must keep its funct3/funct7");
        }
        /* The base OP decode is unaffected: funct7=0 still yields ADD. */
        assert(arcade1_decode(enc_r(0x33, 0, 0x00, 1, 2, 3)).op ==
                   ARCADE1_OP_ADD && "ADD must still classify");
        /* funct7=0x01 is M-extension ONLY for OP: OP-IMM shift space with
         * bit 25 set is still a reserved encoding. */
        assert(arcade1_decode(enc_r(0x13, 1, 0x01, 1, 2, 3)).op ==
                   ARCADE1_OP_UNKNOWN &&
               "OP-IMM funct7=0x01 must stay UNKNOWN (no M on OP-IMM)");
    }

    /* === System ========================================================= */
    assert(arcade1_decode(0x00000073u).op == ARCADE1_OP_ECALL &&
           "ECALL must classify");
    assert(arcade1_decode(0x00100073u).op == ARCADE1_OP_EBREAK &&
           "EBREAK must classify");

    /* === Unknown encodings ============================================== */
    assert(arcade1_decode(0x00000000u).op == ARCADE1_OP_UNKNOWN &&
           "zero word must be UNKNOWN");
    /* Reserved OP funct7 patterns must not classify: 0x01 is the M extension
     * (MUL..REMU, tested above); anything else besides 0x00/0x20 is bad. */
    assert(arcade1_decode(enc_r(0x33, 0, 0x02, 1, 2, 3)).op == ARCADE1_OP_UNKNOWN &&
           "bad OP funct7 must be UNKNOWN");
    assert(arcade1_decode(enc_r(0x33, 4, 0x21, 1, 2, 3)).op == ARCADE1_OP_UNKNOWN &&
           "bad OP funct7 must be UNKNOWN");

    printf("milestone08: all assertions passed\n");
    printf("  verified: opcode, register fields, I/S/B/U/J immediates,\n");
    printf("            formats, branch offsets, load/store offsets,\n");
    printf("            OP/OP-IMM sub-classes, M extension (MUL..REMU),\n");
    printf("            system, unknown encodings\n");

    return 0;
}
