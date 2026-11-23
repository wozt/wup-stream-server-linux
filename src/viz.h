#pragma once
#include <gtk/gtk.h>
#include "wupstream.h"

/* Physical Wii U GamePad input monitor drawn with GTK3/Cairo.
 * pad_viz_feed() accepts fresh raw wupstream_input_t reports; passing NULL
 * shows the idle/disconnected visual. All buttons, both sticks, touch,
 * gyroscope gauges and accelerometer readings are handled in src/viz.c. */
GtkWidget *pad_viz_new(void);
void pad_viz_feed(GtkWidget *w, const wupstream_input_t *in);
