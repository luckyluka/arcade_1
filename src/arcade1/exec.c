/* ARCADE-1 CPU execution — RV32I interpreter (milestone 09) implementation.
 *
 * See exec.h. One instruction per call: fetch (via the bus), decode (cpu.h),
 * execute, advance PC. x0 is hardwired to zero through reg_get/reg_set.
 *
 * The switch covers every arcade1_op_t and has no `default`, so -Wswitch
 * flags any operation that is classified but not yet executed — keeping the
 * decoder and interpreter in lockstep. FENCE/ECALL/EBREAK and unknown words
 * currently fall through as NOP (PC already advances); there is no OS/syscall
 * handling in this milestone.
 */

#include "exec.h"

/* --- Register file, with the x0 rule ------------------------------------- */

/* Read a register: x0 always reads as 0. */
static uint32_t reg_get(const arcade1_cpu_t *c, uint32_t i)
{
    return i ? c->regs[i] : 0u;
}

/* Write a register: writes to x0 are discarded. */
static void reg_set(arcade1_cpu_t *c, uint32_t i, uint32_t v)
{
    if (i)
        c->regs[i] = v;
}

/* --- Lifecycle ----------------------------------------------------------- */

void arcade1_cpu_reset(arcade1_cpu_t *cpu)
{
    for (int i = 0; i < 32; i++)
        cpu->regs[i] = 0u;
    cpu->pc = ARCADE1_ROM_BASE;
}

/* --- Execute one instruction --------------------------------------------- */

void arcade1_cpu_step(arcade1_cpu_t *cpu, arcade1_bus_t *bus)
{
    arcade1_insn_t d = arcade1_decode(arcade1_read32(bus, cpu->pc));
    uint32_t rs1 = reg_get(cpu, d.rs1);
    uint32_t rs2 = reg_get(cpu, d.rs2);
    uint32_t imm = (uint32_t)d.imm;     /* sign-extended immediate as bits   */
    uint32_t next = cpu->pc + 4u;       /* default: fall through             */
    uint32_t addr;

    switch (d.op) {
    /* --- U-type --- */
    case ARCADE1_OP_LUI:
        reg_set(cpu, d.rd, imm);
        break;
    case ARCADE1_OP_AUIPC:
        reg_set(cpu, d.rd, cpu->pc + imm);
        break;

    /* --- Jumps --- */
    case ARCADE1_OP_JAL:
        reg_set(cpu, d.rd, cpu->pc + 4u);
        next = cpu->pc + imm;
        break;
    case ARCADE1_OP_JALR: {
        uint32_t ret = cpu->pc + 4u;
        next = (rs1 + imm) & ~1u;       /* target with bit 0 cleared         */
        reg_set(cpu, d.rd, ret);
        break;
    }

    /* --- Branches (taken -> PC += imm) --- */
    case ARCADE1_OP_BEQ:  if (rs1 == rs2)                   next = cpu->pc + imm; break;
    case ARCADE1_OP_BNE:  if (rs1 != rs2)                   next = cpu->pc + imm; break;
    case ARCADE1_OP_BLT:  if ((int32_t)rs1 <  (int32_t)rs2) next = cpu->pc + imm; break;
    case ARCADE1_OP_BGE:  if ((int32_t)rs1 >= (int32_t)rs2) next = cpu->pc + imm; break;
    case ARCADE1_OP_BLTU: if (rs1 <  rs2)                   next = cpu->pc + imm; break;
    case ARCADE1_OP_BGEU: if (rs1 >= rs2)                   next = cpu->pc + imm; break;

    /* --- Loads (address = rs1 + imm) --- */
    case ARCADE1_OP_LB:
        addr = rs1 + imm;
        reg_set(cpu, d.rd, (uint32_t)(int32_t)(int8_t)arcade1_read8(bus, addr));
        break;
    case ARCADE1_OP_LH:
        addr = rs1 + imm;
        reg_set(cpu, d.rd, (uint32_t)(int32_t)(int16_t)arcade1_read16(bus, addr));
        break;
    case ARCADE1_OP_LW:
        addr = rs1 + imm;
        reg_set(cpu, d.rd, arcade1_read32(bus, addr));
        break;
    case ARCADE1_OP_LBU:
        addr = rs1 + imm;
        reg_set(cpu, d.rd, arcade1_read8(bus, addr));   /* zero-extends      */
        break;
    case ARCADE1_OP_LHU:
        addr = rs1 + imm;
        reg_set(cpu, d.rd, arcade1_read16(bus, addr));  /* zero-extends      */
        break;

    /* --- Stores (address = rs1 + imm) --- */
    case ARCADE1_OP_SB:
        addr = rs1 + imm;
        arcade1_write8(bus, addr, (uint8_t)rs2);
        break;
    case ARCADE1_OP_SH:
        addr = rs1 + imm;
        arcade1_write16(bus, addr, (uint16_t)rs2);
        break;
    case ARCADE1_OP_SW:
        addr = rs1 + imm;
        arcade1_write32(bus, addr, rs2);
        break;

    /* --- OP-IMM --- */
    case ARCADE1_OP_ADDI:  reg_set(cpu, d.rd, rs1 + imm); break;
    case ARCADE1_OP_SLTI:  reg_set(cpu, d.rd, (int32_t)rs1 < d.imm ? 1u : 0u); break;
    case ARCADE1_OP_SLTIU: reg_set(cpu, d.rd, rs1 < imm ? 1u : 0u); break;
    case ARCADE1_OP_XORI:  reg_set(cpu, d.rd, rs1 ^ imm); break;
    case ARCADE1_OP_ORI:   reg_set(cpu, d.rd, rs1 | imm); break;
    case ARCADE1_OP_ANDI:  reg_set(cpu, d.rd, rs1 & imm); break;
    case ARCADE1_OP_SLLI:  reg_set(cpu, d.rd, rs1 << d.rs2); break;
    case ARCADE1_OP_SRLI:  reg_set(cpu, d.rd, rs1 >> d.rs2); break;
    case ARCADE1_OP_SRAI:  reg_set(cpu, d.rd, (uint32_t)((int32_t)rs1 >> d.rs2)); break;

    /* --- OP (register-register) --- */
    case ARCADE1_OP_ADD:  reg_set(cpu, d.rd, rs1 + rs2); break;
    case ARCADE1_OP_SUB:  reg_set(cpu, d.rd, rs1 - rs2); break;
    case ARCADE1_OP_SLL:  reg_set(cpu, d.rd, rs1 << (rs2 & 0x1Fu)); break;
    case ARCADE1_OP_SLT:  reg_set(cpu, d.rd, (int32_t)rs1 < (int32_t)rs2 ? 1u : 0u); break;
    case ARCADE1_OP_SLTU: reg_set(cpu, d.rd, rs1 < rs2 ? 1u : 0u); break;
    case ARCADE1_OP_XOR:  reg_set(cpu, d.rd, rs1 ^ rs2); break;
    case ARCADE1_OP_SRL:  reg_set(cpu, d.rd, rs1 >> (rs2 & 0x1Fu)); break;
    case ARCADE1_OP_SRA:  reg_set(cpu, d.rd, (uint32_t)((int32_t)rs1 >> (rs2 & 0x1Fu))); break;
    case ARCADE1_OP_OR:   reg_set(cpu, d.rd, rs1 | rs2); break;
    case ARCADE1_OP_AND:  reg_set(cpu, d.rd, rs1 & rs2); break;

    /* --- No effect beyond advancing PC --- */
    case ARCADE1_OP_UNKNOWN:
    case ARCADE1_OP_FENCE:
    case ARCADE1_OP_ECALL:
    case ARCADE1_OP_EBREAK:
        break;
    }

    cpu->pc = next;
}
