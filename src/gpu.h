/*
 * gpu.h - the picture processing unit (PPU).
 *
 * The screen is 160x144 pixels, 4 shades of grey-green. It is drawn one
 * line (scanline) at a time, and each line goes through 3 "modes":
 *
 *   mode 2  OAM scan     80 cycles   find the sprites on this line
 *   mode 3  drawing     172 cycles   push pixels to the LCD
 *   mode 0  H-Blank     204 cycles   idle until the next line
 *                       ---
 *                       456 cycles per line
 *
 * After line 143 comes mode 1 (V-Blank) for 10 more "lines" (144-153).
 * V-Blank is when games safely update VRAM/OAM. One whole frame is
 * 154 * 456 = 70224 cycles, i.e. ~59.7 frames per second.
 *
 * Graphics are made of 8x8 tiles, 2 bits per pixel (16 bytes per tile):
 *   the background is a 32x32 map of tile numbers, scrolled by SCX/SCY,
 *   the window is a second non-scrolling layer (good for a status bar),
 *   sprites (OBJ) are 40 freely-moving tiles described in OAM.
 */
#ifndef GPU_H
#define GPU_H

#include <stdint.h>

#define SCREEN_W 160
#define SCREEN_H 144
#define CYCLES_PER_FRAME 70224

/* Each pixel is a shade 0 (lightest) .. 3 (darkest) */
extern uint8_t framebuffer[SCREEN_H][SCREEN_W];

void gpuReset(void);
void gpuStep(int cycles);

#endif
