/*
 * interrupts.h - hardware asking the CPU for attention.
 *
 * Each source sets a bit in IF (0xFF0F). If the same bit is set in IE (0xFFFF)
 * and the master switch IME is on, the CPU pushes PC and jumps to a fixed
 * address (the "vector"). Lowest bit wins when several are pending.
 *
 *   bit 0  V-Blank  -> 0x40   (a frame has finished drawing)
 *   bit 1  LCD STAT -> 0x48   (LY == LYC, or a PPU mode change)
 *   bit 2  Timer    -> 0x50   (TIMA overflowed)
 *   bit 3  Serial   -> 0x58
 *   bit 4  Joypad   -> 0x60   (a button was pressed)
 */
#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

#define INT_VBLANK (1 << 0)
#define INT_LCD    (1 << 1)
#define INT_TIMER  (1 << 2)
#define INT_SERIAL (1 << 3)
#define INT_JOYPAD (1 << 4)

extern int ime;       /* Interrupt Master Enable (set by EI, cleared by DI) */
extern int imeDelay;  /* EI takes effect after the *next* instruction */

void requestInterrupt(uint8_t flag);
int  pendingInterrupts(void);   /* IE & IF */
int  interruptStep(void);       /* service one interrupt; returns cycles used */

#endif
