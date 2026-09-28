#include "keys.h"
#include "interrupts.h"
#include "cpu.h"

/* 1 = released, 0 = pressed (same polarity as the hardware) */
static uint8_t directions = 0x0F;
static uint8_t buttons    = 0x0F;

void keyDown(Button b) {
    if (b <= BUTTON_DOWN) directions &= ~(1 << b);
    else                  buttons    &= ~(1 << (b - BUTTON_A));
    requestInterrupt(INT_JOYPAD);
    cpu.stopped = 0;                 /* a button press wakes the CPU from STOP */
}

void keyUp(Button b) {
    if (b <= BUTTON_DOWN) directions |= 1 << b;
    else                  buttons    |= 1 << (b - BUTTON_A);
}

void keysReleaseAll(void) {
    directions = buttons = 0x0F;
}

uint8_t keysRead(uint8_t joyp) {
    uint8_t result = 0x0F;
    if (!(joyp & 0x10)) result &= directions;
    if (!(joyp & 0x20)) result &= buttons;
    return 0xC0 | (joyp & 0x30) | result;
}
