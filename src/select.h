#pragma once
/* Blocking region picker: darkens nothing, draws a translucent rubber band
   over the whole X desktop (all monitors, negative coords included).
   Returns 1 and fills rect on ButtonRelease, 0 on Escape/error.
   Safe to call from a worker thread (own Display connection). */
int select_region(int *x, int *y, int *w, int *h);
