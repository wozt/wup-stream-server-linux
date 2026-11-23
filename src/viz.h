#pragma once
#include <gtk/gtk.h>
#include "wupstream.h"

/* Physical Wii U GamePad input monitor drawn with GTK3/Cairo.
 * pad_viz_feed() accepts fresh raw wupstream_input_t reports; passing NULL
 * shows the idle/disconnected visual. All buttons, both sticks, touch,
 * gyroscope gauges and accelerometer readings are handled in src/viz.c. */
GtkWidget *pad_viz_new(void);
void pad_viz_feed(GtkWidget *w, const wupstream_input_t *in);

/* WUP_V5_JSON_THEMES: update the entire GamePad preview without restarting. */
void pad_viz_set_palette(GtkWidget *w, const char *background, const char *shell,
                         const char *screen, const char *border,
                         const char *text, const char *muted,
                         const char *accent, const char *negative);

/* WUP_V6_JSON_LANGUAGES: localized labels in Cairo. */
void pad_viz_set_labels(GtkWidget *w, const char *const labels[6]);
