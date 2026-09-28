/*
 * platform_x11.c - window + keyboard using plain Xlib (works on X11 and
 * under Wayland through XWayland).
 *
 * Keyboard layout:
 *   Arrow keys   D-pad
 *   Z / Space    A        (fire)
 *   X            B
 *   Enter        Start
 *   Backspace    Select
 *   Tab (hold)   fast-forward      P  pause      R  reset
 *   F1           print registers   F12 screenshot   Esc quit
 */
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include "platform.h"
#include "keys.h"

PlatformState platform;

static Display *display;
static Window   window;
static GC       gc;
static XImage  *image;
static Atom     wmDelete;
static int      scale;

/* Classic DMG green palette, shade 0 (light) .. 3 (dark) */
static const uint32_t palette[4] = { 0xE0F8D0, 0x88C070, 0x346856, 0x081820 };

int platformInit(int s, const char *title) {
    scale = s;
    display = XOpenDisplay(NULL);
    if (!display) { fprintf(stderr, "Cannot open X display\n"); return 0; }

    int screen = DefaultScreen(display);
    int w = SCREEN_W * scale, h = SCREEN_H * scale;

    window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, w, h, 0,
                                 BlackPixel(display, screen), BlackPixel(display, screen));
    XStoreName(display, window, title);

    /* fixed-size window */
    XSizeHints *hints = XAllocSizeHints();
    hints->flags = PMinSize | PMaxSize;
    hints->min_width = hints->max_width = w;
    hints->min_height = hints->max_height = h;
    XSetWMNormalHints(display, window, hints);
    XFree(hints);

    XSelectInput(display, window, KeyPressMask | KeyReleaseMask | ExposureMask | FocusChangeMask);
    wmDelete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wmDelete, 1);

    /* Without this, holding a key sends fake release/press pairs */
    XkbSetDetectableAutoRepeat(display, True, NULL);

    XMapWindow(display, window);
    gc = XCreateGC(display, window, 0, NULL);

    char *pixels = malloc((size_t)w * h * 4);
    image = XCreateImage(display, DefaultVisual(display, screen), DefaultDepth(display, screen),
                         ZPixmap, 0, pixels, w, h, 32, 0);
    return image != NULL;
}

/* Returns 1 if the key is a Game Boy button, filling *b */
static int mapKey(KeySym k, Button *b) {
    switch (k) {
    case XK_Right:     *b = BUTTON_RIGHT;  return 1;
    case XK_Left:      *b = BUTTON_LEFT;   return 1;
    case XK_Up:        *b = BUTTON_UP;     return 1;
    case XK_Down:      *b = BUTTON_DOWN;   return 1;
    case XK_z: case XK_Z:
    case XK_space:     *b = BUTTON_A;      return 1;
    case XK_x: case XK_X: *b = BUTTON_B;   return 1;
    case XK_Return:    *b = BUTTON_START;  return 1;
    case XK_BackSpace: *b = BUTTON_SELECT; return 1;
    }
    return 0;
}

void platformPollEvents(void) {
    while (XPending(display)) {
        XEvent e;
        XNextEvent(display, &e);

        switch (e.type) {
        case KeyPress:
        case KeyRelease: {
            int down = (e.type == KeyPress);
            KeySym k = XLookupKeysym(&e.xkey, 0);
            Button b;
            if (mapKey(k, &b)) {
                if (down) keyDown(b); else keyUp(b);
            } else if (k == XK_Tab) {
                platform.turbo = down;
            } else if (down) {
                switch (k) {
                case XK_Escape: platform.quit = 1; break;
                case XK_p:      platform.paused = !platform.paused; break;
                case XK_r:      platform.reset = 1; break;
                case XK_F1:     platform.dumpRegs = 1; break;
                case XK_F12:    platform.screenshot = 1; break;
                }
            }
            break;
        }
        case FocusOut:
            keysReleaseAll();              /* avoid "stuck" keys when alt-tabbing */
            platform.turbo = 0;
            break;
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == wmDelete) platform.quit = 1;
            break;
        }
    }
}

void platformDraw(uint8_t fb[SCREEN_H][SCREEN_W]) {
    /* nearest-neighbour upscale */
    for (int y = 0; y < SCREEN_H * scale; y++)
        for (int x = 0; x < SCREEN_W * scale; x++)
            XPutPixel(image, x, y, palette[fb[y / scale][x / scale]]);
    XPutImage(display, window, gc, image, 0, 0, 0, 0, SCREEN_W * scale, SCREEN_H * scale);
    XFlush(display);
}

void platformClose(void) {
    if (image) XDestroyImage(image);   /* also frees the pixel buffer */
    if (gc) XFreeGC(display, gc);
    if (display) XCloseDisplay(display);
}
