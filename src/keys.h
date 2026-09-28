/*
 * keys.h - the joypad.
 *
 * The 8 buttons are read through one register, 0xFF00, in two groups of 4.
 * The game writes bit 4 or 5 to 0 to pick a group, then reads bits 0-3.
 * A pressed button reads as 0 (active low!).
 *
 *              bit 3   bit 2   bit 1   bit 0
 *   bit 4=0:   Down    Up      Left    Right     (directions)
 *   bit 5=0:   Start   Select  B       A         (buttons)
 */
#ifndef KEYS_H
#define KEYS_H

#include <stdint.h>

typedef enum {
    BUTTON_RIGHT, BUTTON_LEFT, BUTTON_UP, BUTTON_DOWN,
    BUTTON_A, BUTTON_B, BUTTON_SELECT, BUTTON_START
} Button;

void    keyDown(Button b);
void    keyUp(Button b);
void    keysReleaseAll(void);
uint8_t keysRead(uint8_t joyp);

#endif
