/*
 * cpu.h - the Sharp SM83 CPU (a cousin of the Z80 / Intel 8080).
 *
 * The CPU runs at 4.194304 MHz. Every step it:
 *   1. fetches the byte at PC   (the opcode)
 *   2. decodes it               (which instruction + which registers)
 *   3. executes it              (updates registers / memory / flags)
 *   4. reports how many clock cycles it took, so the GPU and timer
 *      can be advanced by the same amount of time.
 */
#ifndef CPU_H
#define CPU_H

#include <stdint.h>

typedef struct {
    int halted;              /* HALT: sleep until an interrupt is pending */
    int stopped;             /* STOP: sleep until a button is pressed     */
    unsigned long long ticks;/* total cycles executed                     */
} CPU;

extern CPU cpu;

void cpuReset(void);
int  cpuStep(void);          /* execute one instruction, return cycles */
void cpuPrintState(void);    /* dump registers - handy for debugging */

#endif
