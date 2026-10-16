/* ARCADE-1 CPU — RV32I instruction decoder (milestone 08).
 *
 * Pure, stateless decoding of a 32-bit RV32I instruction word into its fields,
 * immediate, format, and a classified operation. There is NO execution state
 * (no registers, no PC) yet — that arrives with milestone 09. This decoder is
 * independently testable (see tests/test_cpu.c).
 *
 * No dependency on libretro, the machine, or any host frontend.
 */

#ifndef ARCADE1_CPU_H
#define ARCADE1_CPU_H

#include <stdint.h>

/* --- Instruction format -------------------------------------------------- */

typedef enum arcade1_fmt {
    ARCADE1_FMT_UNKNOWN = 0,
    ARCADE1_FMT_R,      /* register-register                          */
    ARCADE1_FMT_I,      /* immediate (also loads, JALR)               */
    ARCADE1_FMT_S,      /* store                                      */
    ARCADE1_FMT_B,      /* branch                                     */
    ARCADE1_FMT_U,      /* LUI / AUIPC (upper immediate)              */
    ARCADE1_FMT_J,      /* JAL                                        */
} arcade1_fmt_t;

/* --- Classified RV32I operation ------------------------------------------ */

typedef enum arcade1_op {
    ARCADE1_OP_UNKNOWN = 0,

    /* U-type */
    ARCADE1_OP_LUI, ARCADE1_OP_AUIPC,
    /* Jumps */
    ARCADE1_OP_JAL, ARCADE1_OP_JALR,
    /* Branches */
    ARCADE1_OP_BEQ, ARCADE1_OP_BNE, ARCADE1_OP_BLT,
    ARCADE1_OP_BGE, ARCADE1_OP_BLTU, ARCADE1_OP_BGEU,
    /* Loads */
    ARCADE1_OP_LB, ARCADE1_OP_LH, ARCADE1_OP_LW,
    ARCADE1_OP_LBU, ARCADE1_OP_LHU,
    /* Stores */
    ARCADE1_OP_SB, ARCADE1_OP_SH, ARCADE1_OP_SW,
    /* OP-IMM */
    ARCADE1_OP_ADDI, ARCADE1_OP_SLTI, ARCADE1_OP_SLTIU, ARCADE1_OP_XORI,
    ARCADE1_OP_ORI, ARCADE1_OP_ANDI, ARCADE1_OP_SLLI, ARCADE1_OP_SRLI,
    ARCADE1_OP_SRAI,
    /* OP (register-register) */
    ARCADE1_OP_ADD, ARCADE1_OP_SUB, ARCADE1_OP_SLL, ARCADE1_OP_SLT,
    ARCADE1_OP_SLTU, ARCADE1_OP_XOR, ARCADE1_OP_SRL, ARCADE1_OP_SRA,
    ARCADE1_OP_OR, ARCADE1_OP_AND,
    /* System / misc */
    ARCADE1_OP_FENCE, ARCADE1_OP_ECALL, ARCADE1_OP_EBREAK,
} arcade1_op_t;

/* --- Decoded instruction ------------------------------------------------- */

typedef struct arcade1_insn {
    uint32_t      raw;      /* original instruction word                  */
    uint32_t      opcode;   /* bits 6:0                                   */
    uint32_t      rd;       /* bits 11:7                                  */
    uint32_t      funct3;   /* bits 14:12                                 */
    uint32_t      rs1;      /* bits 19:15                                 */
    uint32_t      rs2;      /* bits 24:20                                 */
    uint32_t      funct7;   /* bits 31:25                                 */
    int32_t       imm;      /* immediate, sign-extended per format        */
    arcade1_fmt_t fmt;      /* instruction format                         */
    arcade1_op_t  op;       /* classified operation                       */
} arcade1_insn_t;

/* Decode a 32-bit RV32I instruction word. Always succeeds; unrecognised
 * encodings yield ARCADE1_OP_UNKNOWN / ARCADE1_FMT_UNKNOWN. Deterministic. */
arcade1_insn_t arcade1_decode(uint32_t inst);

#endif /* ARCADE1_CPU_H */
