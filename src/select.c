#define _DEFAULT_SOURCE
/* WUP_V94_REGION_PICKER
 * X11 desktop region selector. A screenshot is kept in memory ONLY, so the
 * dimmed screenshot works with or without a compositing window manager.
 * No filesystem writes; no GTK calls from the worker thread.
 */
#include "select.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static int smaller(int a, int b) { return a < b ? a : b; }
static int larger(int a, int b) { return a > b ? a : b; }

static void make_dimmed(XImage *img) {
    /* The common Debian X11 TrueColor path: B,G,R,unused in each pixel. */
    if (img->bits_per_pixel == 32 && img->byte_order == LSBFirst &&
        img->red_mask == 0xff0000UL && img->green_mask == 0xff00UL &&
        img->blue_mask == 0xffUL) {
        for (int y = 0; y < img->height; y++) {
            unsigned char *p = (unsigned char *)img->data + (size_t)y * img->bytes_per_line;
            for (int x = 0; x < img->width; x++, p += 4) {
                p[0] = (unsigned char)((p[0] * 38) / 100);
                p[1] = (unsigned char)((p[1] * 38) / 100);
                p[2] = (unsigned char)((p[2] * 38) / 100);
            }
        }
        return;
    }
    /* Fallback for unusual X11 visuals/endianness. */
    unsigned long rm = img->red_mask, gm = img->green_mask, bm = img->blue_mask;
    unsigned long rgb = rm | gm | bm;
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            unsigned long p = XGetPixel(img, x, y), out = p & ~rgb;
            unsigned long masks[3] = {rm, gm, bm};
            for (int i = 0; i < 3; i++) {
                unsigned long mask = masks[i];
                if (!mask) continue;
                int shift = 0;
                while (!((mask >> shift) & 1UL)) shift++;
                unsigned long component = (p & mask) >> shift;
                out |= ((component * 38 / 100) << shift) & mask;
            }
            XPutPixel(img, x, y, out);
        }
    }
}

/* XPutImage clipping for a region in desktop coordinates. */
static void restore_rect(Display *d, Window win, GC gc, XImage *dim,
                         int left, int top, int width, int height) {
    if (left < 0) { width += left; left = 0; }
    if (top < 0) { height += top; top = 0; }
    if (left >= dim->width || top >= dim->height || width <= 0 || height <= 0) return;
    if (width > dim->width - left) width = dim->width - left;
    if (height > dim->height - top) height = dim->height - top;
    XPutImage(d, win, gc, dim, left, top, left, top,
              (unsigned int)width, (unsigned int)height);
}

typedef struct {
    int x0, y0, x1, y1;
    int has_selection;
} selection_t;

/* Redraw only the old highlight area; avoid retransmitting the entire desktop
 * on every mouse motion (important for large/multiple monitors). */
static void paint_selection(Display *d, Window win, GC gc, XImage *screenshot,
                            XImage *dimmed, unsigned long outline,
                            unsigned long dark, unsigned long ink,
                            selection_t *previous, int sx, int sy, int cx, int cy,
                            int full) {
    int width = screenshot->width, height = screenshot->height;
    if (full) {
        XPutImage(d, win, gc, dimmed, 0, 0, 0, 0,
                  (unsigned int)width, (unsigned int)height);
    } else if (previous->has_selection) {
        /* Also erase the previous measurements bubble, above the rectangle
         * or inside it when the selection touches the top of the desktop. */
        int label_y = previous->y0 >= 32 ? previous->y0 - 30 : previous->y0 + 5;
        int left = smaller(previous->x0 - 4, previous->x0 - 4);
        int top = smaller(previous->y0 - 5, label_y - 2);
        int right = larger(previous->x1 + 6, previous->x0 + 148);
        int bottom = larger(previous->y1 + 6, label_y + 28);
        restore_rect(d, win, gc, dimmed, left, top, right - left, bottom - top);
    }

    previous->has_selection = 0;
    if (sx >= 0 && sy >= 0) {
        int x0 = smaller(sx, cx), x1 = larger(sx, cx);
        int y0 = smaller(sy, cy), y1 = larger(sy, cy);
        x0 = larger(0, smaller(x0, width - 1));
        x1 = larger(0, smaller(x1, width - 1));
        y0 = larger(0, smaller(y0, height - 1));
        y1 = larger(0, smaller(y1, height - 1));
        int rw = x1 - x0 + 1, rh = y1 - y0 + 1;

        /* "Cut out" the dark overlay to reveal original pixels inside. */
        XPutImage(d, win, gc, screenshot, x0, y0, x0, y0,
                  (unsigned int)rw, (unsigned int)rh);
        XSetForeground(d, gc, outline);
        XSetLineAttributes(d, gc, 2, LineSolid, CapButt, JoinMiter);
        XDrawRectangle(d, win, gc, x0, y0, (unsigned int)(rw-1), (unsigned int)(rh-1));
        XSetLineAttributes(d, gc, 1, LineSolid, CapButt, JoinMiter);

        int label_y = y0 >= 32 ? y0 - 30 : y0 + 5;
        int label_x = smaller(x0 + 4, larger(0, width - 146));
        char text[64];
        snprintf(text, sizeof text, "%d x %d", rw, rh);
        XSetForeground(d, gc, dark);
        XFillRectangle(d, win, gc, label_x, label_y, 142, 25);
        XSetForeground(d, gc, outline);
        XDrawRectangle(d, win, gc, label_x, label_y, 141, 24);
        XSetForeground(d, gc, ink);
        XDrawString(d, win, gc, label_x + 9, label_y + 17,
                    text, (int)strlen(text));
        previous->x0 = x0; previous->y0 = y0;
        previous->x1 = x1; previous->y1 = y1;
        previous->has_selection = 1;
    }
    /* Desktop guidance; the selector is intentionally independent of GTK i18n.
     * No special font needed, so Xlib's built-in font works on minimal setups. */
    const char *hint = "Drag to select  |  Esc / right click: cancel";
    int hw = 342, hx = larger(0, (width - hw) / 2), hy = 15;
    XSetForeground(d, gc, dark);
    XFillRectangle(d, win, gc, hx, hy, (unsigned int)hw, 27);
    XSetForeground(d, gc, ink);
    XDrawString(d, win, gc, hx + 12, hy + 18, hint, (int)strlen(hint));
    XFlush(d);
}

int select_region(int *ox, int *oy, int *ow, int *oh) {
    if (!ox || !oy || !ow || !oh) return 0;
    Display *d = XOpenDisplay(NULL);
    if (!d) return 0;
    int result = 0;
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen), overlay = None;
    GC gc = NULL;
    Cursor crosshair = None;
    XImage *shot = NULL, *dim = NULL;
    int pointer_grabbed = 0, keyboard_grabbed = 0;
    XWindowAttributes a;
    if (!XGetWindowAttributes(d, root, &a) || a.width < 8 || a.height < 8)
        goto cleanup;

    /* The root encompasses the entire X11 virtual desktop (all monitors).
     * Coordinates returned below are therefore X11 root coordinates. */
    int width = a.width, height = a.height;
    shot = XGetImage(d, root, 0, 0, (unsigned int)width,
                     (unsigned int)height, AllPlanes, ZPixmap);
    if (!shot) goto cleanup;
    if (shot->bytes_per_line <= 0 ||
        (size_t)height > SIZE_MAX / (size_t)shot->bytes_per_line) goto cleanup;
    size_t len = (size_t)shot->bytes_per_line * (size_t)height;
    char *pixels = malloc(len);
    if (!pixels) goto cleanup;
    memcpy(pixels, shot->data, len);
    dim = XCreateImage(d, a.visual, (unsigned int)a.depth, ZPixmap, 0,
                       pixels, (unsigned int)width, (unsigned int)height,
                       shot->bitmap_pad, shot->bytes_per_line);
    if (!dim) { free(pixels); goto cleanup; }
    make_dimmed(dim);

    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(d, screen);
    attrs.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | KeyPressMask;
    overlay = XCreateWindow(d, root, 0, 0, (unsigned int)width,
                            (unsigned int)height, 0,
                            CopyFromParent, InputOutput, CopyFromParent,
                            CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);
    if (!overlay) goto cleanup;
    gc = XCreateGC(d, overlay, 0, NULL);
    if (!gc) goto cleanup;
    crosshair = XCreateFontCursor(d, XC_crosshair);
    XDefineCursor(d, overlay, crosshair);
    XMapRaised(d, overlay);
    XSync(d, False);

    /* Grab after map; GTK released its button grab before our delayed start. */
    for (int attempt = 0; attempt < 12; attempt++) {
        if (XGrabPointer(d, overlay, False,
                         ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                         GrabModeAsync, GrabModeAsync, None, crosshair,
                         CurrentTime) == GrabSuccess) {
            pointer_grabbed = 1;
            break;
        }
        usleep(25000);
    }
    if (!pointer_grabbed) goto cleanup;
    for (int attempt = 0; attempt < 8; attempt++) {
        if (XGrabKeyboard(d, overlay, False, GrabModeAsync,
                          GrabModeAsync, CurrentTime) == GrabSuccess) {
            keyboard_grabbed = 1;
            break;
        }
        usleep(25000);
    }
    if (!keyboard_grabbed) {
        /* Can't reliably handle Escape without owning the keyboard. */
        goto cleanup;
    }
    XSetInputFocus(d, overlay, RevertToPointerRoot, CurrentTime);
    XSync(d, False);
    XColor c;
    c.red = 0x54 * 257; c.green = 0xb8 * 257; c.blue = 0xff * 257;
    c.flags = DoRed | DoGreen | DoBlue;
    unsigned long outline = XAllocColor(d, a.colormap, &c) ? c.pixel : WhitePixel(d, screen);
    c.red = 0x16 * 257; c.green = 0x1c * 257; c.blue = 0x22 * 257;
    unsigned long dark = XAllocColor(d, a.colormap, &c) ? c.pixel : BlackPixel(d, screen);
    unsigned long ink = WhitePixel(d, screen);

    selection_t last = {0};
    int sx = -1, sy = -1, cx = 0, cy = 0, done = 0;
    paint_selection(d, overlay, gc, shot, dim, outline, dark, ink,
                    &last, -1, -1, 0, 0, 1);
    while (!done) {
        XEvent ev;
        XNextEvent(d, &ev);
        if (ev.xany.window != overlay) continue;
        if (ev.type == Expose) {
            /* Redraw on compositor/expose events as well as mouse motion. */
            if (ev.xexpose.count == 0) {
                last.has_selection = 0;
                paint_selection(d, overlay, gc, shot, dim, outline, dark, ink,
                                &last, sx, sy, cx, cy, 1);
            }
        } else if (ev.type == MotionNotify && sx >= 0) {
            /* Coalesce motion events: only render the newest pointer position. */
            XEvent newer;
            while (XCheckTypedWindowEvent(d, overlay, MotionNotify, &newer)) ev = newer;
            cx = ev.xmotion.x; cy = ev.xmotion.y;
            paint_selection(d, overlay, gc, shot, dim, outline, dark, ink,
                            &last, sx, sy, cx, cy, 0);
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.button == Button1) {
                sx = ev.xbutton.x; sy = ev.xbutton.y;
                cx = sx; cy = sy;
                paint_selection(d, overlay, gc, shot, dim, outline, dark, ink,
                                &last, sx, sy, cx, cy, 0);
            } else if (ev.xbutton.button == Button3) done = 1;
        } else if (ev.type == ButtonRelease && ev.xbutton.button == Button1 && sx >= 0) {
            int x0 = smaller(sx, ev.xbutton.x), y0 = smaller(sy, ev.xbutton.y);
            int x1 = larger(sx, ev.xbutton.x), y1 = larger(sy, ev.xbutton.y);
            x0 = larger(0, x0); y0 = larger(0, y0);
            x1 = smaller(width - 1, x1); y1 = smaller(height - 1, y1);
            if (x1 - x0 + 1 >= 8 && y1 - y0 + 1 >= 8) {
                *ox = x0; *oy = y0;
                *ow = x1 - x0 + 1; *oh = y1 - y0 + 1;
                result = 1;
            }
            done = 1;
        } else if (ev.type == KeyPress && XLookupKeysym(&ev.xkey, 0) == XK_Escape) {
            done = 1;
        }
    }

cleanup:
    if (keyboard_grabbed) XUngrabKeyboard(d, CurrentTime);
    if (pointer_grabbed) XUngrabPointer(d, CurrentTime);
    if (overlay) XDestroyWindow(d, overlay);
    if (crosshair) XFreeCursor(d, crosshair);
    if (gc) XFreeGC(d, gc);
    if (dim) XDestroyImage(dim);
    if (shot) XDestroyImage(shot);
    XCloseDisplay(d);
    return result;
}
