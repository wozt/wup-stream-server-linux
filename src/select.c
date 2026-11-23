#include "select.h"
#include "x11util.h"
#include <stdio.h>
#include <string.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xfixes.h>

/* Find a 32-bit ARGB visual for compositor transparency */
static Visual *argb_visual(Display *d, int *depth) {
    int n = 0;
    XVisualInfo *vis = XGetVisualInfo(d, 0, NULL, &n);
    Visual *v = NULL;
    for (int i = 0; i < n; i++) {
        if (vis[i].depth == 32 && vis[i].class == TrueColor &&
            vis[i].red_mask == 0xff0000) { v = vis[i].visual; *depth = 32; break; }
    }
    XFree(vis);
    return v;
}

static int desktop_bbox(Display *d, int *x, int *y, int *w, int *h) {
    Window root = DefaultRootWindow(d);
    int minx = 0, miny = 0, maxx = 0, maxy = 0, first = 1;
    if (XRRQueryExtension(d, &(int){0}, &(int){0})) {
        XRRScreenResources *res = XRRGetScreenResourcesCurrent(d, root);
        if (res) {
            for (int i = 0; i < res->ncrtc; i++) {
                XRRCrtcInfo *ci = XRRGetCrtcInfo(d, res, res->crtcs[i]);
                if (ci && ci->mode != None && ci->width) {
                    if (first) { minx = ci->x; miny = ci->y; maxx = ci->x + ci->width; maxy = ci->y + ci->height; first = 0; }
                    else {
                        if (ci->x < minx) minx = ci->x;
                        if (ci->y < miny) miny = ci->y;
                        if (ci->x + ci->width > maxx) maxx = ci->x + ci->width;
                        if (ci->y + ci->height > maxy) maxy = ci->y + ci->height;
                    }
                }
                if (ci) XRRFreeCrtcInfo(ci);
            }
            XRRFreeScreenResources(res);
        }
    }
    if (first) {
        XWindowAttributes a;
        XGetWindowAttributes(d, root, &a);
        minx = miny = 0; maxx = a.width; maxy = a.height;
    }
    *x = minx; *y = miny; *w = maxx - minx; *h = maxy - miny;
    return 1;
}

int select_region(int *ox, int *oy, int *ow, int *oh) {
    Display *d = XOpenDisplay(NULL);
    if (!d) return 0;

    int bx, by, bw, bh;
    desktop_bbox(d, &bx, &by, &bw, &bh);

    int depth = 0;
    Visual *vis = argb_visual(d, &depth);
    Colormap cmap = vis ? XCreateColormap(d, DefaultRootWindow(d), vis, AllocNone)
                        : DefaultColormap(d, DefaultScreen(d));
    XSetWindowAttributes attrs;
    memset(&attrs, 0, sizeof attrs);
    attrs.override_redirect = True;
    attrs.background_pixmap = None;
    if (vis) {
        attrs.colormap = cmap;
        attrs.background_pixel = 0;      /* fully transparent */
        attrs.border_pixel = 0;
    } else {
        attrs.background_pixel = BlackPixel(d, DefaultScreen(d));
    }
    unsigned long valuemask = vis ? (CWOverrideRedirect | CWColormap | CWBackPixel | CWBorderPixel)
                                  : (CWOverrideRedirect | CWBackPixel | CWBorderPixel);

    Window w = XCreateWindow(d, DefaultRootWindow(d), bx, by, bw, bh, 0,
                             depth ? depth : CopyFromParent,
                             vis ? vis->class : CopyFromParent,
                             vis ? vis : (Visual *)CopyFromParent,
                             valuemask, &attrs);
    if (vis) {
        /* tell the compositor the window is transparent */
        Atom net_wm_cm = XInternAtom(d, "_NET_WM_WINDOW_OPACITY", False);
        (void)net_wm_cm;
    }
    XMapWindow(d, w);
    XRaiseWindow(d, w);
    XSetInputFocus(d, w, RevertToParent, CurrentTime);
    XDefineCursor(d, w, XCreateFontCursor(d, XC_crosshair));

    GC gc = XCreateGC(d, w, 0, NULL);
    XSetForeground(d, gc, 0x80ffcc00);   /* amber, alpha 0x80 */
    XSetBackground(d, gc, 0x00000000);
    GC textgc = XCreateGC(d, w, 0, NULL);
    XSetForeground(d, textgc, 0xffffffff);

    int sx = -1, sy = -1, cx = -1, cy = -1, done = 0, ok = 0;
    XSelectInput(d, w, ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                        KeyPressMask | ExposureMask);
    XGrabPointer(d, w, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                 GrabModeAsync, GrabModeAsync, w, None, CurrentTime);

    while (!done) {
        XEvent ev;
        XNextEvent(d, &ev);
        if (ev.xany.window != w) continue;
        if (ev.type == Expose) {
            /* nothing persistent to redraw; selection redrawn on motion */
        } else if (ev.type == MotionNotify) {
            cx = ev.xmotion.x; cy = ev.xmotion.y;
            if (sx >= 0) {
                /* clear previous frame then draw new one */
                XClearArea(d, w, 0, 0, bw, bh, False);
                int x0 = sx < cx ? sx : cx, y0 = sy < cy ? sy : cy;
                int x1 = sx > cx ? sx : cx, y1 = sy > cy ? sy : cy;
                if (vis) {
                    /* translucent fill */
                    XSetForeground(d, gc, 0x4020a0ff);
                    XFillRectangle(d, w, gc, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
                    XSetForeground(d, gc, 0x80ffcc00);
                }
                XDrawRectangle(d, w, gc, x0, y0, x1 - x0, y1 - y0);
            }
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.button == Button1) {
                sx = ev.xbutton.x; sy = ev.xbutton.y;
                cx = sx; cy = sy;
            } else if (ev.xbutton.button == Button3) {
                done = 1; /* right click cancels */
            }
        } else if (ev.type == ButtonRelease) {
            if (ev.xbutton.button == Button1 && sx >= 0) {
                int x0 = sx < ev.xbutton.x ? sx : ev.xbutton.x;
                int y0 = sy < ev.xbutton.y ? sy : ev.xbutton.y;
                int x1 = sx > ev.xbutton.x ? sx : ev.xbutton.x;
                int y1 = sy > ev.xbutton.y ? sy : ev.xbutton.y;
                if (x1 - x0 >= 8 && y1 - y0 >= 8) {
                    *ox = bx + x0; *oy = by + y0;
                    *ow = x1 - x0; *oh = y1 - y0;
                    ok = 1;
                }
                done = 1;
            }
        } else if (ev.type == KeyPress) {
            if (ev.xkey.keycode == 9 /* Escape */) { done = 1; }
        }
    }

    XUngrabPointer(d, CurrentTime);
    XDestroyWindow(d, w);
    XFreeGC(d, gc); XFreeGC(d, textgc);
    if (vis) XFreeColormap(d, cmap);
    XCloseDisplay(d);
    return ok;
}
