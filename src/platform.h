/*
 * platform.h - everything that talks to the host computer (window, keyboard,
 * clock). Keeping it separate means you could swap X11 for SDL2 by writing
 * one new file, without touching the emulator core.
 */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include "gpu.h"

typedef struct {
    int quit;        /* window closed or Esc                 */
    int paused;      /* P toggles                            */
    int turbo;       /* Tab held: run without frame limiting */
    int reset;       /* R pressed                            */
    int dumpRegs;    /* F1 pressed: print CPU registers      */
    int screenshot;  /* F12 pressed: save screenshot.ppm     */
} PlatformState;

extern PlatformState platform;

int  platformInit(int scale, const char *title);
void platformPollEvents(void);
void platformDraw(uint8_t fb[SCREEN_H][SCREEN_W]);
void platformClose(void);

#endif
