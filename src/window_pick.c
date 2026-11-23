#define _DEFAULT_SOURCE
/* WUP_V98_ALL_IN_ONE: modal, one-click X11 window picker.
 * Runs ONLY when invoked, in its own thread and X connection.
 * Uses EWMH stacking order, not XQueryPointer under the overlay.
 * No screenshot, polling or overlay during ordinary streaming. */
#include "window_pick.h"
#include "x11util.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned long pick_at(Display *d, const winfo_t *candidates, int count,
                             Window *stack, unsigned long count_stack,
                             int px, int py, int *x, int *y, int *w, int *h) {
    /* Last window in _NET_CLIENT_LIST_STACKING is topmost. */
    for (long z = (long)count_stack - 1; z >= 0; z--) {
        Window id = stack[z];
        for (int j = 0; j < count; j++) {
            if ((Window)candidates[j].id != id) continue;
            int wx, wy, ww, wh;
            if (!get_window_geom(d, id, &wx, &wy, &ww, &wh)) break;
            XWindowAttributes a;
            if (!XGetWindowAttributes(d, id, &a) || a.map_state != IsViewable) break;
            if (px >= wx && py >= wy && px - wx < ww && py - wy < wh) {
                *x = wx; *y = wy; *w = ww; *h = wh;
                return (unsigned long)id;
            }
            break;
        }
    }
    /* Non-EWMH WM fallback: use client list in reverse order. */
    if (!count_stack)
        for (int j = count - 1; j >= 0; j--) {
            int wx, wy, ww, wh;
            if (!get_window_geom(d, candidates[j].id, &wx, &wy, &ww, &wh)) continue;
            if (px >= wx && py >= wy && px - wx < ww && py - wy < wh) {
                *x = wx; *y = wy; *w = ww; *h = wh;
                return candidates[j].id;
            }
        }
    return 0;
}

unsigned long wup_pick_window_x11(void) {
    winfo_t *candidates = calloc(256, sizeof *candidates);
    if (!candidates) return 0;
    int n = list_windows(candidates, 256);
    if (n <= 0) { free(candidates); return 0; }
    Display *d = XOpenDisplay(NULL);
    if (!d) { free(candidates); return 0; }
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen), overlay = None;
    GC gc = NULL;
    Cursor cursor = None;
    unsigned long result = 0;
    int grabbed_pointer = 0, grabbed_keyboard = 0;
    Window *stack = NULL;
    unsigned long nstack = 0;
    XImage *shot = NULL;
    XWindowAttributes root_attr;
    if (!XGetWindowAttributes(d, root, &root_attr)) goto cleanup;
    /* Snapshot before creating the overlay, like the working region selector.
     * This is a one-shot XGetImage on clicking Pick, never per frame. */
    shot = XGetImage(d, root, 0, 0,
                     (unsigned int)root_attr.width, (unsigned int)root_attr.height,
                     AllPlanes, ZPixmap);
    if (!shot) goto cleanup;
    Atom prop = XInternAtom(d, "_NET_CLIENT_LIST_STACKING", True);
    if (prop != None) {
        Atom actual = None;
        int fmt = 0;
        unsigned long bytes_after = 0;
        if (XGetWindowProperty(d, root, prop, 0, 4096, False, XA_WINDOW,
                 &actual, &fmt, &nstack, &bytes_after,
                 (unsigned char **)&stack) != Success || actual != XA_WINDOW || fmt != 32) {
            if (stack) XFree(stack);
            stack = NULL; nstack = 0;
        }
    }
    XSetWindowAttributes attrs = {0};
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(d, screen);
    attrs.event_mask = ExposureMask | PointerMotionMask | ButtonPressMask | KeyPressMask;
    overlay = XCreateWindow(d, root, 0, 0,
              (unsigned int)root_attr.width, (unsigned int)root_attr.height,
              0, CopyFromParent, InputOutput, CopyFromParent,
              CWOverrideRedirect | CWBackPixel | CWEventMask, &attrs);
    if (!overlay) goto cleanup;
    gc = XCreateGC(d, overlay, 0, NULL);
    if (!gc) goto cleanup;
    cursor = XCreateFontCursor(d, XC_crosshair);
    XDefineCursor(d, overlay, cursor);
    XMapRaised(d, overlay);
    XSync(d, False);
    for (int j = 0; j < 12; j++) {
        if (XGrabPointer(d, overlay, False, ButtonPressMask | PointerMotionMask,
                         GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime) == GrabSuccess) {
            grabbed_pointer = 1; break;
        }
        usleep(25000);
    }
    if (!grabbed_pointer) goto cleanup;
    for (int j = 0; j < 8; j++) {
        if (XGrabKeyboard(d, overlay, False, GrabModeAsync,
                          GrabModeAsync, CurrentTime) == GrabSuccess) {
            grabbed_keyboard = 1; break;
        }
        usleep(25000);
    }
    if (!grabbed_keyboard) goto cleanup;
    XSetInputFocus(d, overlay, RevertToPointerRoot, CurrentTime);
    XColor c;
    c.flags = DoRed | DoGreen | DoBlue;
    c.red = 0x54 * 257; c.green = 0xb8 * 257; c.blue = 0xff * 257;
    unsigned long outline = XAllocColor(d, root_attr.colormap, &c) ? c.pixel : WhitePixel(d, screen);
    unsigned long previous = 0;
    int last_x = -1, last_y = -1;
    int done = 0;
    while (!done) {
        XEvent e;
        XNextEvent(d, &e);
        if (e.xany.window != overlay) continue;
        if (e.type == ButtonPress) {
            if (e.xbutton.button == Button3) break;
            if (e.xbutton.button == Button1) {
                int x,y,w,h;
                result = pick_at(d,candidates,n,stack,nstack,e.xbutton.x,e.xbutton.y,&x,&y,&w,&h);
                break;
            }
        } else if (e.type == KeyPress && XLookupKeysym(&e.xkey,0) == XK_Escape) {
            break;
        } else if (e.type == MotionNotify || e.type == Expose) {
            int px = last_x, py = last_y;
            if (e.type == MotionNotify) {
                XEvent newer;
                while (XCheckTypedWindowEvent(d,overlay,MotionNotify,&newer)) e = newer;
                px=e.xmotion.x;py=e.xmotion.y;
            }
            if (px == last_x && py == last_y && e.type == MotionNotify) continue;
            last_x=px;last_y=py;
            int x=0,y=0,w=0,h=0;
            unsigned long selected=pick_at(d,candidates,n,stack,nstack,px,py,&x,&y,&w,&h);
            if (selected == previous && e.type == MotionNotify) continue;
            previous=selected;
            XPutImage(d,overlay,gc,shot,0,0,0,0,
                      (unsigned int)root_attr.width,(unsigned int)root_attr.height);
            XSetForeground(d,gc,WhitePixel(d,screen));
            const char *hint="Click a window  |  Esc / right click: cancel";
            XDrawString(d,overlay,gc,20,25,hint,(int)strlen(hint));
            if (selected) {
                XSetForeground(d,gc,outline);
                XSetLineAttributes(d,gc,3,LineSolid,CapButt,JoinMiter);
                XDrawRectangle(d,overlay,gc,x,y,(unsigned int)(w-1),(unsigned int)(h-1));
                XSetLineAttributes(d,gc,1,LineSolid,CapButt,JoinMiter);
            }
            XFlush(d);
        }
    }
cleanup:
    if (grabbed_keyboard) XUngrabKeyboard(d,CurrentTime);
    if (grabbed_pointer) XUngrabPointer(d,CurrentTime);
    if (overlay) XDestroyWindow(d,overlay);
    if (cursor) XFreeCursor(d,cursor);
    if (gc) XFreeGC(d,gc);
    if (stack) XFree(stack);
    if (shot) XDestroyImage(shot);
    XCloseDisplay(d);
    free(candidates);
    return result;
}
