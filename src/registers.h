/*
 * registers.h - the Game Boy CPU (Sharp SM83) register file.
 *
 * The CPU has eight 8-bit registers: A F B C D E H L.
 * They can also be used in pairs as 16-bit registers: AF BC DE HL.
 * On top of that there are two pure 16-bit registers:
 *   SP - stack pointer
 *   PC - program counter (address of the next instruction)
 *
 * A union lets us access the same bytes either way:
 *   registers.b = 0x12; registers.c = 0x34;  ->  registers.bc == 0x1234
 * (This relies on the host being little-endian, which x86 and ARM are.)
 */
#ifndef REGISTERS_H
#define REGISTERS_H

#include <stdint.h>

typedef struct {
    union { struct { uint8_t f, a; }; uint16_t af; };
    union { struct { uint8_t c, b; }; uint16_t bc; };
    union { struct { uint8_t e, d; }; uint16_t de; };
    union { struct { uint8_t l, h; }; uint16_t hl; };
    uint16_t sp;
    uint16_t pc;
} Registers;

extern Registers registers;

/*
 * F is the flags register. Only the top 4 bits are used:
 *   bit 7  Z  zero        - last result was 0
 *   bit 6  N  subtract    - last operation was a subtraction
 *   bit 5  H  half carry  - carry out of bit 3 (used by DAA for BCD maths)
 *   bit 4  C  carry       - carry out of bit 7 / borrow
 */
#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

#define FLAG_ISSET(x)  (registers.f & (x))
#define FLAG_SET(x)    (registers.f |= (x))
#define FLAG_CLEAR(x)  (registers.f &= ~(x))
#define FLAG_PUT(x, v) do { if (v) FLAG_SET(x); else FLAG_CLEAR(x); } while (0)

#endif
