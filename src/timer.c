#include "timer.h"
#include "memory.h"
#include "interrupts.h"

/* DIV is really the top byte of a 16-bit counter that ticks every CPU cycle */
static uint16_t divCounter = 0xABCC;
static int timaCounter = 0;

uint8_t timerGetDiv(void) { return divCounter >> 8; }
void    timerResetDiv(void) { divCounter = 0; timaCounter = 0; }

void timerStep(int cycles) {
    divCounter += cycles;

    uint8_t tac = io[0x07];
    if (!(tac & 0x04)) return;                     /* timer disabled */

    static const int period[4] = { 1024, 16, 64, 256 }; /* 4096, 262144, 65536, 16384 Hz */
    timaCounter += cycles;
    while (timaCounter >= period[tac & 3]) {
        timaCounter -= period[tac & 3];
        if (io[0x05] == 0xFF) {
            io[0x05] = io[0x06];                   /* reload from TMA */
            requestInterrupt(INT_TIMER);
        } else {
            io[0x05]++;
        }
    }
}
