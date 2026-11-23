#pragma once
#include <gtk/gtk.h>
#include "wupstream.h"

/* live GamePad visualization widget: call pad_viz_feed() with the latest
   wupstream_input_t (or NULL when no pad data); it redraws itself. */
GtkWidget *pad_viz_new(void);
void pad_viz_feed(GtkWidget *w, const wupstream_input_t *in);
