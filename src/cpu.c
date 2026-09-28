/*
 * cpu.c - fetch, decode, execute.
 *
 * Cinoop uses a table of 256 functions, one per opcode. Here we decode the
 * opcode's *bit pattern* instead, because the instruction set is very regular:
 *
 *      7 6 | 5 4 3 | 2 1 0
 *       x  |   y   |   z          p = y >> 1,  q = y & 1
 *
 * For example every "LD r, r'" is 01 yyy zzz: x=1, destination=y, source=z,
 * and the register numbers are always:
 *
 *      0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
 *
 * So one line of code handles 49 opcodes. The full opcode map is at
 * https://gbdev.io/gb-opcodes/optables/ - keep it open while reading this.
 */
#include <stdio.h>

#include "cpu.h"
#include "registers.h"
#include "memory.h"
#include "interrupts.h"

Registers registers;
CPU cpu;

/* State right after the boot ROM finishes (we skip the Nintendo logo scroll) */
void cpuReset(void) {
    registers.af = 0x01B0;
    registers.bc = 0x0013;
    registers.de = 0x00D8;
    registers.hl = 0x014D;
    registers.sp = 0xFFFE;
    registers.pc = 0x0100;       /* cartridge entry point */
    cpu.halted = cpu.stopped = 0;
    cpu.ticks = 0;
    ime = imeDelay = 0;
}

void cpuPrintState(void) {
    printf("AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X PC=%04X  [%c%c%c%c] IME=%d  op=%02X %02X %02X\n",
           registers.af, registers.bc, registers.de, registers.hl, registers.sp, registers.pc,
           FLAG_ISSET(FLAG_Z) ? 'Z' : '-', FLAG_ISSET(FLAG_N) ? 'N' : '-',
           FLAG_ISSET(FLAG_H) ? 'H' : '-', FLAG_ISSET(FLAG_C) ? 'C' : '-', ime,
           readByte(registers.pc), readByte(registers.pc + 1), readByte(registers.pc + 2));
}

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static uint8_t  fetch8(void)  { return readByte(registers.pc++); }
static uint16_t fetch16(void) { uint16_t v = readShort(registers.pc); registers.pc += 2; return v; }

static void push(uint16_t v) { registers.sp -= 2; writeShort(registers.sp, v); }
static uint16_t pop(void)    { uint16_t v = readShort(registers.sp); registers.sp += 2; return v; }

/* 8-bit register by index: 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A */
static uint8_t getR(int i) {
    switch (i) {
    case 0: return registers.b;
    case 1: return registers.c;
    case 2: return registers.d;
    case 3: return registers.e;
    case 4: return registers.h;
    case 5: return registers.l;
    case 6: return readByte(registers.hl);
    default: return registers.a;
    }
}

static void setR(int i, uint8_t v) {
    switch (i) {
    case 0: registers.b = v; break;
    case 1: registers.c = v; break;
    case 2: registers.d = v; break;
    case 3: registers.e = v; break;
    case 4: registers.h = v; break;
    case 5: registers.l = v; break;
    case 6: writeByte(registers.hl, v); break;
    default: registers.a = v; break;
    }
}

/* 16-bit register pair by index. rp: BC DE HL SP.  rp2 (push/pop): BC DE HL AF */
static uint16_t *rp(int p)  { uint16_t *t[4] = { &registers.bc, &registers.de, &registers.hl, &registers.sp }; return t[p]; }
static uint16_t *rp2(int p) { uint16_t *t[4] = { &registers.bc, &registers.de, &registers.hl, &registers.af }; return t[p]; }

/* Branch conditions: 0=NZ 1=Z 2=NC 3=C */
static int condition(int cc) {
    switch (cc) {
    case 0: return !FLAG_ISSET(FLAG_Z);
    case 1: return  FLAG_ISSET(FLAG_Z);
    case 2: return !FLAG_ISSET(FLAG_C);
    default: return FLAG_ISSET(FLAG_C);
    }
}

/* ------------------------------------------------------------------------ */
/* Arithmetic / logic                                                        */
/* ------------------------------------------------------------------------ */

/* The 8 ALU ops share opcodes 0x80-0xBF (register) and 0xC6.. (immediate):
 * 0=ADD 1=ADC 2=SUB 3=SBC 4=AND 5=XOR 6=OR 7=CP */
static void alu(int op, uint8_t v) {
    uint8_t a = registers.a;
    int carry = FLAG_ISSET(FLAG_C) ? 1 : 0;
    int result;

    switch (op) {
    case 0: /* ADD */
    case 1: /* ADC */
        if (op == 0) carry = 0;
        result = a + v + carry;
        registers.f = 0;
        FLAG_PUT(FLAG_H, ((a & 0x0F) + (v & 0x0F) + carry) > 0x0F);
        FLAG_PUT(FLAG_C, result > 0xFF);
        break;
    case 2: /* SUB */
    case 3: /* SBC */
    case 7: /* CP = SUB that throws the result away */
        if (op != 3) carry = 0;
        result = a - v - carry;
        registers.f = FLAG_N;
        FLAG_PUT(FLAG_H, ((a & 0x0F) - (v & 0x0F) - carry) < 0);
        FLAG_PUT(FLAG_C, result < 0);
        break;
    case 4: result = a & v; registers.f = FLAG_H; break;
    case 5: result = a ^ v; registers.f = 0;      break;
    default: result = a | v; registers.f = 0;     break;
    }

    FLAG_PUT(FLAG_Z, (result & 0xFF) == 0);
    if (op != 7) registers.a = (uint8_t)result;
}

static uint8_t inc8(uint8_t v) {
    uint8_t r = v + 1;
    FLAG_PUT(FLAG_Z, r == 0);
    FLAG_CLEAR(FLAG_N);
    FLAG_PUT(FLAG_H, (v & 0x0F) == 0x0F);
    return r;                                      /* carry is unchanged! */
}

static uint8_t dec8(uint8_t v) {
    uint8_t r = v - 1;
    FLAG_PUT(FLAG_Z, r == 0);
    FLAG_SET(FLAG_N);
    FLAG_PUT(FLAG_H, (v & 0x0F) == 0x00);
    return r;
}

static void addHL(uint16_t v) {
    uint32_t r = registers.hl + v;
    FLAG_CLEAR(FLAG_N);
    FLAG_PUT(FLAG_H, ((registers.hl & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF);
    FLAG_PUT(FLAG_C, r > 0xFFFF);
    registers.hl = (uint16_t)r;
}

/* SP + signed 8-bit offset (used by ADD SP,e and LD HL,SP+e) */
static uint16_t spPlusOffset(void) {
    int8_t e = (int8_t)fetch8();
    uint16_t sp = registers.sp;
    registers.f = 0;
    FLAG_PUT(FLAG_H, ((sp & 0x0F) + (e & 0x0F)) > 0x0F);
    FLAG_PUT(FLAG_C, ((sp & 0xFF) + (e & 0xFF)) > 0xFF);
    return (uint16_t)(sp + e);
}

/* Decimal adjust: fix A after adding/subtracting two BCD numbers */
static void daa(void) {
    uint8_t a = registers.a;
    int carry = FLAG_ISSET(FLAG_C) != 0;

    if (!FLAG_ISSET(FLAG_N)) {
        if (carry || a > 0x99) { a += 0x60; carry = 1; }
        if (FLAG_ISSET(FLAG_H) || (a & 0x0F) > 0x09) a += 0x06;
    } else {
        if (carry) a -= 0x60;
        if (FLAG_ISSET(FLAG_H)) a -= 0x06;
    }
    registers.a = a;
    FLAG_PUT(FLAG_Z, a == 0);
    FLAG_CLEAR(FLAG_H);
    FLAG_PUT(FLAG_C, carry);
}

/* Rotates / shifts used by the CB-prefixed opcodes (and RLCA etc.)
 * 0=RLC 1=RRC 2=RL 3=RR 4=SLA 5=SRA 6=SWAP 7=SRL */
static uint8_t rotate(int op, uint8_t v) {
    int oldCarry = FLAG_ISSET(FLAG_C) != 0;
    int newCarry = 0;
    uint8_t r;

    switch (op) {
    case 0: newCarry = v >> 7; r = (v << 1) | newCarry;       break;
    case 1: newCarry = v & 1;  r = (v >> 1) | (newCarry << 7); break;
    case 2: newCarry = v >> 7; r = (v << 1) | oldCarry;       break;
    case 3: newCarry = v & 1;  r = (v >> 1) | (oldCarry << 7); break;
    case 4: newCarry = v >> 7; r = v << 1;                    break;
    case 5: newCarry = v & 1;  r = (v >> 1) | (v & 0x80);     break;
    case 6: r = (v << 4) | (v >> 4);                          break;
    default: newCarry = v & 1; r = v >> 1;                    break;
    }
    registers.f = 0;
    FLAG_PUT(FLAG_Z, r == 0);
    FLAG_PUT(FLAG_C, newCarry);
    return r;
}

/* ------------------------------------------------------------------------ */
/* CB prefix: 256 more opcodes for bit operations                            */
/* ------------------------------------------------------------------------ */

static int executeCB(void) {
    uint8_t op = fetch8();
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    uint8_t v = getR(z);
    int hl = (z == 6);                             /* (HL) operand is slower */

    switch (x) {
    case 0: setR(z, rotate(y, v)); break;          /* RLC..SRL r */
    case 1:                                        /* BIT y, r */
        FLAG_PUT(FLAG_Z, !(v & (1 << y)));
        FLAG_CLEAR(FLAG_N);
        FLAG_SET(FLAG_H);
        return hl ? 12 : 8;
    case 2: setR(z, v & ~(1 << y)); break;         /* RES y, r */
    case 3: setR(z, v |  (1 << y)); break;         /* SET y, r */
    }
    return hl ? 16 : 8;
}

/* ------------------------------------------------------------------------ */
/* Main decoder                                                              */
/* ------------------------------------------------------------------------ */

static int execute(uint8_t op) {
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    int p = y >> 1, q = y & 1;

    switch (x) {
    /* ================= x = 0: misc, 16-bit loads, INC/DEC ================ */
    case 0:
        switch (z) {
        case 0:
            switch (y) {
            case 0: return 4;                                         /* NOP */
            case 1: writeShort(fetch16(), registers.sp); return 20;   /* LD (nn),SP */
            case 2: fetch8(); cpu.stopped = 1; return 4;              /* STOP */
            case 3: { int8_t e = (int8_t)fetch8(); registers.pc += e; return 12; } /* JR e */
            default: {                                                /* JR cc,e */
                int8_t e = (int8_t)fetch8();
                if (condition(y - 4)) { registers.pc += e; return 12; }
                return 8;
            }
            }
        case 1:
            if (!q) { *rp(p) = fetch16(); return 12; }                /* LD rr,nn */
            addHL(*rp(p)); return 8;                                  /* ADD HL,rr */
        case 2: {
            /* LD (BC),A / LD (DE),A / LD (HL+),A / LD (HL-),A  and the reverse */
            uint16_t addr;
            switch (p) {
            case 0: addr = registers.bc; break;
            case 1: addr = registers.de; break;
            case 2: addr = registers.hl++; break;
            default: addr = registers.hl--; break;
            }
            if (!q) writeByte(addr, registers.a);
            else    registers.a = readByte(addr);
            return 8;
        }
        case 3:
            if (!q) (*rp(p))++; else (*rp(p))--;                      /* INC/DEC rr */
            return 8;
        case 4: setR(y, inc8(getR(y))); return y == 6 ? 12 : 4;       /* INC r */
        case 5: setR(y, dec8(getR(y))); return y == 6 ? 12 : 4;       /* DEC r */
        case 6: setR(y, fetch8());      return y == 6 ? 12 : 8;       /* LD r,n */
        case 7:
            switch (y) {
            case 0: case 1: case 2: case 3:                           /* RLCA RRCA RLA RRA */
                registers.a = rotate(y, registers.a);
                FLAG_CLEAR(FLAG_Z);                                   /* unlike CB versions */
                return 4;
            case 4: daa(); return 4;                                  /* DAA */
            case 5: registers.a = ~registers.a; FLAG_SET(FLAG_N | FLAG_H); return 4; /* CPL */
            case 6: FLAG_CLEAR(FLAG_N | FLAG_H); FLAG_SET(FLAG_C); return 4;         /* SCF */
            default:                                                                 /* CCF */
                FLAG_CLEAR(FLAG_N | FLAG_H);
                registers.f ^= FLAG_C;
                return 4;
            }
        }
        break;

    /* ================= x = 1: LD r,r' (and HALT) ========================= */
    case 1:
        if (y == 6 && z == 6) { cpu.halted = 1; return 4; }           /* HALT */
        setR(y, getR(z));
        return (y == 6 || z == 6) ? 8 : 4;

    /* ================= x = 2: ALU A,r ==================================== */
    case 2:
        alu(y, getR(z));
        return z == 6 ? 8 : 4;

    /* ================= x = 3: jumps, calls, stack, I/O =================== */
    case 3:
        switch (z) {
        case 0:
            switch (y) {
            case 0: case 1: case 2: case 3:                           /* RET cc */
                if (condition(y)) { registers.pc = pop(); return 20; }
                return 8;
            case 4: writeByte(0xFF00 + fetch8(), registers.a); return 12;  /* LDH (n),A */
            case 5: registers.sp = spPlusOffset(); return 16;              /* ADD SP,e */
            case 6: registers.a = readByte(0xFF00 + fetch8()); return 12;  /* LDH A,(n) */
            default: registers.hl = spPlusOffset(); return 12;             /* LD HL,SP+e */
            }
        case 1:
            if (!q) {                                                 /* POP rr */
                *rp2(p) = pop();
                registers.f &= 0xF0;              /* low 4 bits of F are always 0 */
                return 12;
            }
            switch (p) {
            case 0: registers.pc = pop(); return 16;                  /* RET */
            case 1: registers.pc = pop(); ime = 1; return 16;         /* RETI */
            case 2: registers.pc = registers.hl; return 4;            /* JP HL */
            default: registers.sp = registers.hl; return 8;           /* LD SP,HL */
            }
        case 2:
            switch (y) {
            case 0: case 1: case 2: case 3: {                         /* JP cc,nn */
                uint16_t nn = fetch16();
                if (condition(y)) { registers.pc = nn; return 16; }
                return 12;
            }
            case 4: writeByte(0xFF00 + registers.c, registers.a); return 8; /* LD (C),A */
            case 5: writeByte(fetch16(), registers.a); return 16;           /* LD (nn),A */
            case 6: registers.a = readByte(0xFF00 + registers.c); return 8; /* LD A,(C) */
            default: registers.a = readByte(fetch16()); return 16;          /* LD A,(nn) */
            }
        case 3:
            switch (y) {
            case 0: registers.pc = fetch16(); return 16;              /* JP nn */
            case 1: return executeCB();                               /* CB prefix */
            case 6: ime = 0; imeDelay = 0; return 4;                  /* DI */
            case 7: imeDelay = 2; return 4;                           /* EI (delayed) */
            }
            break;
        case 4:
            if (y < 4) {                                              /* CALL cc,nn */
                uint16_t nn = fetch16();
                if (condition(y)) { push(registers.pc); registers.pc = nn; return 24; }
                return 12;
            }
            break;
        case 5:
            if (!q) { push(*rp2(p)); return 16; }                     /* PUSH rr */
            if (p == 0) {                                             /* CALL nn */
                uint16_t nn = fetch16();
                push(registers.pc);
                registers.pc = nn;
                return 24;
            }
            break;
        case 6: alu(y, fetch8()); return 8;                           /* ALU A,n */
        case 7: push(registers.pc); registers.pc = y * 8; return 16;  /* RST */
        }
        break;
    }

    /* 0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD */
    fprintf(stderr, "Illegal opcode 0x%02X at 0x%04X - CPU locked up\n", op, registers.pc - 1);
    registers.pc--;
    cpu.halted = 1;
    return 4;
}

int cpuStep(void) {
    int cycles;

    if (cpu.halted || cpu.stopped) {
        cycles = 4;                                /* sleep, but let time pass */
    } else {
        cycles = execute(fetch8());
    }

    /* EI enables interrupts only after the instruction that follows it */
    if (imeDelay && --imeDelay == 0) ime = 1;

    cpu.ticks += cycles;
    return cycles;
}
