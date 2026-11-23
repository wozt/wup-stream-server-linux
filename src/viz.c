/* pad visualization widget: feed it pad input ~30x/s, it draws itself.
   All drawing lives here so the layout can be iterated independently. */
#include "viz.h"
#include <gtk/gtk.h>
#include <cairo/cairo.h>
#include <math.h>
#include <string.h>

typedef struct {
    wupstream_input_t in;
    int has;
} viz_state_t;

#define VW 320
#define VH 180

/* live pad visualization: logical 320x180 space scaled to fit the widget,
   so nothing is ever clipped. Wii U layout: sticks bottom, dpad left,
   ABXY right, shoulders top, attitude gauge + gyro top-right. */
static void viz_circle(cairo_t *cr, double x, double y, double r,
                       int on, double dim) {
    cairo_set_source_rgba(cr, 1, 1, 1, on ? 0.9 : dim);
    cairo_arc(cr, x, y, r, 0, 2 * G_PI);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
}

static viz_state_t *viz_state(GtkWidget *w) {
    return g_object_get_data(G_OBJECT(w), "viz-state");
}

static gboolean viz_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)data;
    viz_state_t *st = viz_state(w);
    wupstream_input_t in;
    int have = st && st->has;
    if (have) in = st->in;
    GtkAllocation al;
    gtk_widget_get_allocation(w, &al);
    cairo_set_source_rgb(cr, 0.10, 0.11, 0.15);
    cairo_paint(cr);

    double sc = MIN((double)al.width / VW, (double)al.height / VH);
    if (sc <= 0) return FALSE;
    cairo_save(cr);
    cairo_translate(cr, (al.width - VW * sc) / 2, (al.height - VH * sc) / 2);
    cairo_scale(cr, sc, sc);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

    if (!have) {
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.55);
        cairo_set_font_size(cr, 12);
        cairo_move_to(cr, 12, VH / 2);
        cairo_show_text(cr, "no pad data - start the stream");
        cairo_restore(cr);
        return FALSE;
    }

    /* ---- gamepad body silhouette ---- */
    cairo_new_path(cr);
    cairo_move_to(cr, 62, 10);
    cairo_line_to(cr, 258, 10);
    cairo_curve_to(cr, 298, 10, 312, 36, 312, 66);
    cairo_line_to(cr, 312, 128);
    cairo_curve_to(cr, 312, 162, 284, 174, 246, 174);
    cairo_line_to(cr, 74, 174);
    cairo_curve_to(cr, 36, 174, 8, 162, 8, 128);
    cairo_line_to(cr, 8, 66);
    cairo_curve_to(cr, 8, 36, 22, 10, 62, 10);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 0.16, 0.17, 0.22);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    /* ---- screen area (top ~55% of the face) ---- */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
    cairo_rectangle(cr, 70, 18, 180, 68);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.2);
    cairo_rectangle(cr, 70, 18, 180, 68);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
    cairo_rectangle(cr, 78, 24, 60, 4);          /* camera dot */
    cairo_fill(cr);

    /* ---- shoulders along the top edge: ZL L | R ZR ---- */
    {
        struct { int bit; double x; const char *t; } sh[4] = {
            { WUPSTREAM_ZL, 50, "ZL" }, { WUPSTREAM_L, 86, "L" },
            { WUPSTREAM_R, 234, "R" },  { WUPSTREAM_ZR, 270, "ZR" }
        };
        for (int i = 0; i < 4; i++) {
            int on = (in.buttons & sh[i].bit) != 0;
            cairo_set_source_rgba(cr, 1, 1, 1, on ? 0.85 : 0.16);
            cairo_rectangle(cr, sh[i].x - 15, 13, 30, 10);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            cairo_set_source_rgb(cr, on ? 0.1 : 0.6, on ? 0.1 : 0.6, on ? 0.15 : 0.65);
            cairo_set_font_size(cr, 7);
            cairo_text_extents_t te;
            cairo_text_extents(cr, sh[i].t, &te);
            cairo_move_to(cr, sh[i].x - te.width / 2, 21);
            cairo_show_text(cr, sh[i].t);
        }
    }

    /* ---- sticks: L (88,130) R (186,124), r=19 ---- */
    for (int st = 0; st < 2; st++) {
        double bx = st ? 186 : 88, by = st ? 124 : 130;
        int rx = st ? in.rx : in.lx, ry = st ? in.ry : in.ly;
        cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
        cairo_arc(cr, bx, by, 20, 0, 2 * G_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
        cairo_set_line_width(cr, 1.4);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
        cairo_move_to(cr, bx - 19, by); cairo_line_to(cr, bx + 19, by);
        cairo_move_to(cr, bx, by - 19); cairo_line_to(cr, bx, by + 19);
        cairo_stroke(cr);
        double px = bx + rx / 1100.0 * 17, py = by - ry / 1100.0 * 17;
        cairo_set_source_rgb(cr, 0.25, 0.6, 0.95);
        cairo_arc(cr, px, py, 6, 0, 2 * G_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.1, 0.15, 0.25);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        int l3 = st ? (in.buttons & WUPSTREAM_R3) : (in.buttons & WUPSTREAM_L3);
        cairo_set_source_rgb(cr, l3 ? 0.3 : 0.5, l3 ? 0.75 : 0.5, l3 ? 1.0 : 0.5);
        cairo_arc(cr, bx, by - 27, 3, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.55, 0.55, 0.6);
        cairo_set_font_size(cr, 8);
        cairo_move_to(cr, bx - 12, by + 36);
        cairo_show_text(cr, st ? "R" : "L");
    }

    /* ---- dpad (left edge, small): center (38,102), arm 11 ---- */
    {
        double dx = 38, dy = 102, a = 11, w = 8;
        cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
        cairo_rectangle(cr, dx - w / 2, dy - a, w, 2 * a);
        cairo_rectangle(cr, dx - a, dy - w / 2, 2 * a, w);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
        cairo_set_line_width(cr, 1.1);
        cairo_stroke(cr);
        struct { int bit; double ox, oy; } dp[4] = {
            { WUPSTREAM_UP, 0, -1 }, { WUPSTREAM_DOWN, 0, 1 },
            { WUPSTREAM_LEFT, -1, 0 }, { WUPSTREAM_RIGHT, 1, 0 }
        };
        for (int i = 0; i < 4; i++)
            if (in.buttons & dp[i].bit) {
                cairo_set_source_rgb(cr, 0.3, 0.75, 1.0);
                cairo_arc(cr, dx + dp[i].ox * (a - 4), dy + dp[i].oy * (a - 4), 3.5, 0, 2 * G_PI);
                cairo_fill(cr);
            }
    }

    /* ---- face buttons diamond center (253,118), r=24, btn r=10 ---- */
    {
        struct { int bit; double ox, oy; const char *t; } fb[4] = {
            { WUPSTREAM_X, 0, -1, "X" }, { WUPSTREAM_Y, -1, 0, "Y" },
            { WUPSTREAM_A, 1, 0, "A" },  { WUPSTREAM_B, 0, 1, "B" }
        };
        double fx = 253, fy = 118;
        for (int i = 0; i < 4; i++) {
            double bx = fx + fb[i].ox * 24, by = fy + fb[i].oy * 24;
            int on = (in.buttons & fb[i].bit) != 0;
            viz_circle(cr, bx, by, 10, on, on ? 0.9 : 0.2);
            cairo_set_source_rgb(cr, on ? 0.1 : 0.65, on ? 0.1 : 0.65, on ? 0.15 : 0.7);
            cairo_set_font_size(cr, 9);
            cairo_text_extents_t te;
            cairo_text_extents(cr, fb[i].t, &te);
            cairo_move_to(cr, bx - te.width / 2, by + te.height / 2);
            cairo_show_text(cr, fb[i].t);
        }
    }

    /* ---- MINUS / PLUS flanking upper-center, HOME bottom-center ---- */
    {
        struct { int bit; double x, y; const char *t; double r; } sm[3] = {
            { WUPSTREAM_MINUS, 138, 94, "-", 6.5 },
            { WUPSTREAM_PLUS,  182, 94, "+", 6.5 },
            { WUPSTREAM_HOME,  160, 160, "\xe2\x97\x8f", 7 }
        };
        for (int i = 0; i < 3; i++) {
            int on = (in.buttons & sm[i].bit) != 0;
            viz_circle(cr, sm[i].x, sm[i].y, sm[i].r, on, 0.2);
            cairo_set_source_rgb(cr, on ? 0.1 : 0.7, on ? 0.1 : 0.7, on ? 0.15 : 0.75);
            cairo_set_font_size(cr, 7);
            cairo_text_extents_t te;
            cairo_text_extents(cr, sm[i].t, &te);
            cairo_move_to(cr, sm[i].x - te.width / 2, sm[i].y + 2.5);
            cairo_show_text(cr, sm[i].t);
        }
    }

    /* ---- attitude indicator top-right corner: (294,36) r=13 ---- */
    {
        double ax = in.accel[0] / 8200.0, ay = in.accel[1] / 8200.0,
               az = in.accel[2] / 8200.0;
        double roll = atan2(ax, az);
        double pitch = atan2(-ay, sqrt(ax * ax + az * az));
        double hx = 294, hy = 36, hr = 13;
        cairo_save(cr);
        cairo_arc(cr, hx, hy, hr, 0, 2 * G_PI);
        cairo_clip(cr);
        cairo_translate(cr, hx, hy);
        cairo_rotate(cr, roll);
        cairo_translate(cr, 0, pitch * hr * 0.8);
        cairo_set_source_rgb(cr, 0.25, 0.45, 0.75);
        cairo_rectangle(cr, -hr * 2, -hr * 3, hr * 4, hr * 3);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.55, 0.38, 0.2);
        cairo_rectangle(cr, -hr * 2, 0, hr * 4, hr * 3);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, -hr * 2, 0); cairo_line_to(cr, hr * 2, 0);
        cairo_stroke(cr);
        cairo_restore(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
        cairo_arc(cr, hx, hy, hr, 0, 2 * G_PI);
        cairo_set_line_width(cr, 1.1);
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 1.3);
        cairo_move_to(cr, hx - 6, hy); cairo_line_to(cr, hx + 6, hy);
        cairo_move_to(cr, hx, hy - 3); cairo_line_to(cr, hx, hy + 3);
        cairo_stroke(cr);

        /* gyro dps bars under the gauge */
        cairo_set_source_rgb(cr, 0.55, 0.55, 0.6);
        cairo_set_font_size(cr, 7);
        cairo_move_to(cr, 258, 56);
        cairo_show_text(cr, "gyro");
        for (int i = 0; i < 3; i++) {
            double gx = in.gyro[i] / 4194.0;
            double v = gx / 1000.0; if (v > 1) v = 1; if (v < -1) v = -1;
            double bx = 258 + i * 18;
            cairo_set_source_rgba(cr, 1, 1, 1, 0.2);
            cairo_rectangle(cr, bx, 59, 15, 4);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.3, 0.75, 1.0);
            double ww = 7.5 * v;
            cairo_rectangle(cr, bx + 7.5 + (ww < 0 ? ww : 0), 59,
                            ww < 0 ? -ww : ww, 4);
            cairo_fill(cr);
        }
    }

    /* ---- touch point overlay, confined to the screen rect ---- */
    if (in.touched) {
        cairo_set_source_rgb(cr, 1, 0.6, 0.2);
        cairo_arc(cr, 70 + in.tx * 180.0 / 853.0,
                       18 + in.ty * 68.0 / 479.0, 6, 0, 2 * G_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    }

    cairo_restore(cr);
    return FALSE;
}


GtkWidget *pad_viz_new(void) {
    viz_state_t *st = calloc(1, sizeof *st);
    GtkWidget *w = gtk_drawing_area_new();
    gtk_widget_set_size_request(w, 320, 175);
    g_signal_connect(w, "draw", G_CALLBACK(viz_draw), NULL);
    g_object_set_data_full(G_OBJECT(w), "viz-state", st, g_free);
    return w;
}

void pad_viz_feed(GtkWidget *w, const wupstream_input_t *in) {
    viz_state_t *st = viz_state(w);
    if (!st) return;
    if (in) { st->in = *in; st->has = 1; }
    else st->has = 0;
    gtk_widget_queue_draw(w);
}
