#define _GNU_SOURCE
#include "ui.h"
#include "x11util.h"
#include "select.h"
#include "input.h"
#include "server.h"
#include "viz.h"
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
    GtkWidget *cmd_combo;
    GtkWidget *stick_entries[8];
    GtkWidget *viz_area, *black_chk;
    GtkWidget *row1, *row2, *input_cols; /* adaptive layout */
    int stream_icon_mode; /* update button icon only on state changes */
    GtkCssProvider *theme_css;
    GtkWidget *theme_buttons[5];
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

/* WUP_V4_THEME_SWITCHER: live, scoped GTK3 palettes; no global OS theme change. */
typedef struct {
    const char *name, *background, *panel, *entry, *border;
    const char *text, *muted, *accent, *button, *hover, *active;
} wup_theme_palette_t;

static const wup_theme_palette_t wup_palettes[5] = {
    /* true neutral charcoal: no blue cast */
    {"Graphite", "#222224", "#2c2c2f", "#1b1b1d", "#505053",
     "#eeeeef", "#b5b5b8", "#e4e4e7", "#38383c", "#46464a", "#59595e"},
    {"Light", "#f0f1f3", "#ffffff", "#f8f9fb", "#c5ccd4",
     "#20252d", "#586575", "#2463a6", "#e7ebf0", "#d9e1ea", "#b8d7f5"},
    /* original blue-grey V3, refined */
    {"Ocean", "#111923", "#192532", "#0f1b27", "#35485b",
     "#dce7f1", "#9fb5c8", "#96cdf9", "#253547", "#334c66", "#285277"},
    {"Emerald", "#101b17", "#172a22", "#0b1c17", "#315e4b",
     "#e0f3e9", "#a3c6b4", "#65ddb0", "#244537", "#315d49", "#226a4b"},
    {"Rose", "#21161e", "#30202c", "#1b1119", "#69465d",
     "#f6e5ed", "#cfacc0", "#ffa3d0", "#4b3043", "#65405b", "#823f68"}
};

static void wup_apply_theme(ui_t *u) {
    if (!u || !u->theme_css) return;
    int index = u->s->ui_theme;
    if (index < 0 || index >= 5) index = 2;
    const wup_theme_palette_t *p = &wup_palettes[index];
    GString *css = g_string_new(NULL);
    g_string_append_printf(css,
        "window.wup-settings { background-color: %s; color: %s; }"
        ".wup-settings { color: %s; }"
        ".wup-settings * { font-size: 11px; }",
        p->background, p->text, p->text);
    g_string_append_printf(css,
        ".wup-settings frame { background-color: %s; border: 1px solid %s; "
        "border-radius: 8px; margin: 1px; padding: 2px; }"
        ".wup-settings .wup-section-title { color: %s; font-weight: bold; "
        "padding: 0px 3px 0px 0px; }"
        ".wup-settings label { color: %s; padding: 0px; }"
        ".wup-settings label:disabled { color: %s; }",
        p->panel, p->border, p->accent, p->text, p->muted);
    g_string_append_printf(css,
        ".wup-settings button { min-height: 17px; padding: 1px 6px; "
        "border: 1px solid %s; border-radius: 5px; "
        "background-image: none; background-color: %s; color: %s; }"
        ".wup-settings button:hover { background-color: %s; border-color: %s; }"
        ".wup-settings button:active, .wup-settings button:checked { background-color: %s; }"
        ".wup-settings button:disabled { color: %s; }"
        ".wup-settings .wup-icon-button { min-width: 20px; padding: 1px 3px; }",
        p->border, p->button, p->text, p->hover, p->accent, p->active, p->muted);
    g_string_append_printf(css,
        ".wup-settings entry { min-height: 17px; padding: 1px 4px; "
        "background-color: %s; color: %s; border-radius: 4px; "
        "border: 1px solid %s; }"
        ".wup-settings entry:focus { border-color: %s; }"
        ".wup-settings entry selection { background-color: %s; color: %s; }",
        p->entry, p->text, p->border, p->accent, p->active, p->text);
    g_string_append_printf(css,
        ".wup-settings spinbutton { min-height: 17px; padding: 0px; }"
        ".wup-settings spinbutton entry { min-height: 17px; padding: 1px 3px; }"
        ".wup-settings spinbutton button { min-width: 14px; min-height: 17px; padding: 0px 1px; }"
        ".wup-settings combobox { min-height: 17px; padding: 0px; }"
        ".wup-settings combobox button { min-height: 17px; padding: 0px 4px; }"
        ".wup-settings checkbutton { min-height: 17px; padding: 0px; }"
        ".wup-settings checkbutton check { min-width: 14px; min-height: 14px; "
        "border-radius: 3px; background-color: %s; border: 1px solid %s; color: %s; }"
        ".wup-settings checkbutton check:checked { background-color: %s; color: %s; }"
        ".wup-settings scrolledwindow { background-color: transparent; }"
        ".wup-settings image { color: %s; }",
        p->entry, p->border, p->text, p->active, p->text, p->text);

    /* Each of the five 23px buttons is a color-coded theme preview. */
    static const char *classes[5] = {"graphite", "light", "ocean", "emerald", "rose"};
    static const char *swatches[5] = {"#38383c", "#f0f1f3", "#247ab6", "#287b57", "#ad5286"};
    static const char *ink[5] = {"#f4f4f6", "#242b34", "#f4fcff", "#f0fff6", "#fff0f8"};
    for (int i = 0; i < 5; i++) {
        g_string_append_printf(css,
            ".wup-settings button.wup-theme-%s { min-width: 22px; min-height: 22px; "
            "padding: 1px; border-radius: 6px; background-image: none; "
            "background-color: %s; color: %s; border: 1px solid %s; }"
            ".wup-settings button.wup-theme-%s image { color: %s; }"
            ".wup-settings button.wup-theme-%s:checked { border: 2px solid %s; "
            "box-shadow: 0 0 0 1px %s; }",
            classes[i], swatches[i], ink[i], p->border,
            classes[i], ink[i], classes[i], p->accent, p->accent);
    }
    GError *error = NULL;
    gtk_css_provider_load_from_data(u->theme_css, css->str, -1, &error);
    if (error) {
        g_warning("WUP theme CSS: %s", error->message);
        g_error_free(error);
    }
    g_string_free(css, TRUE);
}

static void wup_theme_toggled(GtkToggleButton *btn, ui_t *u) {
    int selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "wup-theme-index"));
    if (selected < 0 || selected >= 5) return;
    if (!gtk_toggle_button_get_active(btn)) {
        /* Re-clicking the selected button must not leave no theme selected. */
        if (selected == u->s->ui_theme)
            gtk_toggle_button_set_active(btn, TRUE);
        return;
    }
    gboolean changed = u->s->ui_theme != selected;
    /* Change state first, so deselection signals cannot reactivate old buttons. */
    u->s->ui_theme = selected;
    for (int i = 0; i < 5; i++) {
        if (i != selected && u->theme_buttons[i])
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->theme_buttons[i]), FALSE);
    }
    if (changed) {
        wup_apply_theme(u);  /* synchronous: no restart, no stream interruption */
        schedule_save(u);
    }
}

static GtkWidget *wup_theme_bar(ui_t *u) {
    static const char *names[] = {"Graphite (neutral grey)", "Light", "Ocean blue",
                                  "Emerald green", "Rose pink"};
    static const char *icons[] = {"weather-clear-night-symbolic", "weather-clear-symbolic",
                                  "preferences-desktop-theme-symbolic", "emblem-default-symbolic",
                                  "emblem-favorite-symbolic"};
    static const char *fallback[] = {"D", "L", "B", "G", "P"};
    static const char *classes[] = {"wup-theme-graphite", "wup-theme-light",
                                    "wup-theme-ocean", "wup-theme-emerald", "wup-theme-rose"};
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    for (int i = 0; i < 5; i++) {
        GtkWidget *b = gtk_toggle_button_new();
        GtkWidget *content = gtk_icon_theme_has_icon(theme, icons[i])
            ? gtk_image_new_from_icon_name(icons[i], GTK_ICON_SIZE_MENU)
            : gtk_label_new(fallback[i]);
        gtk_container_add(GTK_CONTAINER(b), content);
        gtk_widget_set_tooltip_text(b, names[i]);
        gtk_widget_set_size_request(b, 23, 23);
        gtk_style_context_add_class(gtk_widget_get_style_context(b), "wup-theme-button");
        gtk_style_context_add_class(gtk_widget_get_style_context(b), classes[i]);
        g_object_set_data(G_OBJECT(b), "wup-theme-index", GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
        u->theme_buttons[i] = b;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), i == u->s->ui_theme);
        g_signal_connect(b, "toggled", G_CALLBACK(wup_theme_toggled), u);
    }
    return bar;
}

static GtkWidget *mk_label(const char *t) {
    GtkWidget *l = gtk_label_new(t);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    return l;
}

/* WUP_V3_MODERN_DENSE_UI: symbolic icons use the system GTK theme (no PNG dependencies).
 * A small text fallback keeps controls usable with minimal icon themes. */
static gboolean has_symbol(const char *name) {
    return gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), name);
}

static GtkWidget *symbol_image(const char *name) {
    return gtk_image_new_from_icon_name(name, GTK_ICON_SIZE_MENU);
}

static void button_symbol(GtkWidget *button, const char *name) {
    if (!has_symbol(name)) return;
    gtk_button_set_image(GTK_BUTTON(button), symbol_image(name));
    gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
    gtk_button_set_image_position(GTK_BUTTON(button), GTK_POS_LEFT);
}

static GtkWidget *symbol_button(const char *name, const char *fallback,
                                const char *tip) {
    GtkWidget *button = gtk_button_new_with_label(fallback);
    if (has_symbol(name)) {
        gtk_button_set_label(GTK_BUTTON(button), NULL);
        button_symbol(button, name);
    }
    gtk_widget_set_tooltip_text(button, tip);
    gtk_style_context_add_class(gtk_widget_get_style_context(button), "wup-icon-button");
    return button;
}

static const char *section_symbol(const char *title) {
    if (!strcmp(title, "GamePad Connection")) return "network-wireless-symbolic";
    if (!strcmp(title, "Video Source")) return "video-display-symbolic";
    if (!strcmp(title, "Quality")) return "preferences-system-symbolic";
    if (!strcmp(title, "Audio")) return "audio-volume-high-symbolic";
    if (!strcmp(title, "Inputs")) return "input-gaming-symbolic";
    if (!strcmp(title, "Gamepad (evdev)")) return "input-gaming-symbolic";
    if (!strcmp(title, "Keyboard + mouse")) return "input-keyboard-symbolic";
    if (!strcmp(title, "Touch + motion")) return "input-touchpad-symbolic";
    return "applications-system-symbolic";
}

static GtkWidget *mk_spin(int min, int max, int val) {
    GtkAdjustment *a = gtk_adjustment_new(val, min, max, 1, 10, 0);
    return gtk_spin_button_new(a, 1, 0);
}

/* section frame with title */
/* Section headings have theme-native symbolic icons and clean frame chrome. */
static GtkWidget *mk_section(const char *title) {
    GtkWidget *f = gtk_frame_new(NULL);
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(vb), 4);
    GtkWidget *head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    const char *icon = section_symbol(title);
    if (has_symbol(icon)) {
        GtkWidget *img = symbol_image(icon);
        gtk_box_pack_start(GTK_BOX(head), img, FALSE, FALSE, 0);
    }
    GtkWidget *name = mk_label(title);
    gtk_style_context_add_class(gtk_widget_get_style_context(name),
                                "wup-section-title");
    gtk_box_pack_start(GTK_BOX(head), name, FALSE, FALSE, 0);
    gtk_frame_set_label_widget(GTK_FRAME(f), head);
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
    gtk_grid_set_row_spacing(GTK_GRID(grid), 1);
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
    GtkWidget *wbtn = symbol_button("view-refresh-symbolic", "R", "Refresh window list");
    g_signal_connect(wbtn, "clicked", G_CALLBACK(on_refresh_windows), u);
    gtk_box_pack_start(GTK_BOX(u->win_row), wbtn, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(grid), u->win_row, 0, r++, 2, 1);

    /* region row: label + button + info, hidden together */
    u->region_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(u->region_box), mk_label("Region:"), FALSE, FALSE, 0);
    GtkWidget *rbtn = gtk_button_new_with_label("Select...");
    button_symbol(rbtn, "edit-select-all-symbolic");
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
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(sec), grid, FALSE, FALSE, 0);

    /* Two rows instead of four. Preserve original callbacks and controls. */
    u->preset_combo = gtk_combo_box_text_new();
    refresh_presets(u);
    g_signal_connect(u->preset_combo, "changed", G_CALLBACK(on_preset_changed), u);
    gtk_grid_attach(GTK_GRID(grid), mk_label("Preset:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), u->preset_combo, 1, 0, 1, 1);

    u->crf_spin = mk_spin(0, 51, u->s->crf);
    gtk_widget_set_tooltip_text(u->crf_spin, "Lower CRF improves image quality");
    g_signal_connect(u->crf_spin, "value-changed", G_CALLBACK(on_quality_spin), u);
    gtk_grid_attach(GTK_GRID(grid), mk_label("CRF:"), 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), u->crf_spin, 3, 0, 1, 1);

    u->kbps_spin = mk_spin(0, 50000, u->s->kbps);
    gtk_widget_set_tooltip_text(u->kbps_spin, "Maximum bitrate in kbps; 0 = automatic");
    g_signal_connect(u->kbps_spin, "value-changed", G_CALLBACK(on_quality_spin), u);
    gtk_grid_attach(GTK_GRID(grid), mk_label("Bitrate:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), u->kbps_spin, 1, 1, 1, 1);

    u->gop_spin = mk_spin(0, 600, u->s->gop);
    gtk_widget_set_tooltip_text(u->gop_spin, "Keyframe interval; 0 = automatic");
    g_signal_connect(u->gop_spin, "value-changed", G_CALLBACK(on_quality_spin), u);
    gtk_grid_attach(GTK_GRID(grid), mk_label("GOP:"), 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), u->gop_spin, 3, 1, 1, 1);

    GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(sec), line, FALSE, FALSE, 0);
    u->lowlat_chk = gtk_check_button_new_with_label("Low latency");
    gtk_widget_set_tooltip_text(u->lowlat_chk,
        "x264 zerolatency: no lookahead, no B-frames");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->lowlat_chk), u->s->low_latency);
    g_signal_connect(u->lowlat_chk, "toggled", G_CALLBACK(on_lowlat_toggled), u);
    gtk_box_pack_start(GTK_BOX(line), u->lowlat_chk, TRUE, TRUE, 0);

    GtkWidget *sbtn = gtk_button_new_with_label("Save preset");
    gtk_widget_set_tooltip_text(sbtn, "Save quality settings as a named preset");
    button_symbol(sbtn, "document-save-symbolic");
    g_signal_connect(sbtn, "clicked", G_CALLBACK(on_save_preset), u);
    gtk_box_pack_end(GTK_BOX(line), sbtn, FALSE, FALSE, 0);
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


static void on_key_edited(GtkEntry *e, ui_t *u);
static void on_stick_key_edited(GtkEntry *e, ui_t *u);

/* push settings.input.keys/stick_keys back into the entry widgets
   (used when a command preset is applied) */
static void refresh_key_entries(ui_t *u) {
    for (int i = 0; i < 17; i++) {
        if (!u->key_entries[i]) continue;
        g_signal_handlers_block_by_func(u->key_entries[i], G_CALLBACK(on_key_edited), u);
        gtk_entry_set_text(GTK_ENTRY(u->key_entries[i]), u->s->input.keys[i]);
        g_signal_handlers_unblock_by_func(u->key_entries[i], G_CALLBACK(on_key_edited), u);
    }
    for (int i = 0; i < 8; i++) {
        if (!u->stick_entries[i]) continue;
        g_signal_handlers_block_by_func(u->stick_entries[i], G_CALLBACK(on_stick_key_edited), u);
        gtk_entry_set_text(GTK_ENTRY(u->stick_entries[i]), u->s->input.stick_keys[i]);
        g_signal_handlers_unblock_by_func(u->stick_entries[i], G_CALLBACK(on_stick_key_edited), u);
    }
}

static void refresh_cmd_presets(ui_t *u) {
    GtkComboBoxText *cb = GTK_COMBO_BOX_TEXT(u->cmd_combo);
    gtk_combo_box_text_remove_all(cb);
    gtk_combo_box_text_append_text(cb, "(custom)");
    for (int i = 0; i < u->s->input.ncmd; i++)
        gtk_combo_box_text_append_text(cb, u->s->input.cmd_presets[i].name);
    int act = 0;
    for (int i = 0; i < u->s->input.ncmd; i++)
        if (!strcmp(u->s->input.cmd_presets[i].name, u->s->input.cmd_preset))
            act = i + 1;
    gtk_combo_box_set_active(GTK_COMBO_BOX(cb), act);
}

static void on_cmd_preset_changed(GtkComboBox *cb, ui_t *u) {
    int idx = gtk_combo_box_get_active(cb);
    if (idx <= 0 || idx - 1 >= u->s->input.ncmd) {
        u->s->input.cmd_preset[0] = 0;
        schedule_save(u);
        return;
    }
    cmd_preset_t *cp = &u->s->input.cmd_presets[idx - 1];
    snprintf(u->s->input.cmd_preset, sizeof u->s->input.cmd_preset, "%s", cp->name);
    memcpy(u->s->input.keys, cp->keys, sizeof u->s->input.keys);
    memcpy(u->s->input.stick_keys, cp->stick_keys, sizeof u->s->input.stick_keys);
    refresh_key_entries(u);
    server_apply_input_keys(u->srv);
    schedule_save(u);
}

static void on_cmd_save(GtkButton *b, ui_t *u) {
    (void)b;
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Save command preset",
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(u->cmd_combo))),
        GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *lbl = gtk_label_new("Preset name (per-app keymap):");
    GtkWidget *ent = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(ent),
        u->s->input.cmd_preset[0] ? u->s->input.cmd_preset : "Preset");
    gtk_box_pack_start(GTK_BOX(content), lbl, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(content), ent, FALSE, FALSE, 6);
    gtk_widget_show_all(dlg);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(ent));
        if (name && name[0]) {
            /* replace same-name or append */
            int slot = -1;
            for (int i = 0; i < u->s->input.ncmd; i++)
                if (!strcmp(u->s->input.cmd_presets[i].name, name)) slot = i;
            if (slot < 0) {
                if (u->s->input.ncmd < 8) slot = u->s->input.ncmd++;
            }
            if (slot >= 0) {
                cmd_preset_t *cp = &u->s->input.cmd_presets[slot];
                snprintf(cp->name, sizeof cp->name, "%s", name);
                memcpy(cp->keys, u->s->input.keys, sizeof cp->keys);
                memcpy(cp->stick_keys, u->s->input.stick_keys, sizeof cp->stick_keys);
                snprintf(u->s->input.cmd_preset, sizeof u->s->input.cmd_preset, "%s", name);
                refresh_cmd_presets(u);
                settings_save(u->s);
            }
        }
    }
    gtk_widget_destroy(dlg);
}

static void on_cmd_delete(GtkButton *b, ui_t *u) {
    (void)b;
    int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(u->cmd_combo));
    if (idx <= 0 || idx - 1 >= u->s->input.ncmd) return;
    memmove(&u->s->input.cmd_presets[idx - 1], &u->s->input.cmd_presets[idx],
            (u->s->input.ncmd - idx) * sizeof(cmd_preset_t));
    u->s->input.ncmd--;
    u->s->input.cmd_preset[0] = 0;
    refresh_cmd_presets(u);
    settings_save(u->s);
}

static void on_key_help(GtkButton *b, ui_t *u) {
    (void)b; (void)u;
    const char *txt =
        "Key syntax for button/stick mappings:\n"
        "\n"
        "  a            single letter (case-insensitive)\n"
        "  F1..F12      function keys\n"
        "  Left Right Up Down   arrow keys\n"
        "  Return Escape space Tab BackSpace Delete Insert\n"
        "  Home End Page_Up Page_Down\n"
        "  minus plus equal comma period slash backslash\n"
        "  semicolon apostrophe bracketleft bracketright grave\n"
        "\n"
        "  Chords: modifiers + key, e.g.\n"
        "    Ctrl+A          Control + A\n"
        "    Alt+Ctrl+G      Alt + Control + G\n"
        "    Shift+F5       Shift + F5\n"
        "    Super+Return   Super/Win + Enter\n"
        "\n"
        "  Modifiers: Ctrl/Control, Alt, Shift, Super/Win/Meta\n"
        "  (also Control_L, Alt_L, Shift_L, Super_L accepted)\n"
        "  Empty field = unmapped.\n"
        "\n"
        "  Command presets save whole keymaps (buttons + stick dirs)\n"
        "  as JSON in settings.json - one per app if you like.";
    GtkWidget *dlg = gtk_dialog_new_with_buttons("Key mapping help",
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(u->cmd_combo))),
        GTK_DIALOG_MODAL, "_Close", GTK_RESPONSE_CLOSE, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(sw, 420, 380);
    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), txt, -1);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_container_add(GTK_CONTAINER(content), sw);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
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

static GtkWidget *build_input_section(ui_t *u) {
    GtkWidget *sec = mk_section("Inputs");

    GtkWidget *mrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(mrow), mk_label("Mode:"), FALSE, FALSE, 0);
    u->mode_combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(u->mode_combo, FALSE);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->mode_combo), "Virtual gamepad (evdev)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->mode_combo), "Keyboard + mouse (X11)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(u->mode_combo), u->s->input.mode);
    g_signal_connect(u->mode_combo, "changed", G_CALLBACK(on_mode_changed), u);
    gtk_box_pack_start(GTK_BOX(mrow), u->mode_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sec), mrow, FALSE, FALSE, 0);

    /* Two columns: pad + sensors, keyboard mappings */
    GtkWidget *cols = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    u->input_cols = cols;
    gtk_box_pack_start(GTK_BOX(sec), cols, FALSE, FALSE, 0);

    /* gamepad column */
    GtkWidget *gp = mk_section("Gamepad (evdev)");
    gtk_box_pack_start(GTK_BOX(cols), frame_of(gp), TRUE, TRUE, 0);
    {
        GtkWidget *padopts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(gp), padopts, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(padopts), mk_label("Deadzone:"), FALSE, FALSE, 0);
        u->dz_spin = mk_spin(0, 500, u->s->input.deadzone);
        g_signal_connect(u->dz_spin, "value-changed", G_CALLBACK(on_input_spin), u);
        gtk_box_pack_start(GTK_BOX(padopts), u->dz_spin, FALSE, FALSE, 0);
        u->invert_chk = gtk_check_button_new_with_label("Invert Y");
        gtk_widget_set_tooltip_text(u->invert_chk, "Invert GamePad stick Y axis");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->invert_chk), u->s->input.invert_y);
        g_signal_connect(u->invert_chk, "toggled", G_CALLBACK(on_input_toggled), u);
        gtk_box_pack_start(GTK_BOX(padopts), u->invert_chk, FALSE, FALSE, 0);
        u->viz_area = pad_viz_new();
        gtk_widget_set_tooltip_text(u->viz_area, "Live GamePad buttons, sticks, touch and gyro axes");
        gtk_widget_set_hexpand(u->viz_area, TRUE);
        gtk_box_pack_start(GTK_BOX(gp), u->viz_area, FALSE, FALSE, 0);
    }

    /* kb/mouse column */
    GtkWidget *kb = mk_section("Keyboard + mouse");
    gtk_box_pack_start(GTK_BOX(cols), frame_of(kb), TRUE, TRUE, 0);
    {
        u->rstick_chk = gtk_check_button_new_with_label("Right stick = mouse");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->rstick_chk), u->s->input.right_stick_mouse);
        g_signal_connect(u->rstick_chk, "toggled", G_CALLBACK(on_input_toggled), u);
        gtk_box_pack_start(GTK_BOX(kb), u->rstick_chk, FALSE, FALSE, 0);
        /* command presets row: combo + save + delete + help */
        {
            GtkWidget *prow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            gtk_box_pack_start(GTK_BOX(kb), prow, FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(prow), mk_label("Preset:"), FALSE, FALSE, 0);
            u->cmd_combo = gtk_combo_box_text_new();
            gtk_widget_set_hexpand(u->cmd_combo, TRUE);
            refresh_cmd_presets(u);
            g_signal_connect(u->cmd_combo, "changed", G_CALLBACK(on_cmd_preset_changed), u);
            gtk_box_pack_start(GTK_BOX(prow), u->cmd_combo, TRUE, TRUE, 0);
            GtkWidget *hb = symbol_button("help-browser-symbolic", "?", "Key mapping syntax help");
            gtk_widget_set_tooltip_text(hb, "Key syntax help");
            g_signal_connect(hb, "clicked", G_CALLBACK(on_key_help), u);
            gtk_box_pack_start(GTK_BOX(prow), hb, FALSE, FALSE, 0);
            GtkWidget *sv = gtk_button_new_with_label("Save");
            button_symbol(sv, "document-save-symbolic");
            gtk_widget_set_tooltip_text(sv, "Save current mapping as a named preset");
            g_signal_connect(sv, "clicked", G_CALLBACK(on_cmd_save), u);
            gtk_box_pack_start(GTK_BOX(prow), sv, FALSE, FALSE, 0);
            GtkWidget *dl = symbol_button("edit-delete-symbolic", "Del", "Delete current mapping preset");
            g_signal_connect(dl, "clicked", G_CALLBACK(on_cmd_delete), u);
            gtk_box_pack_start(GTK_BOX(prow), dl, FALSE, FALSE, 0);
        }
        gtk_box_pack_start(GTK_BOX(kb), mk_label("Buttons -> keys  (Ctrl+A supported):"), FALSE, FALSE, 0);
        GtkWidget *kmid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(kb), kmid, FALSE, FALSE, 0);
        GtkWidget *kgrid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(kgrid), 0);
        gtk_grid_set_column_spacing(GTK_GRID(kgrid), 4);
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
        gtk_grid_set_row_spacing(GTK_GRID(sgrid), 0);
        gtk_grid_set_column_spacing(GTK_GRID(sgrid), 4);
        for (int i = 0; i < 8; i++) {
            gtk_grid_attach(GTK_GRID(sgrid), mk_label(stick_dir_names[i]), 0, i, 1, 1);
            u->stick_entries[i] = gtk_entry_new();
            gtk_entry_set_text(GTK_ENTRY(u->stick_entries[i]), u->s->input.stick_keys[i]);
            gtk_entry_set_width_chars(GTK_ENTRY(u->stick_entries[i]), 7);
            g_signal_connect(u->stick_entries[i], "changed", G_CALLBACK(on_stick_key_edited), u);
            gtk_grid_attach(GTK_GRID(sgrid), u->stick_entries[i], 1, i, 1, 1);
        }
        gtk_box_pack_start(GTK_BOX(sbox), sgrid, FALSE, FALSE, 0);
    }

    /* Two compact rows under the visualizer, with full tooltip descriptions. */
    GtkWidget *sh = mk_section("Touch + motion");
    gtk_box_pack_start(GTK_BOX(gp), frame_of(sh), FALSE, FALSE, 0);
    {
        GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(sh), line, FALSE, FALSE, 0);
        u->touch_chk = gtk_check_button_new_with_label("Touch -> mouse");
        gtk_widget_set_tooltip_text(u->touch_chk,
            "Use the GamePad touchscreen as a mouse, including in virtual gamepad mode");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->touch_chk),
                                     u->s->input.touch_as_mouse);
        g_signal_connect(u->touch_chk, "toggled", G_CALLBACK(on_input_toggled), u);
        gtk_box_pack_start(GTK_BOX(line), u->touch_chk, TRUE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(line), mk_label("Gyro:"), FALSE, FALSE, 0);
        u->gyro_combo = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->gyro_combo), "Off");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->gyro_combo), "Mouse");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(u->gyro_combo), "Right stick");
        gtk_combo_box_set_active(GTK_COMBO_BOX(u->gyro_combo), u->s->input.gyro_mode);
        g_signal_connect(u->gyro_combo, "changed", G_CALLBACK(on_gyro_changed), u);
        gtk_box_pack_start(GTK_BOX(line), u->gyro_combo, FALSE, FALSE, 0);

        GtkWidget *line2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_box_pack_start(GTK_BOX(sh), line2, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(line2), mk_label("Input %:"), FALSE, FALSE, 0);
        u->sens_spin = mk_spin(10, 400, u->s->input.sensitivity);
        g_signal_connect(u->sens_spin, "value-changed", G_CALLBACK(on_input_spin), u);
        gtk_box_pack_start(GTK_BOX(line2), u->sens_spin, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(line2), mk_label("Gyro %:"), FALSE, FALSE, 0);
        u->gyro_sens_spin = mk_spin(10, 500, u->s->input.gyro_sens);
        g_signal_connect(u->gyro_sens_spin, "value-changed",
                         G_CALLBACK(on_input_spin), u);
        gtk_box_pack_start(GTK_BOX(line2), u->gyro_sens_spin, FALSE, FALSE, 0);

        GtkWidget *cal = symbol_button("view-refresh-symbolic", "Cal",
                                      "Calibrate gyroscope: hold pad still for ~3 seconds while streaming");
        g_signal_connect(cal, "clicked", G_CALLBACK(on_calib_clicked), u);
        gtk_box_pack_end(GTK_BOX(line2), cal, FALSE, FALSE, 0);
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
    GtkWidget *mbtn = symbol_button("view-refresh-symbolic", "R", "Refresh audio inputs");
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
    GtkWidget *line1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    gtk_box_pack_start(GTK_BOX(sec), line1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(line1), mk_label("Pad IP:"), FALSE, FALSE, 0);
    u->ip_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(u->ip_entry), u->s->pad_ip);
    gtk_entry_set_width_chars(GTK_ENTRY(u->ip_entry), 13);
    gtk_entry_set_max_width_chars(GTK_ENTRY(u->ip_entry), 15);
    gtk_entry_set_placeholder_text(GTK_ENTRY(u->ip_entry), "auto");
    gtk_widget_set_tooltip_text(u->ip_entry,
        "GamePad IPv4 address. Leave empty for automatic discovery.");
    g_signal_connect(u->ip_entry, "changed", G_CALLBACK(on_ip_changed), u);
    gtk_box_pack_start(GTK_BOX(line1), u->ip_entry, FALSE, FALSE, 0);

    u->autostart_chk = gtk_check_button_new_with_label("Auto-start");
    gtk_widget_set_tooltip_text(u->autostart_chk,
        "Start the stream automatically when the application opens");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->autostart_chk),
                                 u->s->autostart_stream);
    g_signal_connect(u->autostart_chk, "toggled", G_CALLBACK(on_autostart_toggled), u);
    gtk_box_pack_start(GTK_BOX(line1), u->autostart_chk, FALSE, FALSE, 0);

    GtkWidget *line2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(sec), line2, FALSE, FALSE, 0);
    GtkWidget *gponly_chk = gtk_check_button_new_with_label("GamePad-only mode");
    gtk_widget_set_tooltip_text(gponly_chk,
        "Keep GamePad inputs live with no PC video capture. Useful for calibration.");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gponly_chk), u->s->gamepad_only);
    g_signal_connect(gponly_chk, "toggled", G_CALLBACK(on_gamepad_only_toggled), u);
    gtk_box_pack_start(GTK_BOX(line2), gponly_chk, FALSE, FALSE, 0);

    u->black_chk = gtk_check_button_new_with_label("Black screen");
    gtk_widget_set_tooltip_text(u->black_chk,
        "Show black instead of the test image in GamePad-only mode");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->black_chk),
                                 u->s->gamepad_only_black);
    g_signal_connect(u->black_chk, "toggled", G_CALLBACK(on_black_toggled), u);
    gtk_box_pack_start(GTK_BOX(line2), u->black_chk, FALSE, FALSE, 0);
    gtk_widget_set_sensitive(u->black_chk, u->s->gamepad_only);
    return frame_of(sec);
}

/* GTK3 has no CSS media queries. Switch only the main pairs from
 * horizontal to vertical when a small settings window is requested.
 * Within either mode we never require a horizontal scrollbar. */
static gboolean on_settings_configure(GtkWidget *widget,
                                      GdkEventConfigure *ev, gpointer data) {
    (void)widget;
    ui_t *u = data;
    GtkOrientation upper = ev->width < 840 ? GTK_ORIENTATION_VERTICAL
                                            : GTK_ORIENTATION_HORIZONTAL;
    GtkOrientation lower = ev->width < 850 ? GTK_ORIENTATION_VERTICAL
                                            : GTK_ORIENTATION_HORIZONTAL;
    if (u->row1 && gtk_orientable_get_orientation(GTK_ORIENTABLE(u->row1)) != upper)
        gtk_orientable_set_orientation(GTK_ORIENTABLE(u->row1), upper);
    if (u->row2 && gtk_orientable_get_orientation(GTK_ORIENTABLE(u->row2)) != upper)
        gtk_orientable_set_orientation(GTK_ORIENTABLE(u->row2), upper);
    if (u->input_cols && gtk_orientable_get_orientation(GTK_ORIENTABLE(u->input_cols)) != lower)
        gtk_orientable_set_orientation(GTK_ORIENTABLE(u->input_cols), lower);
    return FALSE;
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

/* WUP_V4_THEME_SWITCHER: resolve icon on developer builds AND installed builds.
 * Never stop at a NULL environment variable (the V3 loader's original bug). */
static GdkPixbuf *load_icon(void) {
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    char *bindir = exe ? g_path_get_dirname(exe) : NULL;
    char *local_png = bindir ? g_build_filename(bindir, "..", "data", "wupstreamd.png", NULL) : NULL;
    char *local_svg = bindir ? g_build_filename(bindir, "..", "data", "wupstreamd.svg", NULL) : NULL;
    const char *paths[] = {
        getenv("WUPSTREAMD_ICON"),
        local_png, local_svg,
        "data/wupstreamd.png", "data/wupstreamd.svg",
        "/usr/share/icons/hicolor/256x256/apps/wupstreamd.png",
        "/usr/local/share/icons/hicolor/256x256/apps/wupstreamd.png",
        "/usr/share/icons/hicolor/scalable/apps/wupstreamd.svg",
        "/usr/local/share/icons/hicolor/scalable/apps/wupstreamd.svg",
        "/usr/share/pixmaps/wupstreamd.png",
        "/usr/share/pixmaps/wupstreamd.svg"
    };
    GdkPixbuf *pb = NULL;
    for (guint i = 0; i < G_N_ELEMENTS(paths); i++) {
        if (!paths[i] || !paths[i][0]) continue;
        GError *error = NULL;
        pb = gdk_pixbuf_new_from_file(paths[i], &error);
        if (pb) break;
        if (error) g_error_free(error);
    }
    g_free(local_png);
    g_free(local_svg);
    g_free(bindir);
    g_free(exe);
    return pb;
}

static gboolean status_tick(gpointer p) {
    ui_refresh_status(p);
    return G_SOURCE_CONTINUE;
}

static gboolean viz_tick(gpointer p) {
    ui_t *u = p;
    if (u->viz_area) {
        wupstream_input_t in;
        pad_viz_feed(u->viz_area,
                     server_input_snapshot(u->srv, &in) ? &in : NULL);
    }
    return G_SOURCE_CONTINUE;
}

void ui_refresh_status(ui_t *u) {
    if (!u->status_lbl) return;
    char buf[512];
    int st = server_state(u->srv);
    snprintf(buf, sizeof buf, "Status: %s", server_status(u->srv));
    gtk_label_set_text(GTK_LABEL(u->status_lbl), buf);
    if (u->stream_btn) {
        int active = st == SRV_STREAMING || st == SRV_WAITING_PAD;
        if (u->stream_icon_mode != active) {
            gtk_button_set_label(GTK_BUTTON(u->stream_btn),
                                 active ? "Stop stream" : "Start stream");
            button_symbol(u->stream_btn, active ? "media-playback-stop-symbolic"
                                                : "media-playback-start-symbolic");
            u->stream_icon_mode = active;
        }
    }
}

ui_t *ui_new(settings_t *s, server_t *srv) {
    ui_t *u = g_new0(ui_t, 1);
    u->s = s; u->srv = srv;

    u->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_style_context_add_class(gtk_widget_get_style_context(u->window),
                                "wup-settings");
    u->stream_icon_mode = -1;
    gtk_window_set_title(GTK_WINDOW(u->window), "wupstreamd — settings");
    {
        GdkPixbuf *ic = load_icon();
        if (ic) {
            gtk_window_set_icon(GTK_WINDOW(u->window), ic);
            gtk_window_set_default_icon(ic);
            g_object_unref(ic);
        }
    }
    gtk_window_set_default_size(GTK_WINDOW(u->window), 960, 680);
    g_signal_connect(u->window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    gtk_widget_set_size_request(u->window, 680, 530);

    /* single scrollable page: status bar + sections */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(u->window), scroll);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(page), 5);
    gtk_container_add(GTK_CONTAINER(scroll), page);

    /* top bar: status + stream button */
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(page), top, FALSE, FALSE, 0);
    u->status_lbl = gtk_label_new("Status: ...");
    gtk_label_set_ellipsize(GTK_LABEL(u->status_lbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(u->status_lbl), 65);
    gtk_label_set_xalign(GTK_LABEL(u->status_lbl), 0);
    gtk_box_pack_start(GTK_BOX(top), u->status_lbl, FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(u->status_lbl, "Current stream connection status");
    GtkWidget *themes = wup_theme_bar(u);  /* five live theme buttons, beside status */
    gtk_box_pack_start(GTK_BOX(top), themes, FALSE, FALSE, 6);
    GtkWidget *top_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(top), top_spacer, TRUE, TRUE, 0);
    u->stream_btn = gtk_button_new_with_label("Start stream");
    button_symbol(u->stream_btn, "media-playback-start-symbolic");
    g_signal_connect(u->stream_btn, "clicked", G_CALLBACK(on_stream_clicked), u);
    gtk_box_pack_start(GTK_BOX(top), u->stream_btn, FALSE, FALSE, 0);

    /* One scoped provider is updated in place when the theme changes. */
    u->theme_css = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(u->theme_css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    wup_apply_theme(u);

    /* paired sections so columns stay balanced */
    GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    u->row1 = row1;
    u->row2 = row2;
    gtk_box_pack_start(GTK_BOX(page), row1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), row2, FALSE, FALSE, 0);
    u->src_frame = build_source_section(u);
    u->qual_frame = build_quality_section(u);
    gtk_box_pack_start(GTK_BOX(row1), build_conn_section(u), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row1), u->src_frame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row2), u->qual_frame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row2), build_audio_section(u), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(page), build_input_section(u), FALSE, FALSE, 0);
    /* Configure-event is emitted when window size changes. */
    g_signal_connect(u->window, "configure-event",
                     G_CALLBACK(on_settings_configure), u);

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
    if (u->theme_css) {
        GdkScreen *screen = gdk_screen_get_default();
        if (screen)
            gtk_style_context_remove_provider_for_screen(
                screen, GTK_STYLE_PROVIDER(u->theme_css));
        g_object_unref(u->theme_css);
    }
    free(u);
}
