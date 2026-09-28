/*
 * main.c - the emulator's heartbeat.
 *
 * Everything is driven by the CPU: execute one instruction, see how many
 * cycles it took, then let the timer and GPU catch up by that many cycles.
 * After 70224 cycles a whole frame has been drawn, so we show it and sleep
 * until it's time for the next one (~59.7 times a second).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cpu.h"
#include "memory.h"
#include "interrupts.h"
#include "timer.h"
#include "gpu.h"
#include "keys.h"
#include "platform.h"

#define FRAME_NS 16742706L   /* 1e9 / 59.7275 */

static int trace = 0;

static void reset(void) {
    resetMemory();
    cpuReset();
    gpuReset();
}

static void runFrame(void) {
    int frameCycles = 0;
    while (frameCycles < CYCLES_PER_FRAME) {
        if (trace) cpuPrintState();
        int cycles = cpuStep();
        cycles += interruptStep();
        timerStep(cycles);
        gpuStep(cycles);
        frameCycles += cycles;
    }
}

static void saveScreenshot(const char *path) {
    static const uint8_t shades[4][3] = { {224,248,208}, {136,192,112}, {52,104,86}, {8,24,32} };
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            fwrite(shades[framebuffer[y][x]], 1, 3, f);
    fclose(f);
    printf("Saved %s\n", path);
}

/*
 * Scripted input for headless runs, e.g. "30:start 40: 60:left,a 120:"
 * means: from frame 30 hold Start, from 40 release everything, ...
 */
static void applyScriptedInput(const char *script, int frame) {
    const char *p = script;
    while (p && *p) {
        int at = atoi(p);
        const char *colon = strchr(p, ':');
        if (!colon) return;
        const char *end = strchr(colon, ' ');
        if (!end) end = colon + strlen(colon);

        if (at == frame) {
            keysReleaseAll();
            static const char *names[] = { "right", "left", "up", "down", "a", "b", "select", "start" };
            char buf[128];
            snprintf(buf, sizeof buf, "%.*s", (int)(end - colon - 1), colon + 1);
            for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ","))
                for (int b = 0; b < 8; b++)
                    if (!strcmp(tok, names[b])) keyDown((Button)b);
        }
        p = *end ? end + 1 : end;
    }
}

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s ROM.gb [options]\n"
        "  --scale N          window size multiplier (default 4)\n"
        "  --serial           print serial port output (for test ROMs)\n"
        "  --trace            print CPU state before every instruction (very slow!)\n"
        "  --headless FRAMES  run without a window for FRAMES frames\n"
        "  --screenshot FILE  save the last frame as a PPM image (headless)\n"
        "  --input SCRIPT     scripted buttons for headless runs, e.g. \"30:start 40: 60:left,a\"\n",
        prog);
}

int main(int argc, char **argv) {
    const char *romPath = NULL, *shotPath = NULL, *script = NULL;
    int scale = 4, headlessFrames = 0;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--scale") && i + 1 < argc)      scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--serial"))                      serialToStdout = 1;
        else if (!strcmp(argv[i], "--trace"))                       trace = 1;
        else if (!strcmp(argv[i], "--headless") && i + 1 < argc)   headlessFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shotPath = argv[++i];
        else if (!strcmp(argv[i], "--input") && i + 1 < argc)      script = argv[++i];
        else if (argv[i][0] != '-')                                 romPath = argv[i];
        else { usage(argv[0]); return 1; }
    }
    if (!romPath) { usage(argv[0]); return 1; }
    if (scale < 1) scale = 1;

    if (!loadROM(romPath)) return 1;
    printCartInfo();
    reset();

    /* ---- Headless mode: no window, useful for testing ---- */
    if (headlessFrames > 0) {
        for (int frame = 0; frame < headlessFrames; frame++) {
            applyScriptedInput(script, frame);
            runFrame();
        }
        if (shotPath) saveScreenshot(shotPath);
        freeMemory();
        return 0;
    }

    /* ---- Normal mode ---- */
    if (!platformInit(scale, "Game Boy")) return 1;
    printf("Controls: arrows=D-pad  Z/Space=A  X=B  Enter=Start  Backspace=Select\n"
           "          Tab=fast-forward  P=pause  R=reset  F1=registers  F12=screenshot  Esc=quit\n");

    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);

    while (!platform.quit) {
        platformPollEvents();

        if (platform.reset)      { reset(); platform.reset = 0; }
        if (platform.dumpRegs)   { cpuPrintState(); platform.dumpRegs = 0; }
        if (platform.screenshot) { saveScreenshot("screenshot.ppm"); platform.screenshot = 0; }

        if (!platform.paused) runFrame();
        platformDraw(framebuffer);

        /* Frame limiter: sleep until the next 1/59.7 s tick */
        if (platform.turbo) {
            clock_gettime(CLOCK_MONOTONIC, &next);
            continue;
        }
        next.tv_nsec += FRAME_NS;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > next.tv_sec + 1) next = now;   /* fell far behind, don't try to catch up */
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }

    platformClose();
    freeMemory();
    return 0;
}
