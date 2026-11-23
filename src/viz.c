/* Wii U GamePad live monitor. GTK3/Cairo, deliberately self-contained.
 * The geometry follows the physical GamePad: sticks above D-pad/ABXY,
 * a central 16:9 touchscreen, shoulders above, utility keys underneath.
 * The LCD hosts three signed gyro gauges and a miniature touch monitor,
 * never an artificial horizon. No shared Cairo paths: each primitive starts a fresh path. */
#include "viz.h"
#include <gtk/gtk.h>
#include <cairo/cairo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    wupstream_input_t in;
    gboolean has;
} viz_state_t;

#define VW 440.0
#define VH 240.0
#define TAU (2.0 * G_PI)

static const double accent[3] = {0.22, 0.69, 1.00};

static viz_state_t *viz_state(GtkWidget *w) {
    return g_object_get_data(G_OBJECT(w), "viz-state");
}

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void rgba(cairo_t *cr, double r, double g, double b, double a) {
    cairo_set_source_rgba(cr, r, g, b, a);
}

static void accent_color(cairo_t *cr, double a) {
    rgba(cr, accent[0], accent[1], accent[2], a);
}

static void round_path(cairo_t *cr, double x, double y,
                       double w, double h, double r) {
    r = fmin(r, fmin(w, h) * 0.5);
    cairo_new_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, G_PI * 1.5);
    cairo_close_path(cr);
}

static void circle_path(cairo_t *cr, double x, double y, double r) {
    cairo_new_path(cr);
    cairo_arc(cr, x, y, r, 0, TAU);
    cairo_close_path(cr);
}

static void label(cairo_t *cr, const char *str, double x, double baseline,
                  double size, gboolean bold, double r, double g, double b,
                  double alpha, gboolean center) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    rgba(cr, r, g, b, alpha);
    if (center) {
        cairo_text_extents_t e;
        cairo_text_extents(cr, str, &e);
        x -= e.x_bearing + e.width / 2;
    }
    cairo_move_to(cr, x, baseline);
    cairo_show_text(cr, str);
    cairo_new_path(cr);
}

static void housing(cairo_t *cr) {
    /* Shoulder/trigger ledge appears behind the front shell. */
    round_path(cr, 22, 13, 396, 33, 16);
    rgba(cr, .075, .087, .115, 1);
    cairo_fill(cr);

    /* Soft edge shadow, then a real rounded GamePad silhouette. */
    round_path(cr, 3.5, 29, 433, 207, 56);
    rgba(cr, .015, .018, .030, .75);
    cairo_fill(cr);

    round_path(cr, 5, 25, 430, 205, 55);
    cairo_pattern_t *p = cairo_pattern_create_linear(0, 25, 0, 232);
    cairo_pattern_add_color_stop_rgb(p, 0, .255, .277, .322);
    cairo_pattern_add_color_stop_rgb(p, .4, .162, .180, .220);
    cairo_pattern_add_color_stop_rgb(p, 1, .105, .119, .155);
    cairo_set_source(cr, p);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(p);
    rgba(cr, .65, .70, .78, .46);
    cairo_set_line_width(cr, 1.65);
    cairo_stroke(cr);

    round_path(cr, 10, 30, 420, 195, 51);
    rgba(cr, .90, .93, 1, .055);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* Camera / sensor window, matching the physical bezel. */
    round_path(cr, 182, 29, 76, 17, 8);
    rgba(cr, .025, .035, .052, 1);
    cairo_fill(cr);
    circle_path(cr, 220, 37.5, 5.0);
    rgba(cr, .11, .23, .35, 1);
    cairo_fill(cr);
    circle_path(cr, 220, 37.5, 2.2);
    accent_color(cr, .6);
    cairo_fill(cr);

    /* The speakers flank the LCD; they are not interactive. */
    for (int side = 0; side < 2; side++) {
        const double x = side ? 350 : 90;
        for (int n = 0; n < 5; n++) {
            circle_path(cr, x, 102 + n * 6, 1.1);
            rgba(cr, .018, .026, .043, .72);
            cairo_fill(cr);
        }
    }
}

static void shoulder(cairo_t *cr, const char *name, double x,
                     unsigned bit, const wupstream_input_t *in) {
    gboolean on = (in->buttons & bit) != 0;
    round_path(cr, x, 11, 39, 17, 5);
    if (on) accent_color(cr, .96);
    else rgba(cr, .22, .26, .33, 1);
    cairo_fill_preserve(cr);
    if (on) rgba(cr, .71, .91, 1, .90);
    else rgba(cr, .66, .71, .79, .54);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    label(cr, name, x + 19.5, 22.7, 9, TRUE,
          on ? .045 : .81, on ? .14 : .85, on ? .22 : .92, 1, TRUE);
}

static void stick(cairo_t *cr, double x, double y,
                  int raw_x, int raw_y, gboolean pressed, const char *name) {
    /* Concentric mechanical base, with a small moving cap for direction. */
    circle_path(cr, x, y, 24);
    rgba(cr, .020, .030, .049, .96);
    cairo_fill_preserve(cr);
    if (pressed) accent_color(cr, 1);
    else rgba(cr, .55, .60, .68, .62);
    cairo_set_line_width(cr, pressed ? 2.4 : 1.4);
    cairo_stroke(cr);

    circle_path(cr, x, y, 18.6);
    rgba(cr, .19, .23, .29, 1);
    cairo_fill_preserve(cr);
    rgba(cr, .55, .62, .71, .31);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    /* Center guides: begin a new path, so no diagonal junk survives. */
    cairo_new_path(cr);
    cairo_move_to(cr, x - 13, y);
    cairo_line_to(cr, x + 13, y);
    cairo_move_to(cr, x, y - 13);
    cairo_line_to(cr, x, y + 13);
    rgba(cr, .75, .80, .87, .18);
    cairo_set_line_width(cr, .8);
    cairo_stroke(cr);

    double px = x + clampd(raw_x / 1100.0, -1, 1) * 10;
    double py = y - clampd(raw_y / 1100.0, -1, 1) * 10;
    circle_path(cr, px, py, 10.2);
    rgba(cr, .27, .32, .40, 1);
    cairo_fill_preserve(cr);
    rgba(cr, .78, .83, .92, .48);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    circle_path(cr, px, py, 5.2);
    accent_color(cr, .98);
    cairo_fill(cr);

    label(cr, name, x, y + 36.5, 9, TRUE, .81, .85, .91, .85, TRUE);
}

static void dpad(cairo_t *cr, double cx, double cy,
                 const wupstream_input_t *in) {
    struct { unsigned bit; double x, y, w, h; } arms[4] = {
        {WUPSTREAM_UP,    cx - 6, cy - 20, 12, 16},
        {WUPSTREAM_DOWN,  cx - 6, cy + 4,  12, 16},
        {WUPSTREAM_LEFT,  cx - 20, cy - 6, 16, 12},
        {WUPSTREAM_RIGHT, cx + 4,  cy - 6, 16, 12}
    };
    for (int i = 0; i < 4; i++) {
        const gboolean on = (in->buttons & arms[i].bit) != 0;
        round_path(cr, arms[i].x, arms[i].y, arms[i].w, arms[i].h, 2.6);
        if (on) accent_color(cr, .95);
        else rgba(cr, .065, .085, .12, 1);
        cairo_fill_preserve(cr);
        rgba(cr, .72, .78, .87, on ? .72 : .30);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
    }
    round_path(cr, cx - 6, cy - 6, 12, 12, 1.7);
    rgba(cr, .095, .125, .17, 1);
    cairo_fill_preserve(cr);
    rgba(cr, .65, .72, .83, .35);
    cairo_set_line_width(cr, .7);
    cairo_stroke(cr);
}

static void face_button(cairo_t *cr, double x, double y,
                        const char *t, gboolean on) {
    circle_path(cr, x, y + 1.3, 12.2);
    rgba(cr, .02, .025, .04, .75);
    cairo_fill(cr);
    circle_path(cr, x, y, 11.4);
    if (on) accent_color(cr, 1);
    else rgba(cr, .11, .14, .19, 1);
    cairo_fill_preserve(cr);
    if (on) rgba(cr, .84, .96, 1, .95);
    else rgba(cr, .68, .74, .83, .66);
    cairo_set_line_width(cr, 1.1);
    cairo_stroke(cr);
    label(cr, t, x, y + 3.8, 11, TRUE,
          on ? .035 : .91, on ? .13 : .94, on ? .20 : .97, 1, TRUE);
}

static void small_key(cairo_t *cr, double x, double y, double radius,
                      const char *t, gboolean on, double fontsize) {
    circle_path(cr, x, y, radius);
    if (on) accent_color(cr, 1);
    else rgba(cr, .085, .115, .158, 1);
    cairo_fill_preserve(cr);
    if (on) rgba(cr, .85, .97, 1, .95);
    else rgba(cr, .72, .77, .87, .55);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    label(cr, t, x, y + fontsize * .36, fontsize, TRUE,
          on ? .04 : .90, on ? .14 : .92, on ? .22 : .97, 1, TRUE);
}

static void screen_frame(cairo_t *cr) {
    /* 16:9 LCD in its own well, never under the buttons. */
    round_path(cr, 100, 49, 240, 143, 8);
    rgba(cr, .018, .027, .042, 1);
    cairo_fill_preserve(cr);
    rgba(cr, .57, .62, .70, .62);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    round_path(cr, 106, 56, 228, 128, 3.5);
    cairo_pattern_t *p = cairo_pattern_create_linear(106, 56, 334, 184);
    cairo_pattern_add_color_stop_rgb(p, 0, .065, .111, .157);
    cairo_pattern_add_color_stop_rgb(p, 1, .027, .050, .080);
    cairo_set_source(cr, p);
    cairo_fill(cr);
    cairo_pattern_destroy(p);

    /* Very subtle LCD grid, behind gauges (never connected to controls). */
    cairo_new_path(cr);
    for (int x = 122; x <= 326; x += 17) {
        cairo_move_to(cr, x, 83); cairo_line_to(cr, x, 175);
    }
    for (int y = 83; y <= 175; y += 16) {
        cairo_move_to(cr, 115, y); cairo_line_to(cr, 325, y);
    }
    rgba(cr, .5, .72, .92, .045);
    cairo_set_line_width(cr, .8);
    cairo_stroke(cr);
}

/* Three permanently visible signed gyro gauges.
 * Raw Wii U sensor units are 4194 units/dps (as in input.c). The previous
 * +/-200 dps linear scale hid ordinary wrist movements. A sqrt response
 * gives small movements useful travel while retaining +/-30 dps headroom.
 * Numbers are real, unamplified dps and remain readable past saturation. */
static void gyro_gauge(cairo_t *cr, const char *axis,
                       int raw, double baseline, gboolean live) {
    const double velocity = live ? raw / 4194.0 : 0.0;
    const double magnitude = sqrt(clampd(fabs(velocity) / 30.0, 0, 1));
    const double x = 137, width = 110, mid = x + width * .5;
    const double y = baseline - 8;
    label(cr, axis, 120, baseline, 10, TRUE, .81, .88, .96,
          live ? 1 : .67, TRUE);

    /* The full track is visible even before a stream is connected. */
    round_path(cr, x, y, width, 9, 4.0);
    rgba(cr, .15, .24, .33, 1);
    cairo_fill_preserve(cr);
    rgba(cr, .51, .70, .84, .48);
    cairo_set_line_width(cr, .8);
    cairo_stroke(cr);

    cairo_new_path(cr);
    cairo_move_to(cr, x + width * .25, y + 2);
    cairo_line_to(cr, x + width * .25, y + 7);
    cairo_move_to(cr, x + width * .75, y + 2);
    cairo_line_to(cr, x + width * .75, y + 7);
    rgba(cr, .85, .93, 1, .27);
    cairo_set_line_width(cr, .8);
    cairo_stroke(cr);

    /* A visible neutral capsule remains at zero. A small movement
       grows from the center: amber to the left, blue to the right. */
    const double amount = live ? fmax(3.5, magnitude * (width * .5 - 3)) : 3.5;
    round_path(cr, velocity < 0 ? mid - amount : mid,
               y + 1.4, amount, 6.2, 2.0);
    if (!live) rgba(cr, .52, .62, .73, .75);
    else if (velocity < 0) rgba(cr, 1, .64, .30, 1);
    else accent_color(cr, 1);
    cairo_fill(cr);

    /* Prominent zero, drawn last so that it cannot be hidden by the bar. */
    cairo_new_path(cr);
    cairo_move_to(cr, mid, y - 1.4);
    cairo_line_to(cr, mid, y + 10.4);
    rgba(cr, .95, .98, 1, .95);
    cairo_set_line_width(cr, 1.25);
    cairo_stroke(cr);

    char value[24];
    if (live) snprintf(value, sizeof value, "%+.1f", velocity);
    else snprintf(value, sizeof value, "--");
    label(cr, value, 260, baseline, 8.2, TRUE,
          velocity < 0 ? 1 : .72,
          velocity < 0 ? .79 : .89,
          velocity < 0 ? .55 : 1,
          live ? 1 : .57, TRUE);
}

static void motion_screen(cairo_t *cr, const wupstream_input_t *in,
                          gboolean have) {
    screen_frame(cr);
    if (!have) {
        /* Present gauges in idle state, not a blank display. The tracks
           are deliberately muted until actual pad packets arrive. */
        label(cr, "WAITING FOR GAMEPAD", 220, 73, 9.5, TRUE,
              .79, .88, .98, .95, TRUE);
        cairo_new_path(cr);
        cairo_move_to(cr, 114, 79);
        cairo_line_to(cr, 326, 79);
        rgba(cr, .49, .64, .78, .24);
        cairo_set_line_width(cr, .8);
        cairo_stroke(cr);

        label(cr, "GYRO (deg/s)", 120, 92, 9, TRUE,
              .71, .81, .91, .96, FALSE);
        gyro_gauge(cr, "X", 0, 109, FALSE);
        gyro_gauge(cr, "Y", 0, 132, FALSE);
        gyro_gauge(cr, "Z", 0, 155, FALSE);
        label(cr, "TOUCH", 302, 92, 9, TRUE,
              .71, .81, .91, .96, TRUE);
        round_path(cr, 278, 100, 49, 28, 2.5);
        rgba(cr, .08, .16, .24, .96);
        cairo_fill_preserve(cr);
        rgba(cr, .59, .76, .92, .60);
        cairo_set_line_width(cr, .8);
        cairo_stroke(cr);
        label(cr, "--", 302, 143, 8, FALSE,
              .58, .68, .78, 1, TRUE);
        label(cr, "Start stream to read sensors", 220, 176, 9, FALSE,
              .60, .73, .86, 1, TRUE);
        return;
    }

    circle_path(cr, 117, 70, 3);
    accent_color(cr, 1);
    cairo_fill(cr);
    label(cr, "WUP / LIVE INPUT", 126, 73, 9, TRUE,
          .78, .88, .97, .95, FALSE);

    char buf[72];
    if (in->battery >= 0 && in->battery <= 100)
        snprintf(buf, sizeof buf, "BAT %d%%", in->battery);
    else
        snprintf(buf, sizeof buf, "BAT --");
    label(cr, buf, 302, 73, 9, TRUE, .67, .80, .90, .9, TRUE);

    cairo_new_path(cr);
    cairo_move_to(cr, 114, 79);
    cairo_line_to(cr, 326, 79);
    rgba(cr, .49, .64, .78, .24);
    cairo_set_line_width(cr, .8);
    cairo_stroke(cr);

    label(cr, "GYRO (deg/s)", 120, 92, 9, TRUE,
          .71, .81, .91, .96, FALSE);
    gyro_gauge(cr, "X", in->gyro[0], 109, TRUE);
    gyro_gauge(cr, "Y", in->gyro[1], 132, TRUE);
    gyro_gauge(cr, "Z", in->gyro[2], 155, TRUE);

    /* Miniature touchscreen, independent of the gyroscope gauges.
       The GamePad touch range is 854 x 480 (coordinate 0..853, 0..479). */
    label(cr, "TOUCH", 302, 92, 9, TRUE, .71, .81, .91, .96, TRUE);
    const double sx = 278, sy = 100, sw = 49, sh = 28;
    round_path(cr, sx, sy, sw, sh, 2.5);
    rgba(cr, .08, .16, .24, .96);
    cairo_fill_preserve(cr);
    rgba(cr, .59, .76, .92, .60);
    cairo_set_line_width(cr, .8);
    cairo_stroke(cr);
    if (in->touched) {
        const double px = sx + 2 + clampd(in->tx / 853.0, 0, 1) * (sw - 4);
        const double py = sy + 2 + clampd(in->ty / 479.0, 0, 1) * (sh - 4);
        circle_path(cr, px, py, 3.3);
        rgba(cr, 1, .67, .30, 1);
        cairo_fill_preserve(cr);
        rgba(cr, 1, 1, 1, .92);
        cairo_set_line_width(cr, .8);
        cairo_stroke(cr);
        snprintf(buf, sizeof buf, "%d, %d", in->tx, in->ty);
    } else {
        snprintf(buf, sizeof buf, "no touch");
    }
    label(cr, buf, 302, 143, 8, FALSE, .80, .90, .98, .90, TRUE);

    /* Accelerometer: signed g values, without an artificial horizon. */
    label(cr, "ACCEL (g)", 278, 163, 8.3, TRUE,
          .65, .76, .87, .9, TRUE);
    snprintf(buf, sizeof buf, "X%+.1f Y%+.1f Z%+.1f",
             in->accel[0] / 8200.0,
             in->accel[1] / 8200.0,
             in->accel[2] / 8200.0);
    /* fit the accelerometer readout across the full LCD width */
    label(cr, buf, 220, 176, 9.1, FALSE,
          .69, .85, .96, .96, TRUE);
}

static gboolean viz_draw(GtkWidget *w, cairo_t *cr, gpointer unused) {
    (void)unused;
    viz_state_t *st = viz_state(w);
    wupstream_input_t in = {0};
    gboolean have = st && st->has;
    if (have) in = st->in;

    GtkAllocation al;
    gtk_widget_get_allocation(w, &al);
    rgba(cr, .064, .078, .112, 1);
    cairo_paint(cr);
    if (al.width <= 0 || al.height <= 0) return FALSE;

    double scale = fmin(al.width / VW, al.height / VH);
    cairo_save(cr);
    cairo_translate(cr, (al.width - VW * scale) / 2,
                        (al.height - VH * scale) / 2);
    cairo_scale(cr, scale, scale);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    housing(cr);
    shoulder(cr, "ZL", 30, WUPSTREAM_ZL, &in);
    shoulder(cr, "L", 75, WUPSTREAM_L, &in);
    shoulder(cr, "R", 326, WUPSTREAM_R, &in);
    shoulder(cr, "ZR", 371, WUPSTREAM_ZR, &in);

    motion_screen(cr, &in, have);
    stick(cr, 49, 74, in.lx, in.ly, (in.buttons & WUPSTREAM_L3) != 0, "L3");
    stick(cr, 391, 74, in.rx, in.ry, (in.buttons & WUPSTREAM_R3) != 0, "R3");
    dpad(cr, 49, 150, &in);

    face_button(cr, 391, 124, "X", (in.buttons & WUPSTREAM_X) != 0);
    face_button(cr, 367, 148, "Y", (in.buttons & WUPSTREAM_Y) != 0);
    face_button(cr, 415, 148, "A", (in.buttons & WUPSTREAM_A) != 0);
    face_button(cr, 391, 172, "B", (in.buttons & WUPSTREAM_B) != 0);

    label(cr, "Wii U", 120, 215, 11, TRUE, .70, .77, .85, .76, FALSE);
    small_key(cr, 220, 211, 10.5, "H", (in.buttons & WUPSTREAM_HOME) != 0, 9);
    label(cr, "HOME", 220, 229, 7.5, TRUE, .72, .78, .88, .73, TRUE);
    small_key(cr, 315, 212, 7.5, "T", (in.buttons & WUPSTREAM_TV) != 0, 8);
    label(cr, "TV", 315, 228, 7, TRUE, .72, .78, .88, .74, TRUE);
    /* Power is rendered as a physical detail but not a synthetic input. */
    small_key(cr, 342, 212, 7.5, "P", FALSE, 8);
    label(cr, "PWR", 342, 228, 7, FALSE, .62, .68, .77, .72, TRUE);
    small_key(cr, 377, 205, 8.2, "-", (in.buttons & WUPSTREAM_MINUS) != 0, 10);
    small_key(cr, 406, 205, 8.2, "+", (in.buttons & WUPSTREAM_PLUS) != 0, 10);

    cairo_restore(cr);
    return FALSE;
}

GtkWidget *pad_viz_new(void) {
    viz_state_t *st = g_new0(viz_state_t, 1);
    GtkWidget *w = gtk_drawing_area_new();
    /* WUP_V3_SMALL_VISUAL: shorter natural height, same aspect-ratio fit. */
    gtk_widget_set_size_request(w, 330, 180);
    g_object_set_data_full(G_OBJECT(w), "viz-state", st, g_free);
    g_signal_connect(w, "draw", G_CALLBACK(viz_draw), NULL);
    return w;
}

void pad_viz_feed(GtkWidget *w, const wupstream_input_t *in) {
    viz_state_t *st = viz_state(w);
    if (!st) return;
    if (in) {
        st->in = *in;
        st->has = TRUE;
    } else {
        st->has = FALSE;
        memset(&st->in, 0, sizeof st->in);
    }
    gtk_widget_queue_draw(w);
}
