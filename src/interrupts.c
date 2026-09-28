#include "interrupts.h"
#include "memory.h"
#include "registers.h"
#include "cpu.h"

int ime = 0;
int imeDelay = 0;

void requestInterrupt(uint8_t flag) {
    io[0x0F] |= flag;
}

int pendingInterrupts(void) {
    return readByte(REG_IE) & readByte(REG_IF) & 0x1F;
}

int interruptStep(void) {
    int pending = pendingInterrupts();

    /* Any pending interrupt wakes the CPU from HALT, even with IME off */
    if (pending) cpu.halted = 0;

    if (!ime || !pending) return 0;

    for (int bit = 0; bit < 5; bit++) {
        if (pending & (1 << bit)) {
            ime = 0;                               /* no nested interrupts by default */
            io[0x0F] &= ~(1 << bit);               /* acknowledge it */
            registers.sp -= 2;
            writeShort(registers.sp, registers.pc); /* like a CALL */
            registers.pc = 0x40 + bit * 8;
            return 20;
        }
    }
    return 0;
}
