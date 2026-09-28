#include <string.h>

#include "gpu.h"
#include "memory.h"
#include "interrupts.h"

uint8_t framebuffer[SCREEN_H][SCREEN_W];

enum { MODE_HBLANK = 0, MODE_VBLANK = 1, MODE_OAM = 2, MODE_DRAW = 3 };

static int modeClock;
static int windowLine;   /* the window has its own line counter */
static int lcdWasOn;

#define LCDC io[0x40]
#define STAT io[0x41]
#define LY   io[0x44]

void gpuReset(void) {
    modeClock = 0;
    windowLine = 0;
    lcdWasOn = 1;
    memset(framebuffer, 0, sizeof framebuffer);
}

static int getMode(void) { return STAT & 3; }

static void setMode(int mode) {
    STAT = (STAT & ~3) | mode;

    /* STAT bits 3/4/5 let the game ask for an interrupt on entering mode 0/1/2 */
    if ((mode == MODE_HBLANK && (STAT & 0x08)) ||
        (mode == MODE_VBLANK && (STAT & 0x10)) ||
        (mode == MODE_OAM    && (STAT & 0x20)))
        requestInterrupt(INT_LCD);
}

/* LY == LYC comparison: sets STAT bit 2 and optionally an interrupt (bit 6) */
static void compareLYC(void) {
    if (LY == io[0x45]) {
        STAT |= 0x04;
        if (STAT & 0x40) requestInterrupt(INT_LCD);
    } else {
        STAT &= ~0x04;
    }
}

/*
 * Read colour index (0-3) of pixel (x,y) in a tile.
 * Each row of a tile is 2 bytes: the low bits of 8 pixels, then the high bits.
 */
static int tilePixel(uint16_t tileAddr, int x, int y) {
    uint8_t lo = vram[tileAddr + y * 2];
    uint8_t hi = vram[tileAddr + y * 2 + 1];
    int bit = 7 - x;
    return (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
}

/* BG/window tiles are either at 0x8000 (numbers 0..255) or 0x9000 (-128..127) */
static uint16_t bgTileAddr(uint8_t tileNum) {
    if (LCDC & 0x10) return tileNum * 16;                       /* 0x8000 */
    return (uint16_t)(0x1000 + (int8_t)tileNum * 16);           /* 0x9000 signed */
}

static uint8_t applyPalette(uint8_t palette, int colour) {
    return (palette >> (colour * 2)) & 3;
}

static void renderScanline(void) {
    int ly = LY;
    uint8_t bgColour[SCREEN_W];                  /* raw indices, for sprite priority */
    memset(bgColour, 0, sizeof bgColour);

    /* ---- Background ---- */
    if (LCDC & 0x01) {
        uint16_t map = (LCDC & 0x08) ? 0x1C00 : 0x1800;          /* 0x9C00 or 0x9800 */
        uint8_t y = ly + io[0x42];                               /* + SCY, wraps at 256 */
        for (int x = 0; x < SCREEN_W; x++) {
            uint8_t px = x + io[0x43];                           /* + SCX */
            uint8_t tile = vram[map + (y / 8) * 32 + (px / 8)];
            bgColour[x] = tilePixel(bgTileAddr(tile), px & 7, y & 7);
        }

        /* ---- Window (drawn over the background) ---- */
        int wy = io[0x4A], wx = io[0x4B] - 7;
        if ((LCDC & 0x20) && ly >= wy && wx < SCREEN_W) {
            uint16_t wmap = (LCDC & 0x40) ? 0x1C00 : 0x1800;
            for (int x = wx < 0 ? 0 : wx; x < SCREEN_W; x++) {
                int col = x - wx;
                uint8_t tile = vram[wmap + (windowLine / 8) * 32 + col / 8];
                bgColour[x] = tilePixel(bgTileAddr(tile), col & 7, windowLine & 7);
            }
            windowLine++;
        }
    }

    for (int x = 0; x < SCREEN_W; x++)
        framebuffer[ly][x] = applyPalette(io[0x47], bgColour[x]);

    /* ---- Sprites ---- */
    if (!(LCDC & 0x02)) return;

    int height = (LCDC & 0x04) ? 16 : 8;
    int visible[10], count = 0;

    /* Hardware limit: only the first 10 sprites (in OAM order) on a line */
    for (int i = 0; i < 40 && count < 10; i++) {
        int sy = oam[i * 4] - 16;
        if (ly >= sy && ly < sy + height) visible[count++] = i;
    }

    /* Draw lowest priority first so the winner ends up on top.
     * Priority: smaller X wins; on a tie, lower OAM index wins. */
    for (int a = 0; a < count; a++)
        for (int b = a + 1; b < count; b++) {
            int xa = oam[visible[a] * 4 + 1], xb = oam[visible[b] * 4 + 1];
            if (xa < xb || (xa == xb && visible[a] < visible[b])) {
                int t = visible[a]; visible[a] = visible[b]; visible[b] = t;
            }
        }

    for (int s = 0; s < count; s++) {
        uint8_t *spr = &oam[visible[s] * 4];
        int sy = spr[0] - 16, sx = spr[1] - 8;
        uint8_t tile = spr[2], attr = spr[3];
        if (height == 16) tile &= 0xFE;

        int row = ly - sy;
        if (attr & 0x40) row = height - 1 - row;                 /* Y flip */

        uint8_t palette = (attr & 0x10) ? io[0x49] : io[0x48];   /* OBP1 / OBP0 */

        for (int px = 0; px < 8; px++) {
            int x = sx + px;
            if (x < 0 || x >= SCREEN_W) continue;
            int col = tilePixel(tile * 16, (attr & 0x20) ? 7 - px : px, row); /* X flip */
            if (col == 0) continue;                              /* colour 0 = transparent */
            if ((attr & 0x80) && bgColour[x] != 0) continue;     /* behind background */
            framebuffer[ly][x] = applyPalette(palette, col);
        }
    }
}

void gpuStep(int cycles) {
    if (!(LCDC & 0x80)) {                         /* LCD switched off */
        if (lcdWasOn) {
            memset(framebuffer, 0, sizeof framebuffer);
            LY = 0;
            modeClock = 0;
            STAT &= ~3;
            lcdWasOn = 0;
        }
        return;
    }
    if (!lcdWasOn) {                              /* just switched back on */
        lcdWasOn = 1;
        setMode(MODE_OAM);
        compareLYC();
    }

    modeClock += cycles;

    switch (getMode()) {
    case MODE_OAM:
        if (modeClock >= 80) { modeClock -= 80; setMode(MODE_DRAW); }
        break;

    case MODE_DRAW:
        if (modeClock >= 172) {
            modeClock -= 172;
            renderScanline();
            setMode(MODE_HBLANK);
        }
        break;

    case MODE_HBLANK:
        if (modeClock >= 204) {
            modeClock -= 204;
            LY++;
            compareLYC();
            if (LY == SCREEN_H) {
                setMode(MODE_VBLANK);
                requestInterrupt(INT_VBLANK);
                windowLine = 0;
            } else {
                setMode(MODE_OAM);
            }
        }
        break;

    case MODE_VBLANK:
        if (modeClock >= 456) {
            modeClock -= 456;
            LY++;
            if (LY > 153) {
                LY = 0;
                setMode(MODE_OAM);
            }
            compareLYC();
        }
        break;
    }
}
