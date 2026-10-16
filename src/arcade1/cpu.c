/* ARCADE-1 CPU — RV32I instruction decoder (milestone 08) implementation.
 *
 * See cpu.h. Stateless field/immediate extraction + opcode classification per
 * the RISC-V spec. No execution. No host dependency.
 */

#include "cpu.h"

/* --- Field extraction ---------------------------------------------------- */

/* Sign-extend the low `bits` of v to 32 bits. */
static int32_t sign_extend(uint32_t v, unsigned bits)
{
    uint32_t m = 1u << (bits - 1);
    return (int32_t)((v ^ m) - m);
}

/* --- Immediate decoding, per format -------------------------------------- */

/* Extract the immediate for a given format from the raw word. Widths and bit
 * placements follow the RISC-V spec:
 *   I: inst[31:20]                       (12-bit, sign-extended)
 *   S: inst[31:25|11:7]                  (12-bit, sign-extended)
 *   B: inst[31|7|30:25|11:8] << 1        (13-bit, sign-extended)
 *   U: inst[31:12] << 12                 (upper 20 bits)
 *   J: inst[31|19:12|20|30:21] << 1      (21-bit, sign-extended)
 */
static int32_t decode_imm(uint32_t inst, arcade1_fmt_t fmt)
{
    switch (fmt) {
    case ARCADE1_FMT_I:
        return sign_extend(inst >> 20, 12);

    case ARCADE1_FMT_S:
        return sign_extend(((inst >> 25) & 0x7Fu) << 5 |
                           ((inst >> 7) & 0x1Fu), 12);

    case ARCADE1_FMT_B:
        return sign_extend(((inst >> 31) & 0x1u) << 12 |
                           ((inst >> 7) & 0x1u) << 11 |
                           ((inst >> 25) & 0x3Fu) << 5 |
                           ((inst >> 8) & 0xFu) << 1, 13);

    case ARCADE1_FMT_U:
        /* Upper 20 bits already sit in place; low 12 bits are zero. */
        return (int32_t)(inst & 0xFFFFF000u);

    case ARCADE1_FMT_J:
        return sign_extend(((inst >> 31) & 0x1u) << 20 |
                           ((inst >> 12) & 0xFFu) << 12 |
                           ((inst >> 20) & 0x1u) << 11 |
                           ((inst >> 21) & 0x3FFu) << 1, 21);

    default:
        return 0;
    }
}

/* --- Format classification ----------------------------------------------- */

static arcade1_fmt_t classify_fmt(uint32_t opcode)
{
    switch (opcode) {
    case 0x37:                          /* LUI   */
    case 0x17:                          /* AUIPC */
        return ARCADE1_FMT_U;
    case 0x6F:                          /* JAL   */
        return ARCADE1_FMT_J;
    case 0x63:                          /* BRANCH*/
        return ARCADE1_FMT_B;
    case 0x23:                          /* STORE */
        return ARCADE1_FMT_S;
    case 0x03:                          /* LOAD  */
    case 0x13:                          /* OP-IMM*/
    case 0x67:                          /* JALR  */
        return ARCADE1_FMT_I;
    case 0x33:                          /* OP    */
        return ARCADE1_FMT_R;
    default:
        return ARCADE1_FMT_UNKNOWN;
    }
}

/* --- Operation classification -------------------------------------------- */

/* funct3 values per major opcode (RISC-V spec). */
static arcade1_op_t classify_op(uint32_t inst, uint32_t opcode,
                                uint32_t rd, uint32_t funct3, uint32_t rs1,
                                uint32_t rs2, uint32_t funct7)
{
    (void)rd; (void)rs1; (void)rs2;

    switch (opcode) {
    case 0x37: return ARCADE1_OP_LUI;
    case 0x17: return ARCADE1_OP_AUIPC;
    case 0x6F: return ARCADE1_OP_JAL;

    case 0x67:                          /* JALR (funct3 must be 0) */
        return (funct3 == 0) ? ARCADE1_OP_JALR : ARCADE1_OP_UNKNOWN;

    case 0x63:                          /* BRANCH */
        switch (funct3) {
        case 0: return ARCADE1_OP_BEQ;
        case 1: return ARCADE1_OP_BNE;
        case 4: return ARCADE1_OP_BLT;
        case 5: return ARCADE1_OP_BGE;
        case 6: return ARCADE1_OP_BLTU;
        case 7: return ARCADE1_OP_BGEU;
        default: return ARCADE1_OP_UNKNOWN;
        }

    case 0x03:                          /* LOAD */
        switch (funct3) {
        case 0: return ARCADE1_OP_LB;
        case 1: return ARCADE1_OP_LH;
        case 2: return ARCADE1_OP_LW;
        case 4: return ARCADE1_OP_LBU;
        case 5: return ARCADE1_OP_LHU;
        default: return ARCADE1_OP_UNKNOWN;
        }

    case 0x23:                          /* STORE */
        switch (funct3) {
        case 0: return ARCADE1_OP_SB;
        case 1: return ARCADE1_OP_SH;
        case 2: return ARCADE1_OP_SW;
        default: return ARCADE1_OP_UNKNOWN;
        }

    case 0x13:                          /* OP-IMM */
        switch (funct3) {
        case 0: return ARCADE1_OP_ADDI;
        case 2: return ARCADE1_OP_SLTI;
        case 3: return ARCADE1_OP_SLTIU;
        case 4: return ARCADE1_OP_XORI;
        case 6: return ARCADE1_OP_ORI;
        case 7: return ARCADE1_OP_ANDI;
        case 1:                          /* SLLI: funct7 must be 0 */
            return (funct7 == 0) ? ARCADE1_OP_SLLI : ARCADE1_OP_UNKNOWN;
        case 5:                          /* SRLI (funct7=0) / SRAI (funct7=0x20) */
            if (funct7 == 0x00) return ARCADE1_OP_SRLI;
            if (funct7 == 0x20) return ARCADE1_OP_SRAI;
            return ARCADE1_OP_UNKNOWN;
        default: return ARCADE1_OP_UNKNOWN;
        }

    case 0x33:                          /* OP */
        switch (funct3) {
        case 0:                          /* ADD (funct7=0) / SUB (funct7=0x20) */
            if (funct7 == 0x00) return ARCADE1_OP_ADD;
            if (funct7 == 0x20) return ARCADE1_OP_SUB;
            return ARCADE1_OP_UNKNOWN;
        case 1: return (funct7 == 0) ? ARCADE1_OP_SLL  : ARCADE1_OP_UNKNOWN;
        case 2: return (funct7 == 0) ? ARCADE1_OP_SLT  : ARCADE1_OP_UNKNOWN;
        case 3: return (funct7 == 0) ? ARCADE1_OP_SLTU : ARCADE1_OP_UNKNOWN;
        case 4: return (funct7 == 0) ? ARCADE1_OP_XOR  : ARCADE1_OP_UNKNOWN;
        case 5:                          /* SRL (funct7=0) / SRA (funct7=0x20) */
            if (funct7 == 0x00) return ARCADE1_OP_SRL;
            if (funct7 == 0x20) return ARCADE1_OP_SRA;
            return ARCADE1_OP_UNKNOWN;
        case 6: return (funct7 == 0) ? ARCADE1_OP_OR   : ARCADE1_OP_UNKNOWN;
        case 7: return (funct7 == 0) ? ARCADE1_OP_AND  : ARCADE1_OP_UNKNOWN;
        default: return ARCADE1_OP_UNKNOWN;
        }

    case 0x0F:                          /* MISC-MEM (FENCE) */
        return (funct3 == 0) ? ARCADE1_OP_FENCE : ARCADE1_OP_UNKNOWN;

    case 0x73:                          /* SYSTEM */
        if (funct3 == 0) {
            if (inst == 0x00000073u) return ARCADE1_OP_ECALL;
            if (inst == 0x00100073u) return ARCADE1_OP_EBREAK;
        }
        return ARCADE1_OP_UNKNOWN;

    default:
        return ARCADE1_OP_UNKNOWN;
    }
}

/* --- Public API ---------------------------------------------------------- */

arcade1_insn_t arcade1_decode(uint32_t inst)
{
    arcade1_insn_t d;

    d.raw    = inst;
    d.opcode = (inst >> 0) & 0x7Fu;     /* bits 6:0   */
    d.rd     = (inst >> 7) & 0x1Fu;     /* bits 11:7  */
    d.funct3 = (inst >> 12) & 0x7u;     /* bits 14:12 */
    d.rs1    = (inst >> 15) & 0x1Fu;    /* bits 19:15 */
    d.rs2    = (inst >> 20) & 0x1Fu;    /* bits 24:20 */
    d.funct7 = (inst >> 25) & 0x7Fu;    /* bits 31:25 */

    d.fmt = classify_fmt(d.opcode);
    d.imm = decode_imm(inst, d.fmt);
    d.op  = classify_op(inst, d.opcode, d.rd, d.funct3, d.rs1, d.rs2, d.funct7);

    return d;
}
