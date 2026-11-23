#pragma once
/* WUP_V94_REGION_PICKER */
/* X11 desktop screenshot selector (no screenshot saved to disk).
 * Draw a selection rectangle after a click-and-drag, then return the root
 * coordinates. Escape, right-click, too-small areas and grab failures cancel.
 * Own XOpenDisplay connection, safe to call from a worker thread. */
int select_region(int *x, int *y, int *w, int *h);
