#define _GNU_SOURCE
#include "ui.h"
#include "x11util.h"
#include "select.h"
#include "input.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

struct ui_t {
    settings_t *s;
    server_t *srv;
    GtkWidget *window;
    GtkStatusIcon *tray;
    GtkWidget *status_lbl;
    GtkWidget *stream_btn;
    /* source */
    GtkWidget *src_combo, *scr_row, *screen_combo, *win_row, *win_combo;
    GtkListStore *win_store;
    GtkWidget *region_box, *region_lbl;
    /* quality */
    GtkWidget *preset_combo, *crf_spin, *kbps_spin, *gop_spin, *lowlat_chk;
    GtkWidget *src_frame, *qual_frame;
    /* input */
    GtkWidget *mode_combo, *dz_spin, *sens_spin, *invert_chk;
    GtkWidget *gyro_combo, *gyro_sens_spin, *touch_chk, *rstick_chk;
    GtkWidget *key_entries[18];
    GtkWidget *stick_entries[8];
    GtkWidget *viz_area, *black_chk;
    /* audio */
    GtkWidget *aout_chk, *ain_chk, *monitor_combo;
    /* pad */
    GtkWidget *ip_entry, *autostart_chk;
    guint save_id;
};

/* ---------- helpers ---------- */

static gboolean save_cb(gpointer p) {
    ui_t *u = p;
    settings_save(u->s);
    u->save_id = 0;
    return G_SOURCE_REMOVE;
}

static void schedule_save(ui_t *u) {
    if (u->save_id) g_source_remove(u->save_id);
    u->save_id = g_timeout_add(800, save_cb, u);
}

static void commit_capture(ui_t *u) { schedule_save(u); server_restart_capture(u->srv); }
static void commit_reconnect(ui_t *u) { schedule_save(u); server_request_reconnect(u->srv); }
static void commit_live(ui_t *u) { schedule_save(u); }

static GtkWidget *mk_label(const char *t) {
    GtkWidget *l = gtk_label_new(t);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    return l;
}

static GtkWidget *mk_spin(int min, int max, int val) {
    GtkAdjustment *a = gtk_adjustment_new(val, min, max, 1, 10, 0);
    return gtk_spin_button_new(a, 1, 0);
}

/* section frame with title */
static GtkWidget *mk_section(const char *title) {
    GtkWidget *f = gtk_frame_new(NULL);
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vb), 4);
    gtk_frame_set_label_widget(GTK_FRAME(f), mk_label(title));
    gtk_container_add(GTK_CONTAINER(f), vb);
    return vb;
}

static GtkWidget *frame_of(GtkWidget *vb) { return gtk_widget_get_parent(vb); }

static void grid_row(GtkWidget *grid, int *r, const char *label, GtkWidget *w) {
    gtk_grid_attach(GTK_GRID(grid), mk_label(label), 0, (*r)++, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), w, 1, *r - 1, 1, 1);
}

/* ---------- source ---------- */

static void refresh_screens(ui_t *u) {
    screen_t buf[16];
    int n = list_screens(buf, 16);
    GtkComboBoxText *cb = GTK_COMBO_BOX_TEXT(u->screen_combo);
    gtk_combo_box_text_remove_all(cb);
    for (int i = 0; i < n; i++) {
        char label[160];
        snprintf(label, sizeof label, "%s — %dx%d @%d,%d%s", buf[i].name,
                 buf[i].w, buf[i].h, buf[i].x, buf[i].y,
                 buf[i].primary ? " (primary)" : "");
        gtk_combo_box_text_append(cb, buf[i].name, label);
    }
    /* restore saved selection */
    int found = -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(buf[i].name, u->s->source.output)) found = i;
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb), found >= 0 ? found : 0);
}

static void refresh_windows(ui_t *u) {
    winfo_t buf[256];
    int n = list_windows(buf, 256);
    gtk_list_store_clear(u->win_store);
    for (int i = 0; i < n; i++) {
        char label[512];
        const char *t = buf[i].title[0] ? buf[i].title : "(untitled)";
        snprintf(label, sizeof label, "%.45s — %s (%dx%d)", t,
                 buf[i].wm_class[0] ? buf[i].wm_class : "?", buf[i].w, buf[i].h);
        GtkTreeIter it;
        gtk_list_store_append(u->win_store, &it);
        gtk_list_store_set(u->win_store, &it, 0, (guint64)buf[i].id, 1, label, -1);
    }
    /* restore saved window */
    GtkTreeModel *m = gtk_combo_box_get_model(GTK_COMBO_BOX(u->win_combo));
    GtkTreeIter it;
    gboolean valid = gtk_tree_model_get_iter_first(m, &it);
    while (valid) {
        guint64 id;
        gtk_tree_model_get(m, &it, 0, &id, -1);
        if ((unsigned long)id == u->s->source.window_id) {
            gtk_combo_box_set_active_iter(GTK_COMBO_BOX(u->win_combo), &it);
            return;
        }
        valid = gtk_tree_model_iter_next(m, &it);
    }
    if (gtk_tree_model_get_iter_first(m, &it))
        gtk_combo_box_set_active_iter(GTK_COMBO_BOX(u->win_combo), &it);
}

static void on_screen_changed(GtkComboBox *cb, ui_t *u) {
    const char *id = gtk_combo_box_get_active_id(cb);
    if (id) snprintf(u->s->source.output, sizeof u->s->source.output, "%s", id);
    commit_capture(u);
}

static void on_win_changed(GtkComboBox *cb, ui_t *u) {
    GtkTreeIter it;
    if (gtk_combo_box_get_active_iter(cb, &it)) {
        guint64 id;
        gtk_tree_model_get(GTK_TREE_MODEL(u->win_store), &it, 0, &id, -1);
        u->s->source.window_id = (unsigned long)id;
        commit_capture(u);
    }
}

static void on_refresh_windows(GtkButton *b, ui_t *u) {
    (void)b;
    refresh_windows(u);
}

/* region selection */
typedef struct { ui_t *u; int x, y, w, h; } region_res_t;

static gboolean region_done_cb(gpointer p) {
    region_res_t *r = p;
    ui_t *u = r->u;
    u->s->source.x = r->x;
    u->s->source.y = r->y;
    u->s->source.w = r->w;
    u->s->source.h = r->h;
    char lbl[128];
    snprintf(lbl, sizeof lbl, "Region: %d,%d %dx%d", r->x, r->y, r->w, r->h);
    gtk_label_set_text(GTK_LABEL(u->region_lbl), lbl);
    commit_capture(u);
    g_free(r);
    return G_SOURCE_REMOVE;
}

static void *region_thread(void *arg) {
    region_res_t *r = arg;
    r->w = r->h = 0;
    if (select_region(&r->x, &r->y, &r->w, &r->h))
        g_idle_add(region_done_cb, r);
    else
        g_free(r);
    return NULL;
}

static void on_region_clicked(GtkButton *b, ui_t *u) {
    (void)b;
    region_res_t *r = g_new0(region_res_t, 1);
    r->u = u;
    g_thread_new("region-select", region_thread, r);
}

static void on_src_type_changed(GtkComboBox *cb, ui_t *u) {
    u->s->source_type = gtk_combo_box_get_active(cb);
    gtk_widget_set_sensitive(u->scr_row, u->s->source_type == SRC_SCREEN);
    gtk_widget_set_sensitive(u->win_row, u->s->source_type == SRC_WINDOW);
    gtk_widget_set_sensitive(u->region_box, u->s->source_type == SRC_REGION);
    commit_capture(u);
}

static void on_aspect_changed(GtkComboBox *cb, ui_t *u) {
    u->s->aspect = gtk_combo_box_get_active(cb);
    commit_capture(u);
}

static GtkWidget *build_source_section(ui_t *u) {
    GtkWidget *sec = mk_section("Video Source");
    int r = 0;
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(sec), grid, FALSE, FALSE, 0);

    u->src_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(u->src_combo, FALSE);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->src_combo), "Full screen");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->src_combo), "Window (application)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->src_combo), "Screen region");
    gtk_combo_box_set_active(GTK_COMBO_BOX(u->src_combo), u->s->source_type);
    g_signal_connect(u->src_combo, "changed", G_CALLBACK(on_src_type_changed), u);
    grid_row(grid, &r, "Stream:", u->src_combo);

    /* monitor row: label + combo hidden together */
    u->scr_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(u->scr_row), mk_label("Monitor:"), FALSE, FALSE, 0);
    u->screen_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(u->screen_combo, FALSE);
    refresh_screens(u);
    g_signal_connect(u->screen_combo, "changed", G_CALLBACK(on_screen_changed), u);
    gtk_box_pack_start(GTK_BOX(u->scr_row), u->screen_combo, TRUE, TRUE, 0);
    gtk_grid_attach(GTK_GRID(grid), u->scr_row, 0, r++, 2, 1);

    /* window row: label + combo + refresh, hidden together */
    u->win_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(u->win_row), mk_label("Window:"), FALSE, FALSE, 0);
    u->win_store = gtk_list_store_new(2, G_TYPE_UINT64, G_TYPE_STRING);
    u->win_combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(u->win_store));
    gtk_widget_set_hexpand(u->win_combo, FALSE);
    GtkCellRenderer *cr = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(u->win_combo), cr, TRUE);
    gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(u->win_combo), cr, "text", 1);
    refresh_windows(u);
    g_signal_connect(u->win_combo, "changed", G_CALLBACK(on_win_changed), u);
    gtk_box_pack_start(GTK_BOX(u->win_row), u->win_combo, TRUE, TRUE, 0);
    GtkWidget *wbtn = gtk_button_new_with_label("R");
    g_signal_connect(wbtn, "clicked", G_CALLBACK(on_refresh_windows), u);
    gtk_box_pack_start(GTK_BOX(u->win_row), wbtn, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), u->win_row, 0, r++, 2, 1);

    /* region row: label + button + info, hidden together */
    u->region_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(u->region_box), mk_label("Region:"), FALSE, FALSE, 0);
    GtkWidget *rbtn = gtk_button_new_with_label("Select...");
    g_signal_connect(rbtn, "clicked", G_CALLBACK(on_region_clicked), u);
    u->region_lbl = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(u->region_lbl), 0);
    if (u->s->source.w > 0)
        gtk_label_set_text(GTK_LABEL(u->region_lbl), "region set (click to change)");
    gtk_box_pack_start(GTK_BOX(u->region_box), rbtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(u->region_box), u->region_lbl, TRUE, TRUE, 0);
    gtk_grid_attach(GTK_GRID(grid), u->region_box, 0, r++, 2, 1);

    /* aspect */
    GtkWidget *aspect = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(aspect, FALSE);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(aspect), "Letterbox (black bars)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(aspect), "Crop (fill)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(aspect), "Stretch (distort)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(aspect), u->s->aspect);
    g_signal_connect(aspect, "changed", G_CALLBACK(on_aspect_changed), u);
    grid_row(grid, &r, "854x480 fit:", aspect);

    gtk_widget_set_sensitive(u->scr_row, u->s->source_type == SRC_SCREEN);
    gtk_widget_set_sensitive(u->win_row, u->s->source_type == SRC_WINDOW);
    gtk_widget_set_sensitive(u->region_box, u->s->source_type == SRC_REGION);
    return frame_of(sec);
}

/* ---------- quality ---------- */

static void refresh_presets(ui_t *u) {
    GtkComboBoxText *cb = GTK_COMBO_BOX_TEXT(u->preset_combo);
    gtk_combo_box_text_remove_all(cb);
    for (int i = 0; i < u->s->npresets; i++)
        gtk_combo_box_text_append_text(cb, u->s->presets[i].name);
    for (int i = 0; i < u->s->npresets; i++)
        if (!strcmp(u->s->presets[i].name, u->s->preset_name))
            gtk_combo_box_set_active(GTK_COMBO_BOX(cb), i);
}

static void on_preset_changed(GtkComboBox *cb, ui_t *u) {
    char *name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(cb));
    if (!name) return;
    snprintf(u->s->preset_name, sizeof u->s->preset_name, "%s", name);
    for (int i = 0; i < u->s->npresets; i++)
        if (!strcmp(u->s->presets[i].name, name)) {
            u->s->crf = u->s->presets[i].crf;
            u->s->kbps = u->s->presets[i].kbps;
            u->s->gop = u->s->presets[i].gop;
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->crf_spin), u->s->crf);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->kbps_spin), u->s->kbps);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(u->gop_spin), u->s->gop);
            break;
        }
    g_free(name);
    commit_reconnect(u);
}

static void on_quality_spin(GtkSpinButton *sp, ui_t *u) {
    (void)sp;
    u->s->crf = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->crf_spin));
    u->s->kbps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->kbps_spin));
    u->s->gop = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->gop_spin));
    u->s->custom_quality = 1;
    commit_reconnect(u);
}

static void on_lowlat_toggled(GtkToggleButton *tb, ui_t *u) {
    u->s->low_latency = gtk_toggle_button_get_active(tb);
    commit_reconnect(u);
}

static void on_save_preset(GtkButton *b, ui_t *u) {
    (void)b;
    if (u->s->npresets >= 8) return;
    quality_preset_t *p = &u->s->presets[u->s->npresets++];
    snprintf(p->name, sizeof p->name, "Custom %d", u->s->npresets);
    p->crf = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->crf_spin));
    p->kbps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->kbps_spin));
    p->gop = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->gop_spin));
    refresh_presets(u);
    commit_live(u);
}

static GtkWidget *build_quality_section(ui_t *u) {
    GtkWidget *sec = mk_section("Quality");
    int r = 0;
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(sec), grid, FALSE, FALSE, 0);

    u->preset_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(u->preset_combo, FALSE);
    refresh_presets(u);
    g_signal_connect(u->preset_combo, "changed", G_CALLBACK(on_preset_changed), u);
    grid_row(grid, &r, "Preset:", u->preset_combo);

    u->crf_spin = mk_spin(0, 51, u->s->crf);
    g_signal_connect(u->crf_spin, "value-changed", G_CALLBACK(on_quality_spin), u);
    grid_row(grid, &r, "CRF (lower = better):", u->crf_spin);

    u->kbps_spin = mk_spin(0, 50000, u->s->kbps);
    g_signal_connect(u->kbps_spin, "value-changed", G_CALLBACK(on_quality_spin), u);
    grid_row(grid, &r, "Max bitrate (kbps, 0=auto):", u->kbps_spin);

    u->gop_spin = mk_spin(0, 600, u->s->gop);
    g_signal_connect(u->gop_spin, "value-changed", G_CALLBACK(on_quality_spin), u);
    grid_row(grid, &r, "Keyframe interval:", u->gop_spin);

    u->lowlat_chk = gtk_check_button_new_with_label(
        "Low latency (x264 zerolatency: no lookahead, no B-frames)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->lowlat_chk), u->s->low_latency);
    g_signal_connect(u->lowlat_chk, "toggled", G_CALLBACK(on_lowlat_toggled), u);
    gtk_box_pack_start(GTK_BOX(sec), u->lowlat_chk, FALSE, FALSE, 0);

    GtkWidget *sbtn = gtk_button_new_with_label("Save current values as preset");
    g_signal_connect(sbtn, "clicked", G_CALLBACK(on_save_preset), u);
    gtk_box_pack_start(GTK_BOX(sec), sbtn, FALSE, FALSE, 0);

    return frame_of(sec);
}

/* ---------- inputs ---------- */

static void on_input_spin(GtkSpinButton *sp, ui_t *u) {
    (void)sp;
    u->s->input.deadzone = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->dz_spin));
    u->s->input.sensitivity = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->sens_spin));
    u->s->input.gyro_sens = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(u->gyro_sens_spin));
    commit_live(u);
}

static void on_input_toggled(GtkToggleButton *tb, ui_t *u) {
    (void)tb;
    u->s->input.invert_y = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(u->invert_chk));
    u->s->input.touch_as_mouse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(u->touch_chk));
    u->s->input.right_stick_mouse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(u->rstick_chk));
    commit_live(u);
}

static void on_mode_changed(GtkComboBox *cb, ui_t *u) {
    u->s->input.mode = gtk_combo_box_get_active(cb);
    gtk_widget_set_sensitive(u->rstick_chk, u->s->input.mode == INMODE_KBMOUSE);
    commit_reconnect(u);
}

static void on_gyro_changed(GtkComboBox *cb, ui_t *u) {
    u->s->input.gyro_mode = gtk_combo_box_get_active(cb);
    commit_live(u);
}

/* button index -> icon file (data/icons), NULL = text label */
static const char *btn_icon_files[18] = {
    "btn_a.png", "btn_b.png", "btn_x.png", "btn_y.png",
    "btn_left.png", "btn_right.png", "btn_up.png", "btn_down.png",
    "btn_zl.png", "btn_zr.png", "btn_l.png", "btn_r.png",
    "btn_plus.png", "btn_minus.png", NULL,      /* HOME: no icon */
    "btn_l3.png", "btn_r3.png", NULL            /* TV: no icon */
};

static GtkWidget *btn_icon_widget(int idx) {
    const char *f = btn_icon_files[idx];
    if (!f) return mk_label(wbtn_names[idx]);
    char path[700], exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return mk_label(wbtn_names[idx]);
    exe[n] = 0;
    char *slash = strrchr(exe, '/');
    if (slash) *slash = 0;
    snprintf(path, sizeof path, "%s/../data/icons/%s", exe, f);
    GError *e = NULL;
    GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(path, 20, 20, TRUE, &e);
    if (e) g_error_free(e);
    if (!pb) return mk_label(wbtn_names[idx]);
    GtkWidget *img = gtk_image_new_from_pixbuf(pb);
    g_object_unref(pb);
    gtk_widget_set_tooltip_text(img, wbtn_names[idx]);
    return img;
}

static const char *stick_dir_names[8] = {
    "L\u2190", "L\u2192", "L\u2191", "L\u2193", "R\u2190", "R\u2192", "R\u2191", "R\u2193"
};

static void on_stick_key_edited(GtkEntry *e, ui_t *u) {
    for (int i = 0; i < 8; i++)
        if (u->stick_entries[i] == GTK_WIDGET(e)) {
            snprintf(u->s->input.stick_keys[i], 32, "%s", gtk_entry_get_text(e));
            server_apply_input_keys(u->srv);
            schedule_save(u);
            return;
        }
}

static void on_calib_clicked(GtkButton *b, ui_t *u) {
    (void)b;
    server_calib_gyro(u->srv);
}

static void on_key_edited(GtkEntry *e, ui_t *u) {
    for (int i = 0; i < 17; i++)   /* TV (17) is not user-mappable */
        if (u->key_entries[i] == GTK_WIDGET(e)) {
            snprintf(u->s->input.keys[i], 32, "%s", gtk_entry_get_text(e));
            server_apply_input_keys(u->srv);
            schedule_save(u);
            return;
        }
}

/* live pad visualization: logical 320x180 space scaled to fit the widget,
   so nothing is ever clipped. Wii U layout: sticks bottom, dpad left,
   ABXY right, shoulders top, attitude gauge + gyro top-right. */
#define VW 320
#define VH 180

static void viz_circle(cairo_t *cr, double x, double y, double r,
                       int on, double dim) {
    cairo_set_source_rgba(cr, 1, 1, 1, on ? 0.9 : dim);
    cairo_arc(cr, x, y, r, 0, 2 * G_PI);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
}

static gboolean viz_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    ui_t *u = data;
    GtkAllocation al;
    gtk_widget_get_allocation(w, &al);
    wupstream_input_t in;
    cairo_set_source_rgb(cr, 0.10, 0.11, 0.15);
    cairo_paint(cr);

    double sc = MIN((double)al.width / VW, (double)al.height / VH);
    if (sc <= 0) return FALSE;
    cairo_save(cr);
    cairo_translate(cr, (al.width - VW * sc) / 2, (al.height - VH * sc) / 2);
    cairo_scale(cr, sc, sc);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

    if (!server_input_snapshot(u->srv, &in)) {
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

    /* screen hint: the pad's own display area, top center */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
    cairo_rectangle(cr, 96, 22, 128, 40);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
    cairo_rectangle(cr, 96, 22, 128, 40);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    /* ---- shoulders along the top edge: ZL L | R ZR ---- */
    {
        struct { int bit; double x; const char *t; } sh[4] = {
            { WUPSTREAM_ZL, 52, "ZL" }, { WUPSTREAM_L, 88, "L" },
            { WUPSTREAM_R, 232, "R" },  { WUPSTREAM_ZR, 268, "ZR" }
        };
        for (int i = 0; i < 4; i++) {
            int on = (in.buttons & sh[i].bit) != 0;
            cairo_set_source_rgba(cr, 1, 1, 1, on ? 0.85 : 0.16);
            cairo_rectangle(cr, sh[i].x - 15, 13, 30, 11);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
            cairo_set_source_rgb(cr, on ? 0.1 : 0.6, on ? 0.1 : 0.6, on ? 0.15 : 0.65);
            cairo_set_font_size(cr, 7);
            cairo_text_extents_t te;
            cairo_text_extents(cr, sh[i].t, &te);
            cairo_move_to(cr, sh[i].x - te.width / 2, 21.5);
            cairo_show_text(cr, sh[i].t);
        }
    }

    /* ---- sticks: L (66,120) R (168,132), r=22 ---- */
    for (int st = 0; st < 2; st++) {
        double bx = st ? 168 : 66, by = st ? 132 : 120;
        int rx = st ? in.rx : in.lx, ry = st ? in.ry : in.ly;
        cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
        cairo_arc(cr, bx, by, 23, 0, 2 * G_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
        cairo_set_line_width(cr, 1.4);
        cairo_stroke(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
        cairo_move_to(cr, bx - 22, by); cairo_line_to(cr, bx + 22, by);
        cairo_move_to(cr, bx, by - 22); cairo_line_to(cr, bx, by + 22);
        cairo_stroke(cr);
        double px = bx + rx / 1100.0 * 20, py = by - ry / 1100.0 * 20;
        cairo_set_source_rgb(cr, 0.25, 0.6, 0.95);
        cairo_arc(cr, px, py, 6, 0, 2 * G_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 0.1, 0.15, 0.25);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        int l3 = st ? (in.buttons & WUPSTREAM_R3) : (in.buttons & WUPSTREAM_L3);
        cairo_set_source_rgb(cr, l3 ? 0.3 : 0.5, l3 ? 0.75 : 0.5, l3 ? 1.0 : 0.5);
        cairo_arc(cr, bx, by - 30, 3, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.55, 0.55, 0.6);
        cairo_set_font_size(cr, 8);
        cairo_move_to(cr, bx - 12, by + 38);
        cairo_show_text(cr, st ? "R" : "L");
    }

    /* ---- dpad cross at (120,58), arm 13 ---- */
    {
        double dx = 120, dy = 58, a = 13, w = 9;
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
                cairo_arc(cr, dx + dp[i].ox * (a - 4), dy + dp[i].oy * (a - 4), 4, 0, 2 * G_PI);
                cairo_fill(cr);
            }
    }

    /* ---- face buttons diamond center (252,108), r=26, btn r=10 ---- */
    {
        struct { int bit; double ox, oy; const char *t; } fb[4] = {
            { WUPSTREAM_X, 0, -1, "X" }, { WUPSTREAM_Y, -1, 0, "Y" },
            { WUPSTREAM_A, 1, 0, "A" },  { WUPSTREAM_B, 0, 1, "B" }
        };
        double fx = 252, fy = 108;
        for (int i = 0; i < 4; i++) {
            double bx = fx + fb[i].ox * 26, by = fy + fb[i].oy * 26;
            int on = (in.buttons & fb[i].bit) != 0;
            viz_circle(cr, bx, by, 10.5, on, on ? 0.9 : 0.2);
            cairo_set_source_rgb(cr, on ? 0.1 : 0.65, on ? 0.1 : 0.65, on ? 0.15 : 0.7);
            cairo_set_font_size(cr, 9);
            cairo_text_extents_t te;
            cairo_text_extents(cr, fb[i].t, &te);
            cairo_move_to(cr, bx - te.width / 2, by + te.height / 2);
            cairo_show_text(cr, fb[i].t);
        }
    }

    /* ---- center buttons: MINUS HOME PLUS + TV row at y=70, x~200-260 ---- */
    {
        struct { int bit; double x; const char *t; } sm[3] = {
            { WUPSTREAM_MINUS, 196, "-" }, { WUPSTREAM_HOME, 214, "\xe2\x97\x8f" },
            { WUPSTREAM_PLUS, 232, "+" }
        };
        for (int i = 0; i < 3; i++) {
            int on = (in.buttons & sm[i].bit) != 0;
            viz_circle(cr, sm[i].x, 70, 6.5, on, 0.2);
            cairo_set_source_rgb(cr, on ? 0.1 : 0.7, on ? 0.1 : 0.7, on ? 0.15 : 0.75);
            cairo_set_font_size(cr, 7);
            cairo_text_extents_t te;
            cairo_text_extents(cr, sm[i].t, &te);
            cairo_move_to(cr, sm[i].x - te.width / 2, 72.5);
            cairo_show_text(cr, sm[i].t);
        }
    }

    /* ---- attitude indicator bottom-right inside body: (272,140) r=16 ---- */
    {
        double ax = in.accel[0] / 8200.0, ay = in.accel[1] / 8200.0,
               az = in.accel[2] / 8200.0;
        double roll = atan2(ax, az);
        double pitch = atan2(-ay, sqrt(ax * ax + az * az));
        double hx = 272, hy = 140, hr = 16;
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
        cairo_set_line_width(cr, 1.2);
        cairo_move_to(cr, -hr * 2, 0); cairo_line_to(cr, hr * 2, 0);
        cairo_stroke(cr);
        cairo_restore(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
        cairo_arc(cr, hx, hy, hr, 0, 2 * G_PI);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, hx - 7, hy); cairo_line_to(cr, hx + 7, hy);
        cairo_move_to(cr, hx, hy - 3.5); cairo_line_to(cr, hx, hy + 3.5);
        cairo_stroke(cr);

        /* gyro dps bars under the gauge */
        cairo_set_source_rgb(cr, 0.55, 0.55, 0.6);
        cairo_set_font_size(cr, 7);
        cairo_move_to(cr, 226, 162);
        cairo_show_text(cr, "gyro");
        for (int i = 0; i < 3; i++) {
            double gx = in.gyro[i] / 4194.0;
            double v = gx / 1000.0; if (v > 1) v = 1; if (v < -1) v = -1;
            double bx = 226 + i * 22;
            cairo_set_source_rgba(cr, 1, 1, 1, 0.2);
            cairo_rectangle(cr, bx, 165, 18, 4);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.3, 0.75, 1.0);
            double ww = 9 * v;
            cairo_rectangle(cr, bx + 9 + (ww < 0 ? ww : 0), 165,
                            ww < 0 ? -ww : ww, 4);
            cairo_fill(cr);
        }
    }

    /* ---- touch point overlay (pad screen coords mapped onto the body) ---- */
    if (in.touched) {
        cairo_set_source_rgb(cr, 1, 0.6, 0.2);
        cairo_arc(cr, 30 + in.tx * (VW - 60) / 853.0,
                       20 + in.ty * (VH - 40) / 479.0, 6, 0, 2 * G_PI);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
    }

    cairo_restore(cr);
    return FALSE;
}

static GtkWidget *build_input_section(ui_t *u) {
    GtkWidget *sec = mk_section("Inputs");

    GtkWidget *mrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(mrow), mk_label("Injection mode:"), FALSE, FALSE, 0);
    u->mode_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(u->mode_combo, FALSE);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->mode_combo), "Virtual gamepad (evdev)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->mode_combo), "Keyboard + mouse (X11)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(u->mode_combo), u->s->input.mode);
    g_signal_connect(u->mode_combo, "changed", G_CALLBACK(on_mode_changed), u);
    gtk_box_pack_start(GTK_BOX(mrow), u->mode_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sec), mrow, FALSE, FALSE, 0);

    /* 3 columns */
    GtkWidget *cols = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(sec), cols, FALSE, FALSE, 0);

    /* gamepad column */
    GtkWidget *gp = mk_section("Gamepad (evdev)");
    gtk_box_pack_start(GTK_BOX(cols), frame_of(gp), TRUE, TRUE, 0);
    {
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
        gtk_box_pack_start(GTK_BOX(gp), grid, FALSE, FALSE, 0);
        int r = 0;
        u->dz_spin = mk_spin(0, 500, u->s->input.deadzone);
        g_signal_connect(u->dz_spin, "value-changed", G_CALLBACK(on_input_spin), u);
        grid_row(grid, &r, "Stick deadzone:", u->dz_spin);
        u->invert_chk = gtk_check_button_new_with_label("Invert Y axis");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->invert_chk), u->s->input.invert_y);
        g_signal_connect(u->invert_chk, "toggled", G_CALLBACK(on_input_toggled), u);
        gtk_box_pack_start(GTK_BOX(gp), u->invert_chk, FALSE, FALSE, 0);
        u->viz_area = gtk_drawing_area_new();
        gtk_widget_set_size_request(u->viz_area, 320, 175);
        g_signal_connect(u->viz_area, "draw", G_CALLBACK(viz_draw), u);
        gtk_box_pack_start(GTK_BOX(gp), u->viz_area, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(gp),
            mk_label("Live pad state (buttons, sticks, gyro)"),
            FALSE, FALSE, 0);
    }

    /* kb/mouse column */
    GtkWidget *kb = mk_section("Keyboard + mouse");
    gtk_box_pack_start(GTK_BOX(cols), frame_of(kb), TRUE, TRUE, 0);
    {
        u->rstick_chk = gtk_check_button_new_with_label("Right stick = mouse");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->rstick_chk), u->s->input.right_stick_mouse);
        g_signal_connect(u->rstick_chk, "toggled", G_CALLBACK(on_input_toggled), u);
        gtk_box_pack_start(GTK_BOX(kb), u->rstick_chk, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(kb), mk_label("Button -> key mapping:"), FALSE, FALSE, 0);
        GtkWidget *kmid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(kb), kmid, FALSE, FALSE, 0);
        GtkWidget *kgrid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(kgrid), 1);
        gtk_grid_set_column_spacing(GTK_GRID(kgrid), 6);
        for (int i = 0; i < 17; i++) {   /* TV excluded: reserved for screen switching */
            int col = (i / 9) * 2, row = i % 9;
            gtk_grid_attach(GTK_GRID(kgrid), btn_icon_widget(i), col, row, 1, 1);
            u->key_entries[i] = gtk_entry_new();
            gtk_entry_set_text(GTK_ENTRY(u->key_entries[i]), u->s->input.keys[i]);
            gtk_entry_set_width_chars(GTK_ENTRY(u->key_entries[i]), 8);
            g_signal_connect(u->key_entries[i], "changed", G_CALLBACK(on_key_edited), u);
            gtk_grid_attach(GTK_GRID(kgrid), u->key_entries[i], col + 1, row, 1, 1);
        }
        gtk_box_pack_start(GTK_BOX(kmid), kgrid, FALSE, FALSE, 0);
        /* third sub-column: stick directions -> keys */
        GtkWidget *sbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_pack_start(GTK_BOX(kmid), sbox, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(sbox), mk_label("Stick -> key:"), FALSE, FALSE, 0);
        GtkWidget *sgrid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(sgrid), 1);
        gtk_grid_set_column_spacing(GTK_GRID(sgrid), 4);
        for (int i = 0; i < 8; i++) {
            gtk_grid_attach(GTK_GRID(sgrid), mk_label(stick_dir_names[i]), 0, i, 1, 1);
            u->stick_entries[i] = gtk_entry_new();
            gtk_entry_set_text(GTK_ENTRY(u->stick_entries[i]), u->s->input.stick_keys[i]);
            gtk_entry_set_width_chars(GTK_ENTRY(u->stick_entries[i]), 6);
            g_signal_connect(u->stick_entries[i], "changed", G_CALLBACK(on_stick_key_edited), u);
            gtk_grid_attach(GTK_GRID(sgrid), u->stick_entries[i], 1, i, 1, 1);
        }
        gtk_box_pack_start(GTK_BOX(sbox), sgrid, FALSE, FALSE, 0);
    }

    /* shared column */
    GtkWidget *sh = mk_section("Shared (touch + gyro)");
    gtk_box_pack_start(GTK_BOX(cols), frame_of(sh), TRUE, TRUE, 0);
    {
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
        gtk_box_pack_start(GTK_BOX(sh), grid, FALSE, FALSE, 0);
        int r = 0;

        u->sens_spin = mk_spin(10, 400, u->s->input.sensitivity);
        g_signal_connect(u->sens_spin, "value-changed", G_CALLBACK(on_input_spin), u);
        grid_row(grid, &r, "Mouse/stick sens %:", u->sens_spin);

        u->touch_chk = gtk_check_button_new_with_label("Touch = mouse (works in gamepad mode too)");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->touch_chk), u->s->input.touch_as_mouse);
        g_signal_connect(u->touch_chk, "toggled", G_CALLBACK(on_input_toggled), u);
        gtk_box_pack_start(GTK_BOX(sh), u->touch_chk, FALSE, FALSE, 0);

        u->gyro_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(u->gyro_combo, FALSE);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->gyro_combo), "Off");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->gyro_combo), "Mouse");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->gyro_combo), "Right stick");
        gtk_combo_box_set_active(GTK_COMBO_BOX(u->gyro_combo), u->s->input.gyro_mode);
        g_signal_connect(u->gyro_combo, "changed", G_CALLBACK(on_gyro_changed), u);
        grid_row(grid, &r, "Gyroscope:", u->gyro_combo);

        u->gyro_sens_spin = mk_spin(10, 500, u->s->input.gyro_sens);
        g_signal_connect(u->gyro_sens_spin, "value-changed", G_CALLBACK(on_input_spin), u);
        grid_row(grid, &r, "Gyro sens %:", u->gyro_sens_spin);

        GtkWidget *cal = gtk_button_new_with_label("Calibrate gyroscope");
        g_signal_connect(cal, "clicked", G_CALLBACK(on_calib_clicked), u);
        gtk_box_pack_start(GTK_BOX(sh), cal, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(sh),
            mk_label("Keep the pad flat and still for ~3 s\nwhile streaming."),
            FALSE, FALSE, 0);
    }

    return frame_of(sec);
}

/* ---------- audio ---------- */

static void refresh_monitors(ui_t *u) {
    GtkComboBoxText *cb = GTK_COMBO_BOX_TEXT(u->monitor_combo);
    gtk_combo_box_text_remove_all(cb);
    gtk_combo_box_text_append(cb, "", "Default (default sink monitor)");
    FILE *p = popen("pactl list short sources 2>/dev/null", "r");
    if (p) {
        char line[512];
        while (fgets(line, sizeof line, p)) {
            char *t1 = strchr(line, '\t');
            if (!t1) continue;
            char *t2 = strchr(t1 + 1, '\t');
            char name[256] = {0}, desc[256] = {0};
            *t1 = 0;
            if (t2) { *t2 = 0; snprintf(desc, sizeof desc, "%s", t2 + 1); }
            snprintf(name, sizeof name, "%s", t1 + 1);
            desc[strcspn(desc, "\n")] = 0;
            name[strcspn(name, "\n")] = 0;
            char label[256];
            const char *d = desc[0] ? desc : name;
            snprintf(label, sizeof label, "%.60s", d);
            gtk_combo_box_text_append(cb, name, label);
        }
        pclose(p);
    }
    GtkTreeModel *m = gtk_combo_box_get_model(GTK_COMBO_BOX(cb));
    GtkTreeIter it;
    gboolean valid = gtk_tree_model_get_iter_first(m, &it);
    int found = 0;
    while (valid) {
        char *id = NULL;
        gtk_tree_model_get(m, &it, 0, &id, -1);
        if (id && !strcmp(id, u->s->audio_monitor)) {
            gtk_combo_box_set_active_iter(GTK_COMBO_BOX(cb), &it);
            g_free(id);
            found = 1;
            break;
        }
        g_free(id);
        valid = gtk_tree_model_iter_next(m, &it);
    }
    if (!found) gtk_combo_box_set_active(GTK_COMBO_BOX(cb), 0);
}

static void on_monitor_changed(GtkComboBox *cb, ui_t *u) {
    const char *id = gtk_combo_box_get_active_id(cb);
    snprintf(u->s->audio_monitor, sizeof u->s->audio_monitor, "%s", id ? id : "");
    commit_reconnect(u);
}

static void on_audio_toggled(GtkToggleButton *tb, ui_t *u) {
    (void)tb;
    u->s->audio_pc_to_pad = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(u->aout_chk));
    u->s->audio_pad_to_pc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(u->ain_chk));
    commit_reconnect(u);
}

static void on_refresh_monitors(GtkButton *b, ui_t *u) {
    (void)b;
    refresh_monitors(u);
}

static GtkWidget *build_audio_section(ui_t *u) {
    GtkWidget *sec = mk_section("Audio");

    u->aout_chk = gtk_check_button_new_with_label("Send PC audio to GamePad");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->aout_chk), u->s->audio_pc_to_pad);
    g_signal_connect(u->aout_chk, "toggled", G_CALLBACK(on_audio_toggled), u);
    gtk_box_pack_start(GTK_BOX(sec), u->aout_chk, FALSE, FALSE, 0);

    u->ain_chk = gtk_check_button_new_with_label("Send GamePad microphone to PC");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->ain_chk), u->s->audio_pad_to_pc);
    g_signal_connect(u->ain_chk, "toggled", G_CALLBACK(on_audio_toggled), u);
    gtk_box_pack_start(GTK_BOX(sec), u->ain_chk, FALSE, FALSE, 0);

    GtkWidget *mrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(mrow), mk_label("PC audio source:"), FALSE, FALSE, 0);
    u->monitor_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(u->monitor_combo, FALSE);
    refresh_monitors(u);
    g_signal_connect(u->monitor_combo, "changed", G_CALLBACK(on_monitor_changed), u);
    gtk_box_pack_start(GTK_BOX(mrow), u->monitor_combo, TRUE, TRUE, 0);
    GtkWidget *mbtn = gtk_button_new_with_label("R");
    g_signal_connect(mbtn, "clicked", G_CALLBACK(on_refresh_monitors), u);
    gtk_box_pack_start(GTK_BOX(mrow), mbtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sec), mrow, FALSE, FALSE, 0);

    return frame_of(sec);
}

/* ---------- connection / pad ---------- */

static void on_ip_changed(GtkEntry *e, ui_t *u) {
    snprintf(u->s->pad_ip, sizeof u->s->pad_ip, "%s", gtk_entry_get_text(e));
    commit_reconnect(u);
}

static void on_autostart_toggled(GtkToggleButton *tb, ui_t *u) {
    u->s->autostart_stream = gtk_toggle_button_get_active(tb);
    commit_live(u);
}

static void on_black_toggled(GtkToggleButton *tb, ui_t *u) {
    u->s->gamepad_only_black = gtk_toggle_button_get_active(tb);
    commit_live(u);
}

static void on_gamepad_only_toggled(GtkToggleButton *tb, ui_t *u) {
    u->s->gamepad_only = gtk_toggle_button_get_active(tb);
    gtk_widget_set_sensitive(u->src_frame, !u->s->gamepad_only);
    gtk_widget_set_sensitive(u->qual_frame, !u->s->gamepad_only);
    if (u->black_chk) gtk_widget_set_sensitive(u->black_chk, u->s->gamepad_only);
    commit_live(u);
}

static void on_stream_clicked(GtkButton *b, ui_t *u) {
    (void)b;
    int st = server_state(u->srv);
    if (st == SRV_STREAMING || st == SRV_WAITING_PAD)
        server_request_stop(u->srv);
    else
        server_request_stream(u->srv);
    ui_refresh_status(u);
}

static GtkWidget *build_conn_section(ui_t *u) {
    GtkWidget *sec = mk_section("GamePad Connection");
    int r = 0;
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(sec), grid, FALSE, FALSE, 0);

    GtkWidget *irow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    u->ip_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(u->ip_entry), u->s->pad_ip);
    gtk_entry_set_width_chars(GTK_ENTRY(u->ip_entry), 16);
    gtk_entry_set_placeholder_text(GTK_ENTRY(u->ip_entry), "auto");
    g_signal_connect(u->ip_entry, "changed", G_CALLBACK(on_ip_changed), u);
    gtk_box_pack_start(GTK_BOX(irow), u->ip_entry, FALSE, FALSE, 0);
    grid_row(grid, &r, "Pad IP:", irow);

    u->autostart_chk = gtk_check_button_new_with_label("Start streaming automatically");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->autostart_chk), u->s->autostart_stream);
    g_signal_connect(u->autostart_chk, "toggled", G_CALLBACK(on_autostart_toggled), u);
    gtk_box_pack_start(GTK_BOX(sec), u->autostart_chk, FALSE, FALSE, 0);

    GtkWidget *gponly_chk = gtk_check_button_new_with_label(
        "Gamepad-only mode (no capture, inputs stay live)");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gponly_chk), u->s->gamepad_only);
    g_signal_connect(gponly_chk, "toggled", G_CALLBACK(on_gamepad_only_toggled), u);
    gtk_box_pack_start(GTK_BOX(sec), gponly_chk, FALSE, FALSE, 0);
    u->black_chk = gtk_check_button_new_with_label("Send black screen instead of test image");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->black_chk), u->s->gamepad_only_black);
    g_signal_connect(u->black_chk, "toggled", G_CALLBACK(on_black_toggled), u);
    gtk_box_pack_start(GTK_BOX(sec), u->black_chk, FALSE, FALSE, 0);
    gtk_widget_set_sensitive(u->black_chk, u->s->gamepad_only);
    gtk_box_pack_start(GTK_BOX(sec),
        mk_label("Useful to calibrate the gyroscope or test inputs\nwithout streaming your screen."),
        FALSE, FALSE, 0);

    return frame_of(sec);
}

/* ---------- tray / window ---------- */

static void on_open_settings(GtkMenuItem *i, ui_t *u) {
    (void)i;
    gtk_widget_show_all(u->window);
    gtk_window_present(GTK_WINDOW(u->window));
}

static void on_quit(GtkMenuItem *i, gpointer data) {
    (void)i; (void)data;
    gtk_main_quit();
}

static void on_tray_popup(GtkStatusIcon *icon, guint button, guint time, gpointer data) {
    ui_t *u = data;
    (void)icon; (void)button; (void)time;
    GtkWidget *m = gtk_menu_new();
    GtkWidget *o = gtk_menu_item_new_with_label("Open settings");
    g_signal_connect(o, "activate", G_CALLBACK(on_open_settings), u);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), o);
    GtkWidget *s = gtk_menu_item_new_with_label("Start/stop stream");
    g_signal_connect(s, "activate", G_CALLBACK(on_stream_clicked), u);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), s);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new());
    GtkWidget *q = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(q, "activate", G_CALLBACK(on_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(m), q);
    gtk_widget_show_all(m);
    gtk_menu_popup(GTK_MENU(m), NULL, NULL,
                   gtk_status_icon_position_menu, icon, button, time);
}

static GdkPixbuf *load_icon(void) {
    const char *paths[] = {
        getenv("WUPSTREAMD_ICON"),
        "/usr/share/icons/hicolor/scalable/apps/wupstreamd.svg",
        "/usr/share/pixmaps/wupstreamd.svg",
        "/usr/share/pixmaps/wupstreamd.png",
        "/usr/share/pixmaps/wupstreamd.svg",
        "/usr/share/pixmaps/wupstreamd.png",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        GError *e = NULL;
        GdkPixbuf *pb = gdk_pixbuf_new_from_file(paths[i], &e);
        if (pb) return pb;
        if (e) g_error_free(e);
    }
    return NULL;
}

static gboolean status_tick(gpointer p) {
    ui_refresh_status(p);
    return G_SOURCE_CONTINUE;
}

static gboolean viz_tick(gpointer p) {
    ui_t *u = p;
    if (u->viz_area) gtk_widget_queue_draw(u->viz_area);
    return G_SOURCE_CONTINUE;
}

void ui_refresh_status(ui_t *u) {
    if (!u->status_lbl) return;
    char buf[512];
    int st = server_state(u->srv);
    snprintf(buf, sizeof buf, "Status: %s", server_status(u->srv));
    gtk_label_set_text(GTK_LABEL(u->status_lbl), buf);
    if (u->stream_btn)
        gtk_button_set_label(GTK_BUTTON(u->stream_btn),
            (st == SRV_STREAMING || st == SRV_WAITING_PAD) ? "Stop stream" : "Start stream");
}

ui_t *ui_new(settings_t *s, server_t *srv) {
    ui_t *u = g_new0(ui_t, 1);
    u->s = s; u->srv = srv;

    u->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(u->window), "wupstreamd — settings");
    {
        GdkPixbuf *ic = load_icon();
        if (ic) { gtk_window_set_icon(GTK_WINDOW(u->window), ic); }
    }
    gtk_window_set_default_size(GTK_WINDOW(u->window), 980, 800);
    g_signal_connect(u->window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    gtk_widget_set_size_request(u->window, 940, 660);

    /* single scrollable page: status bar + sections */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(u->window), scroll);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(page), 6);
    gtk_container_add(GTK_CONTAINER(scroll), page);

    /* top bar: status + stream button */
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(page), top, FALSE, FALSE, 0);
    u->status_lbl = gtk_label_new("Status: ...");
    gtk_label_set_xalign(GTK_LABEL(u->status_lbl), 0);
    gtk_box_pack_start(GTK_BOX(top), u->status_lbl, TRUE, TRUE, 0);
    u->stream_btn = gtk_button_new_with_label("Start stream");
    g_signal_connect(u->stream_btn, "clicked", G_CALLBACK(on_stream_clicked), u);
    gtk_box_pack_start(GTK_BOX(top), u->stream_btn, FALSE, FALSE, 0);

    /* compact styling: tighter frames and labels */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "* { font-size: 11px; }"
        "frame { margin: 1px; padding: 3px; }"
        "frame > label { padding: 0 3px; font-weight: bold; }"
        "label { padding: 0; }"
        "button { padding: 2px 6px; }"
        "entry, combobox, spinbutton { padding: 1px 3px; }"
        "spinbutton button { min-width: 14px; padding: 0; }"
        "combobox { min-height: 0px; padding: 0 2px; }"
        "combobox button { min-height: 0px; padding: 0 2px; }"
        "combobox text { padding: 0; }"
        "entry { min-height: 0px; }"
        "checkbutton { padding: 1px 0; }", -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* paired sections so columns stay balanced */
    GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(page), row1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), row2, FALSE, FALSE, 0);
    u->src_frame = build_source_section(u);
    u->qual_frame = build_quality_section(u);
    gtk_box_pack_start(GTK_BOX(row1), build_conn_section(u), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row1), u->src_frame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row2), u->qual_frame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row2), build_audio_section(u), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), build_input_section(u), FALSE, FALSE, 0);

    GdkPixbuf *pb = load_icon();
    u->tray = gtk_status_icon_new();
    if (pb) {
        gtk_status_icon_set_from_pixbuf(u->tray, pb);
        g_object_unref(pb);
    } else {
        gtk_status_icon_set_from_icon_name(u->tray, "network-workgroup");
    }
    gtk_status_icon_set_tooltip_text(u->tray, "wupstreamd");
    g_signal_connect(u->tray, "popup-menu", G_CALLBACK(on_tray_popup), u);
    g_signal_connect(u->tray, "activate", G_CALLBACK(on_open_settings), u);

    gtk_widget_set_sensitive(u->src_frame, !u->s->gamepad_only);
    gtk_widget_set_sensitive(u->qual_frame, !u->s->gamepad_only);
    gtk_widget_set_sensitive(u->rstick_chk, u->s->input.mode == INMODE_KBMOUSE);

    g_timeout_add(500, status_tick, u);
    g_timeout_add(100, viz_tick, u);

    return u;
}

void ui_free(ui_t *u) {
    if (!u) return;
    if (u->save_id) g_source_remove(u->save_id);
    free(u);
}
