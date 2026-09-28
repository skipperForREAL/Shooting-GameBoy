/*
 * timer.h - DIV and TIMA.
 *
 * DIV  (0xFF04) counts up at 16384 Hz, always. Writing to it resets it.
 * TIMA (0xFF05) counts at a speed chosen by TAC (0xFF07). When it overflows
 *               it is reloaded from TMA (0xFF06) and a Timer interrupt fires.
 */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void    timerStep(int cycles);
uint8_t timerGetDiv(void);
void    timerResetDiv(void);

#endif
