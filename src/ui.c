/* WUP_V8_UI_POLISH */
#define _GNU_SOURCE
#include "ui.h"
#include "x11util.h"
#include "select.h"
#include "input.h"
#include "server.h"
#include "viz.h"
#include "i18n.h" /* WUP_V6_JSON_LANGUAGES */
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
    GtkWidget *theme_combo;
    GPtrArray *themes; /* WUP_V5_JSON_THEMES */
    wup_i18n_t *i18n; /* WUP_V6_JSON_LANGUAGES */
    GtkWidget *language_combo;
    GtkWidget *brand_title, *theme_title, *language_title;
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

/* WUP_V5_JSON_THEMES: data-driven palettes; user-defined themes can override shipped IDs.
 * No CSS is read from JSON: only validated hexadecimal colors enter the provider.
 * The selected ID, not a numeric index, is persisted in settings.json. */
typedef struct {
    char *id, *name;
    char *ui[11];
    char *pad[8];
} wup_theme_t;

static const char *const wup_ui_keys[] = {
    "background", "panel", "entry", "border", "text", "muted",
    "accent", "button", "hover", "active", "negative"
};
static const char *const wup_pad_keys[] = {
    "background", "shell", "screen", "border",
    "text", "muted", "accent", "negative"
};

static void wup_theme_free(gpointer obj) {
    wup_theme_t *t = obj;
    if (!t) return;
    g_free(t->id); g_free(t->name);
    for (guint i=0; i<G_N_ELEMENTS(t->ui); i++) g_free(t->ui[i]);
    for (guint i=0; i<G_N_ELEMENTS(t->pad); i++) g_free(t->pad[i]);
    g_free(t);
}

static gboolean wup_hex_color(const char *s) {
    if (!s || strlen(s) != 7 || s[0] != '#') return FALSE;
    for (guint i=1; i<7; i++)
        if (!g_ascii_isxdigit(s[i])) return FALSE;
    return TRUE;
}

static gboolean wup_id_valid(const char *s) {
    if (!s || !*s || strlen(s) > 63) return FALSE;
    for (; *s; s++)
        if (!(g_ascii_islower(*s) || g_ascii_isdigit(*s) ||
              *s == '-' || *s == '_')) return FALSE;
    return TRUE;
}

static gboolean wup_read_colors(JsonObject *object, const char *const *keys,
                                 guint count, char **output) {
    if (!object) return FALSE;
    for (guint i=0; i<count; i++) {
        if (!json_object_has_member(object, keys[i])) return FALSE;
        JsonNode *n = json_object_get_member(object, keys[i]);
        if (!n || !JSON_NODE_HOLDS_VALUE(n) ||
            json_node_get_value_type(n) != G_TYPE_STRING) return FALSE;
        const char *s = json_node_get_string(n);
        if (!wup_hex_color(s)) return FALSE;
        output[i] = g_strdup(s);
    }
    return TRUE;
}

static wup_theme_t *wup_theme_read(const char *path) {
    JsonParser *parser = json_parser_new();
    GError *err = NULL;
    if (!json_parser_load_from_file(parser, path, &err)) {
        g_warning("Theme %s: %s", path, err ? err->message : "parse error");
        g_clear_error(&err); g_object_unref(parser);
        return NULL;
    }
    JsonNode *node = json_parser_get_root(parser);
    JsonObject *obj = node && JSON_NODE_HOLDS_OBJECT(node)
                    ? json_node_get_object(node) : NULL;
    const char *id = obj ? json_object_get_string_member_with_default(obj,"id",NULL) : NULL;
    const char *name = obj ? json_object_get_string_member_with_default(obj,"name",NULL) : NULL;
    if (!wup_id_valid(id) || !name || !*name || strlen(name) > 100) {
        g_warning("Theme %s: invalid id/name",path);
        g_object_unref(parser); return NULL;
    }
    JsonObject *colors = json_object_has_member(obj,"colors") ?
                         json_object_get_object_member(obj,"colors") : NULL;
    JsonObject *gamepad = json_object_has_member(obj,"gamepad") ?
                          json_object_get_object_member(obj,"gamepad") : NULL;
    wup_theme_t *t = g_new0(wup_theme_t,1);
    t->id=g_strdup(id); t->name=g_strdup(name);
    gboolean ok = wup_read_colors(colors, wup_ui_keys,
                         G_N_ELEMENTS(wup_ui_keys),t->ui) &&
                  wup_read_colors(gamepad,wup_pad_keys,
                         G_N_ELEMENTS(wup_pad_keys),t->pad);
    if (!ok) {
        g_warning("Theme %s: missing or invalid #RRGGBB color",path);
        wup_theme_free(t); t=NULL;
    }
    g_object_unref(parser);
    return t;
}

static gint wup_filename_order(gconstpointer a, gconstpointer b) {
    return g_strcmp0(a,b);
}

static void wup_scan_theme_dir(GPtrArray *themes, const char *folder) {
    if (!folder) return;
    GDir *dir = g_dir_open(folder, 0, NULL);
    if (!dir) return;
    GList *names = NULL;
    const char *name;
    while ((name=g_dir_read_name(dir))) {
        if (g_str_has_suffix(name,".json"))
            names = g_list_prepend(names, g_strdup(name));
    }
    g_dir_close(dir);
    names = g_list_sort(names,wup_filename_order);
    for (GList *it=names; it; it=it->next) {
        char *path = g_build_filename(folder,(const char*)it->data,NULL);
        wup_theme_t *t = wup_theme_read(path);
        if (t) {
            gboolean replaced=FALSE;
            for (guint i=0; i<themes->len; i++) {
                wup_theme_t *old = g_ptr_array_index(themes,i);
                if (g_strcmp0(old->id,t->id)==0) {
                    g_ptr_array_index(themes,i)=t;
                    wup_theme_free(old);
                    replaced=TRUE;
                    break;
                }
            }
            if (!replaced) g_ptr_array_add(themes,t);
        }
        g_free(path);
    }
    g_list_free_full(names,g_free);
}

static GPtrArray *wup_load_themes(void) {
    GPtrArray *themes = g_ptr_array_new_with_free_func(wup_theme_free);
    char *exe=g_file_read_link("/proc/self/exe",NULL);
    char *dir=exe ? g_path_get_dirname(exe) : NULL;
    char *dev=dir ? g_build_filename(dir,"..","data","themes",NULL) : NULL;
    char *local=g_build_filename("data","themes",NULL);
    /* Precedence, low -> high: system install, source tree, user folder. */
    const gchar *const *datadirs = g_get_system_data_dirs();
    for (guint i=0; datadirs && datadirs[i]; i++) {
        char *installed=g_build_filename(datadirs[i],"wupstreamd","themes",NULL);
        wup_scan_theme_dir(themes,installed);
        g_free(installed);
    }
    wup_scan_theme_dir(themes,dev);
    if (!dev || !g_file_test(dev,G_FILE_TEST_IS_DIR))
        wup_scan_theme_dir(themes,local);
    char *usr=g_build_filename(g_get_user_config_dir(),"wup-stream-serveur","themes",NULL);
    wup_scan_theme_dir(themes,usr);  /* user JSON takes precedence */
    g_free(usr);g_free(local);g_free(dev);g_free(dir);g_free(exe);
    return themes;
}

static wup_theme_t *wup_selected_theme(ui_t *u) {
    if (!u || !u->themes) return NULL;
    for (guint i=0; i<u->themes->len; i++) {
        wup_theme_t *t=g_ptr_array_index(u->themes,i);
        if (!g_strcmp0(t->id,u->s->theme_id)) return t;
    }
    return NULL;
}

static void wup_apply_theme(ui_t *u) {
    if (!u || !u->theme_css) return;
    wup_theme_t *t=wup_selected_theme(u);
    if (!t) return;
    const char *bg=t->ui[0],*panel=t->ui[1],*entry=t->ui[2],*border=t->ui[3];
    const char *fg=t->ui[4],*muted=t->ui[5],*accent=t->ui[6],*button=t->ui[7];
    const char *hover=t->ui[8],*active=t->ui[9];
    GString *css=g_string_new(NULL);
    g_string_append_printf(css,
        "window.wup-settings, .wup-settings { background-color:%s; color:%s; }"
        ".wup-settings * { font-size: 11px; }"
        ".wup-settings frame { background-color:%s; border:1px solid %s; border-radius:8px; "
        "margin:1px; padding:2px; }"
        ".wup-settings .wup-section-title { color:%s; font-weight:bold; padding:0 3px 0 0; }"
        ".wup-settings label { color:%s; padding:0; }"
        ".wup-settings label:disabled { color:%s; }",
        bg,fg,panel,border,accent,fg,muted);
    g_string_append_printf(css,
        ".wup-settings button { min-height:17px; padding:1px 6px; border:1px solid %s; "
        "border-radius:5px; background-image:none; background-color:%s; color:%s; }"
        ".wup-settings button:hover { background-color:%s; border-color:%s; }"
        ".wup-settings button:active, .wup-settings button:checked { background-color:%s; }"
        ".wup-settings button:disabled { color:%s; }"
        ".wup-settings .wup-icon-button { min-width:20px; padding:1px 3px; }",
        border,button,fg,hover,accent,active,muted);
    g_string_append_printf(css,
        ".wup-settings entry { min-height:17px; padding:1px 4px; background-color:%s; "
        "color:%s; border-radius:4px; border:1px solid %s; }"
        ".wup-settings entry:focus { border-color:%s; }"
        ".wup-settings entry selection { background-color:%s; color:%s; }"
        ".wup-settings spinbutton { min-height:17px; padding:0; }"
        ".wup-settings spinbutton entry { min-height:17px; padding:1px 3px; }"
        ".wup-settings spinbutton button { min-width:14px; min-height:17px; padding:0 1px; }"
        ".wup-settings combobox { min-height:17px; padding:0; }"
        ".wup-settings combobox button { min-height:17px; padding:0 4px; }"
        ".wup-settings checkbutton { min-height:17px; padding:0; }"
        ".wup-settings checkbutton check { min-width:14px; min-height:14px; "
        "border-radius:3px; background-color:%s; border:1px solid %s; color:%s; }"
        ".wup-settings checkbutton check:checked { background-color:%s; color:%s; }"
        ".wup-settings scrolledwindow { background-color:transparent; }"
        ".wup-settings image { color:%s; }",
        entry,fg,border,accent,active,fg,entry,border,fg,active,fg,fg);
    g_string_append_printf(css,
        ".wup-settings .wup-brand-title { font-size:12px; font-weight:bold; color:%s; }"
        ".wup-settings .wup-window-action { min-width:24px; min-height:22px; "
        "background-color:transparent; border-color:transparent; padding:1px; }"
        ".wup-settings .wup-window-action:hover { background-color:%s; }"
        ".wup-settings .wup-window-close:hover { background-color:%s; color:white; }",
        fg,hover,t->ui[10]);
    /* V8: Help popup body shares the live JSON theme, including light themes. */
    g_string_append_printf(css,
        ".wup-settings .wup-help textview, "
        ".wup-settings .wup-help textview text { background-color:%s; color:%s; }"
        ".wup-settings .wup-help scrolledwindow { border:1px solid %s; border-radius:6px; }",
        entry,fg,border);
    GError *err=NULL;
    gtk_css_provider_load_from_data(u->theme_css,css->str,-1,&err);
    if (err) {g_warning("Theme CSS: %s",err->message);g_clear_error(&err);}
    g_string_free(css,TRUE);
    if (u->viz_area) {
        pad_viz_set_palette(u->viz_area,
            t->pad[0],t->pad[1],t->pad[2],t->pad[3],
            t->pad[4],t->pad[5],t->pad[6],t->pad[7]);
    }
}

static void wup_theme_combo_changed(GtkComboBox *combo, ui_t *u) {
    const char *id=gtk_combo_box_get_active_id(combo);
    if (!id || !g_strcmp0(id,u->s->theme_id)) return;
    g_strlcpy(u->s->theme_id,id,sizeof(u->s->theme_id));
    wup_apply_theme(u);
    schedule_save(u);
}

static void wup_theme_refresh(ui_t *u) {
    if (!u || !u->theme_combo) return;
    g_signal_handlers_block_by_func(u->theme_combo,G_CALLBACK(wup_theme_combo_changed),u);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(u->theme_combo));
    if (u->themes) g_ptr_array_unref(u->themes);
    u->themes = wup_load_themes();
    for (guint i=0; i<u->themes->len; i++) {
        wup_theme_t *t=g_ptr_array_index(u->themes,i);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(u->theme_combo),t->id,wup_i18n_lookup(u->i18n,t->name));
    }
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(u->theme_combo),u->s->theme_id)) {
        if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(u->theme_combo),"ocean"))
            gtk_combo_box_set_active(GTK_COMBO_BOX(u->theme_combo),0);
        const char *id=gtk_combo_box_get_active_id(GTK_COMBO_BOX(u->theme_combo));
        if (id) g_strlcpy(u->s->theme_id,id,sizeof(u->s->theme_id));
    }
    g_signal_handlers_unblock_by_func(u->theme_combo,G_CALLBACK(wup_theme_combo_changed),u);
    wup_apply_theme(u);
}

static void wup_theme_reload_clicked(GtkButton *button,ui_t *u) {
    (void)button;
    wup_theme_refresh(u);
    schedule_save(u);
}

static GtkWidget *wup_theme_dropdown(ui_t *u) {
    GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
    GtkWidget *l=gtk_label_new("Theme:");
    u->theme_title=l;
    gtk_box_pack_start(GTK_BOX(bar),l,FALSE,FALSE,0);
    u->theme_combo=gtk_combo_box_text_new();
    gtk_widget_set_size_request(u->theme_combo,125,-1);
    gtk_widget_set_tooltip_text(u->theme_combo,"Live theme selection (JSON files)");
    gtk_box_pack_start(GTK_BOX(bar),u->theme_combo,FALSE,FALSE,0);
    GtkWidget *reload=gtk_button_new();
    GtkWidget *icon=gtk_image_new_from_icon_name("view-refresh-symbolic",GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(reload),icon);
    gtk_widget_set_tooltip_text(reload,"Reload JSON themes from disk");
    gtk_style_context_add_class(gtk_widget_get_style_context(reload),"wup-icon-button");
    gtk_box_pack_start(GTK_BOX(bar),reload,FALSE,FALSE,0);
    wup_theme_refresh(u);
    g_signal_connect(u->theme_combo,"changed",G_CALLBACK(wup_theme_combo_changed),u);
    g_signal_connect(reload,"clicked",G_CALLBACK(wup_theme_reload_clicked),u);
    return bar;
}

/* WUP_V6_JSON_LANGUAGES: data-driven locale picker beside the theme picker. */
static void wup_apply_language(ui_t *u) {
    if (!u || !u->i18n || !u->window) return;
    wup_i18n_translate_tree(u->window,u->i18n,u->status_lbl);
    if (u->viz_area) {
        const char *pad_words[] = {
            "WAITING FOR GAMEPAD", "GYRO (deg/s)", "TOUCH",
            "Start stream to read sensors", "WUP / LIVE INPUT", "ACCEL (g)"
        };
        const char *translated[6];
        for (int i=0; i<6; i++)
            translated[i]=wup_i18n_lookup(u->i18n,pad_words[i]);
        pad_viz_set_labels(u->viz_area,translated);
    }
    /* The theme dropdown is generated from theme JSON, not English widgets. */
    if (u->theme_combo) wup_theme_refresh(u);
    /* Dynamic status and stream button labels are regenerated by ui_refresh_status. */
    u->stream_icon_mode=-1;
    ui_refresh_status(u);
    gtk_window_set_title(GTK_WINDOW(u->window),
                         wup_i18n_lookup(u->i18n,"WUP Stream — Settings"));
}

static void wup_language_changed(GtkComboBox *combo,ui_t *u) {
    const char *id=gtk_combo_box_get_active_id(combo);
    if (!id || !g_strcmp0(id,u->s->language_id)) return;
    wup_i18n_select(u->i18n,id);
    g_strlcpy(u->s->language_id,wup_i18n_selected(u->i18n),
              sizeof(u->s->language_id));
    wup_apply_language(u);
    schedule_save(u);
}

static void wup_language_reload(GtkButton *button,ui_t *u) {
    (void)button;
    g_signal_handlers_block_by_func(u->language_combo,
                                    G_CALLBACK(wup_language_changed),u);
    wup_i18n_reload(u->i18n);
    wup_i18n_fill_combo(u->i18n,GTK_COMBO_BOX_TEXT(u->language_combo));
    g_signal_handlers_unblock_by_func(u->language_combo,
                                      G_CALLBACK(wup_language_changed),u);
    g_strlcpy(u->s->language_id,wup_i18n_selected(u->i18n),
              sizeof(u->s->language_id));
    wup_apply_language(u);
    schedule_save(u);
}

static GtkWidget *wup_language_dropdown(ui_t *u) {
    GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
    GtkWidget *label=gtk_label_new("Language:");
    u->language_title=label;
    gtk_box_pack_start(GTK_BOX(bar),label,FALSE,FALSE,0);
    u->language_combo=gtk_combo_box_text_new();
    gtk_widget_set_size_request(u->language_combo,112,-1);
    gtk_widget_set_tooltip_text(u->language_combo,
                                "Live language selection (JSON files)");
    gtk_box_pack_start(GTK_BOX(bar),u->language_combo,FALSE,FALSE,0);
    GtkWidget *reload=gtk_button_new();
    gtk_container_add(GTK_CONTAINER(reload),
        gtk_image_new_from_icon_name("view-refresh-symbolic",GTK_ICON_SIZE_MENU));
    gtk_widget_set_tooltip_text(reload,"Reload JSON languages from disk");
    gtk_style_context_add_class(gtk_widget_get_style_context(reload),
                                "wup-icon-button");
    gtk_box_pack_start(GTK_BOX(bar),reload,FALSE,FALSE,0);
    wup_i18n_fill_combo(u->i18n,GTK_COMBO_BOX_TEXT(u->language_combo));
    g_signal_connect(u->language_combo,"changed",
                     G_CALLBACK(wup_language_changed),u);
    g_signal_connect(reload,"clicked",G_CALLBACK(wup_language_reload),u);
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
    GtkWidget *dlg = gtk_dialog_new_with_buttons(wup_i18n_lookup(u->i18n,"Save command preset"),
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

/* WUP_V8_UI_POLISH: this dialog uses the same theme as the main window,
 * has no native GTK titlebar and is movable but deliberately not resizable. */
static gboolean wup_help_drag(GtkWidget *widget, GdkEventButton *event,
                              gpointer dialog) {
    (void)widget;
    if (event->button == GDK_BUTTON_PRIMARY && event->type == GDK_BUTTON_PRESS) {
        gtk_window_begin_move_drag(GTK_WINDOW(dialog), event->button,
                                   (gint)event->x_root, (gint)event->y_root,
                                   event->time);
        return TRUE;
    }
    return FALSE;
}

static void wup_help_close(GtkButton *button, gpointer dialog) {
    (void)button;
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);
}

static gboolean wup_help_key(GtkWidget *widget, GdkEventKey *event,
                             gpointer unused) {
    (void)unused;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_CLOSE);
        return TRUE;
    }
    return FALSE;
}

static void on_key_help(GtkButton *b, ui_t *u) {
    (void)b;
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

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        wup_i18n_lookup(u->i18n, "Key mapping help"),
        GTK_WINDOW(u->window), GTK_DIALOG_MODAL, NULL);
    gtk_window_set_decorated(GTK_WINDOW(dlg), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(dlg), FALSE);
    gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 465, 420);
    gtk_style_context_add_class(gtk_widget_get_style_context(dlg), "wup-settings");
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_style_context_add_class(gtk_widget_get_style_context(content), "wup-help");
    gtk_container_set_border_width(GTK_CONTAINER(content), 9);
    gtk_box_set_spacing(GTK_BOX(content), 7);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(content), bar, FALSE, FALSE, 0);
    GtkWidget *drag = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(drag), FALSE);
    gtk_widget_add_events(drag, GDK_BUTTON_PRESS_MASK);
    gtk_widget_set_hexpand(drag, TRUE);
    GtkWidget *title = gtk_label_new(wup_i18n_lookup(u->i18n, "Key mapping help"));
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "wup-section-title");
    gtk_container_add(GTK_CONTAINER(drag), title);
    gtk_box_pack_start(GTK_BOX(bar), drag, TRUE, TRUE, 0);
    g_signal_connect(drag, "button-press-event", G_CALLBACK(wup_help_drag), dlg);

    GtkWidget *close = gtk_button_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(close), "wup-window-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(close), "wup-window-close");
    gtk_widget_set_tooltip_text(close, wup_i18n_lookup(u->i18n, "_Close"));
    gtk_widget_set_size_request(close, 26, 23);
    if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "window-close-symbolic"))
        gtk_container_add(GTK_CONTAINER(close), gtk_image_new_from_icon_name(
            "window-close-symbolic", GTK_ICON_SIZE_MENU));
    else gtk_container_add(GTK_CONTAINER(close), gtk_label_new("×"));
    gtk_box_pack_end(GTK_BOX(bar), close, FALSE, FALSE, 0);
    g_signal_connect(close, "clicked", G_CALLBACK(wup_help_close), dlg);
    g_signal_connect(dlg, "key-press-event", G_CALLBACK(wup_help_key), NULL);

    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(sw, 440, 350);
    gtk_widget_set_vexpand(sw, TRUE);
    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(tv), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tv), 10);
    const char *help = wup_i18n_lookup(u->i18n, "Key mapping help body");
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)),
        !strcmp(help, "Key mapping help body") ? txt : help, -1);
    gtk_container_add(GTK_CONTAINER(sw), tv);
    gtk_box_pack_start(GTK_BOX(content), sw, TRUE, TRUE, 0);

    /* Do not show GTK's otherwise-empty native dialog footer. */
    GtkWidget *actions = gtk_dialog_get_action_area(GTK_DIALOG(dlg));
    gtk_widget_set_no_show_all(actions, TRUE);
    gtk_widget_hide(actions);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

/* WUP_V8_UI_POLISH: vector Wii U button glyphs, rather than Xbox PNG art.
 * Lookup order: build tree / working tree / XDG data installation paths.
 * A text fallback keeps the UI usable even without an SVG loader. */
static const char *btn_icon_files[18] = {
    "btn_a.svg", "btn_b.svg", "btn_x.svg", "btn_y.svg",
    "btn_left.svg", "btn_right.svg", "btn_up.svg", "btn_down.svg",
    "btn_zl.svg", "btn_zr.svg", "btn_l.svg", "btn_r.svg",
    "btn_plus.svg", "btn_minus.svg", "btn_home.svg",
    "btn_l3.svg", "btn_r3.svg", "btn_tv.svg"
};

static char *wup_wiiu_svg_path(const char *file) {
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    char *bindir = exe ? g_path_get_dirname(exe) : NULL;
    char *dev = bindir ? g_build_filename(bindir, "..", "data", "icons", "wiiu", file, NULL) : NULL;
    char *cwd = g_build_filename("data", "icons", "wiiu", file, NULL);
    char *user = g_build_filename(g_get_user_data_dir(), "wupstreamd", "icons", "wiiu", file, NULL);
    char *local = g_build_filename("/usr/local/share/wupstreamd/icons/wiiu", file, NULL);
    char *system = g_build_filename("/usr/share/wupstreamd/icons/wiiu", file, NULL);
    const char *paths[] = {dev, cwd, user, local, system, NULL};
    char *found = NULL;
    for (guint i = 0; i < G_N_ELEMENTS(paths) - 1; i++) {
        if (paths[i] && g_file_test(paths[i], G_FILE_TEST_IS_REGULAR)) {
            found = g_strdup(paths[i]);
            break;
        }
    }
    g_free(dev); g_free(cwd); g_free(user); g_free(local); g_free(system);
    g_free(bindir); g_free(exe);
    return found;
}

static GtkWidget *btn_icon_widget(int idx) {
    if (idx < 0 || idx >= 18) return gtk_label_new("?");
    char *path = wup_wiiu_svg_path(btn_icon_files[idx]);
    GdkPixbuf *pixbuf = path ? gdk_pixbuf_new_from_file_at_scale(path, 23, 23, TRUE, NULL) : NULL;
    g_free(path);
    GtkWidget *icon = NULL;
    if (pixbuf) {
        icon = gtk_image_new_from_pixbuf(pixbuf);
        g_object_unref(pixbuf);
    } else {
        icon = gtk_label_new(wbtn_names[idx]);
    }
    GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(cell, 26, 23);
    gtk_widget_set_halign(cell, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(cell, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(cell), icon, TRUE, TRUE, 0);
    gtk_widget_set_tooltip_text(cell, wbtn_names[idx]);
    return cell;
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
        /* WUP_V8_UI_POLISH: one aligned 6-column grid, fixed icon slots and
         * equal-width keyboard entries. Preserves all 17 + 8 mapping callbacks. */
        GtkWidget *kgrid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(kgrid), 2);
        gtk_grid_set_column_spacing(GTK_GRID(kgrid), 5);
        gtk_widget_set_hexpand(kgrid, TRUE);
        gtk_box_pack_start(GTK_BOX(kb), kgrid, FALSE, FALSE, 0);
        const char *titles[6] = {"Wii U", "Key", "Wii U", "Key", "Stick", "Key"};
        for (int j = 0; j < 6; j++) {
            GtkWidget *head = mk_label(titles[j]);
            gtk_widget_set_halign(head, GTK_ALIGN_START);
            gtk_grid_attach(GTK_GRID(kgrid), head, j, 0, 1, 1);
        }
        GtkSizeGroup *key_width = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
        for (int i = 0; i < 17; i++) { /* TV remains reserved. */
            const int column = (i / 9) * 2;
            const int row = (i % 9) + 1;
            gtk_grid_attach(GTK_GRID(kgrid), btn_icon_widget(i), column, row, 1, 1);
            GtkWidget *entry = gtk_entry_new();
            u->key_entries[i] = entry;
            gtk_entry_set_width_chars(GTK_ENTRY(entry), 7);
            gtk_entry_set_max_width_chars(GTK_ENTRY(entry), 10);
            gtk_widget_set_valign(entry, GTK_ALIGN_CENTER);
            gtk_widget_set_size_request(entry, 77, 22);
            gtk_entry_set_text(GTK_ENTRY(entry), u->s->input.keys[i]);
            g_signal_connect(entry, "changed", G_CALLBACK(on_key_edited), u);
            gtk_size_group_add_widget(key_width, entry);
            gtk_grid_attach(GTK_GRID(kgrid), entry, column + 1, row, 1, 1);
        }
        for (int i = 0; i < 8; i++) {
            GtkWidget *direction = gtk_label_new(stick_dir_names[i]);
            gtk_widget_set_halign(direction, GTK_ALIGN_CENTER);
            gtk_grid_attach(GTK_GRID(kgrid), direction, 4, i + 1, 1, 1);
            GtkWidget *entry = gtk_entry_new();
            u->stick_entries[i] = entry;
            gtk_entry_set_width_chars(GTK_ENTRY(entry), 7);
            gtk_entry_set_max_width_chars(GTK_ENTRY(entry), 10);
            gtk_widget_set_valign(entry, GTK_ALIGN_CENTER);
            gtk_widget_set_size_request(entry, 77, 22);
            gtk_entry_set_text(GTK_ENTRY(entry), u->s->input.stick_keys[i]);
            g_signal_connect(entry, "changed", G_CALLBACK(on_stick_key_edited), u);
            gtk_size_group_add_widget(key_width, entry);
            gtk_grid_attach(GTK_GRID(kgrid), entry, 5, i + 1, 1, 1);
        }
        g_object_unref(key_width);
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
    if (u->brand_title) gtk_widget_set_visible(u->brand_title,ev->width>=830);
    if (u->theme_title) gtk_widget_set_visible(u->theme_title,ev->width>=770);
    if (u->language_title) gtk_widget_set_visible(u->language_title,ev->width>=770);
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

/* GTK3 deprecated GtkStatusIcon but provides no replacement in GTK3 itself.
 * Keep the existing X11 system tray behavior; isolate this compatibility API,
 * rather than disabling deprecation warnings across the project. */
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
    wup_i18n_translate_tree(m, u->i18n, NULL);
    gtk_widget_show_all(m);
    GdkEvent *event = gtk_get_current_event();
    gtk_menu_popup_at_pointer(GTK_MENU(m), event);
    if (event) gdk_event_free(event);
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

/* WUP_V6_JSON_LANGUAGES: the native titlebar is replaced with three GTK controls.
 * The product badge is the draggable region; a double click toggles maximize.
 * Close keeps the existing tray/hidden-window behavior. */
static gboolean wup_header_press(GtkWidget *widget,GdkEventButton *event,gpointer data) {
    (void)widget;
    ui_t *u=data;
    if (event->button!=1) return FALSE;
    if (event->type==GDK_2BUTTON_PRESS) {
        if (gtk_window_is_maximized(GTK_WINDOW(u->window)))
            gtk_window_unmaximize(GTK_WINDOW(u->window));
        else gtk_window_maximize(GTK_WINDOW(u->window));
        return TRUE;
    }
    if (event->type==GDK_BUTTON_PRESS) {
        gtk_window_begin_move_drag(GTK_WINDOW(u->window),event->button,
                                   (int)event->x_root,(int)event->y_root,event->time);
        return TRUE;
    }
    return FALSE;
}
/* WUP_V7_WINDOW_EDGES */
/* Undecorated X11 windows have no WM-supplied resize border. Give all
 * eight directions small, invisible input strips and an appropriate
 * cursor. The corner strips are added last so they win hit-testing. */
static GdkWindowEdge wup_edge_for_zone(GtkWidget *zone) {
    return (GdkWindowEdge)(GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(zone), "wup-resize-edge")) - 1);
}

static GdkCursorType wup_cursor_for_edge(GdkWindowEdge edge) {
    switch (edge) {
    case GDK_WINDOW_EDGE_NORTH:      return GDK_TOP_SIDE;
    case GDK_WINDOW_EDGE_SOUTH:      return GDK_BOTTOM_SIDE;
    case GDK_WINDOW_EDGE_WEST:       return GDK_LEFT_SIDE;
    case GDK_WINDOW_EDGE_EAST:       return GDK_RIGHT_SIDE;
    case GDK_WINDOW_EDGE_NORTH_WEST: return GDK_TOP_LEFT_CORNER;
    case GDK_WINDOW_EDGE_NORTH_EAST: return GDK_TOP_RIGHT_CORNER;
    case GDK_WINDOW_EDGE_SOUTH_WEST: return GDK_BOTTOM_LEFT_CORNER;
    case GDK_WINDOW_EDGE_SOUTH_EAST: return GDK_BOTTOM_RIGHT_CORNER;
    default:                         return GDK_LEFT_PTR;
    }
}

static gboolean wup_resize_enter(GtkWidget *zone, GdkEventCrossing *event,
                                 gpointer data) {
    ui_t *u = data;
    if (!event->window) return FALSE;
    if (gtk_window_is_maximized(GTK_WINDOW(u->window))) {
        gdk_window_set_cursor(event->window, NULL);
        return FALSE;
    }
    GdkDisplay *display = gdk_window_get_display(event->window);
    GdkCursor *cursor = gdk_cursor_new_for_display(
        display, wup_cursor_for_edge(wup_edge_for_zone(zone)));
    gdk_window_set_cursor(event->window, cursor);
    if (cursor) g_object_unref(cursor);
    return FALSE;
}

static gboolean wup_resize_leave(GtkWidget *zone, GdkEventCrossing *event,
                                 gpointer data) {
    (void)zone;
    (void)data;
    if (event->window) gdk_window_set_cursor(event->window, NULL);
    return FALSE;
}

static gboolean wup_resize_press(GtkWidget *zone, GdkEventButton *event,
                                 gpointer data) {
    ui_t *u = data;
    if (event->button != GDK_BUTTON_PRIMARY ||
        event->type != GDK_BUTTON_PRESS ||
        gtk_window_is_maximized(GTK_WINDOW(u->window))) return FALSE;
    gtk_window_begin_resize_drag(GTK_WINDOW(u->window),
                                 wup_edge_for_zone(zone), event->button,
                                 (gint)event->x_root, (gint)event->y_root,
                                 event->time);
    return TRUE;
}

static void wup_add_resize_zone(GtkOverlay *overlay, ui_t *u,
                                GdkWindowEdge edge,
                                GtkAlign horizontal, GtkAlign vertical,
                                int width, int height) {
    GtkWidget *zone = gtk_event_box_new();
    /* Invisible event box receives pointer events, but paints no frame. */
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(zone), FALSE);
    gtk_widget_set_halign(zone, horizontal);
    gtk_widget_set_valign(zone, vertical);
    gtk_widget_set_size_request(zone, width, height);
    gtk_widget_add_events(zone, GDK_BUTTON_PRESS_MASK |
                                GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_object_set_data(G_OBJECT(zone), "wup-resize-edge",
                      GINT_TO_POINTER((int)edge + 1));
    g_signal_connect(zone, "button-press-event", G_CALLBACK(wup_resize_press), u);
    g_signal_connect(zone, "enter-notify-event", G_CALLBACK(wup_resize_enter), u);
    g_signal_connect(zone, "leave-notify-event", G_CALLBACK(wup_resize_leave), u);
    gtk_overlay_add_overlay(overlay, zone);
}

static void wup_install_resize_edges(GtkOverlay *overlay, ui_t *u) {
    /* 5px edges; 12px corners. Keeps the interactive surface compact. */
    wup_add_resize_zone(overlay, u, GDK_WINDOW_EDGE_NORTH,
                        GTK_ALIGN_FILL, GTK_ALIGN_START, -1, 5);
    wup_add_resize_zone(overlay, u, GDK_WINDOW_EDGE_SOUTH,
                        GTK_ALIGN_FILL, GTK_ALIGN_END, -1, 5);
    wup_add_resize_zone(overlay, u, GDK_WINDOW_EDGE_WEST,
                        GTK_ALIGN_START, GTK_ALIGN_FILL, 5, -1);
    wup_add_resize_zone(overlay, u, GDK_WINDOW_EDGE_EAST,
                        GTK_ALIGN_END, GTK_ALIGN_FILL, 5, -1);
    wup_add_resize_zone(overlay, u, GDK_WINDOW_EDGE_NORTH_WEST,
                        GTK_ALIGN_START, GTK_ALIGN_START, 12, 12);
    wup_add_resize_zone(overlay, u, GDK_WINDOW_EDGE_NORTH_EAST,
                        GTK_ALIGN_END, GTK_ALIGN_START, 12, 12);
    wup_add_resize_zone(overlay, u, GDK_WINDOW_EDGE_SOUTH_WEST,
                        GTK_ALIGN_START, GTK_ALIGN_END, 12, 12);
    wup_add_resize_zone(overlay, u, GDK_WINDOW_EDGE_SOUTH_EAST,
                        GTK_ALIGN_END, GTK_ALIGN_END, 12, 12);
}

static void wup_win_minimize(GtkButton *button,ui_t *u) {
    (void)button; gtk_window_iconify(GTK_WINDOW(u->window));
}
static void wup_win_maximize(GtkButton *button,ui_t *u) {
    (void)button;
    if (gtk_window_is_maximized(GTK_WINDOW(u->window)))
        gtk_window_unmaximize(GTK_WINDOW(u->window));
    else gtk_window_maximize(GTK_WINDOW(u->window));
}
static void wup_win_close(GtkButton *button,ui_t *u) {
    (void)button; gtk_widget_hide(u->window);
}
static GtkWidget *wup_header_brand(ui_t *u) {
    GtkWidget *event=gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(event),FALSE);
    gtk_widget_add_events(event,GDK_BUTTON_PRESS_MASK);
    GtkWidget *inner=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);
    GdkPixbuf *original=load_icon();
    if (original) {
        GdkPixbuf *small=gdk_pixbuf_scale_simple(original,18,18,GDK_INTERP_BILINEAR);
        if (small) {
            gtk_box_pack_start(GTK_BOX(inner),gtk_image_new_from_pixbuf(small),
                               FALSE,FALSE,0);
            g_object_unref(small);
        }
        g_object_unref(original);
    } else {
        gtk_box_pack_start(GTK_BOX(inner),
            gtk_image_new_from_icon_name("input-gaming-symbolic",GTK_ICON_SIZE_MENU),
            FALSE,FALSE,0);
    }
    GtkWidget *label=gtk_label_new("WUP Stream");
    u->brand_title=label;
    gtk_style_context_add_class(gtk_widget_get_style_context(label),"wup-brand-title");
    gtk_box_pack_start(GTK_BOX(inner),label,FALSE,FALSE,0);
    gtk_container_add(GTK_CONTAINER(event),inner);
    g_signal_connect(event,"button-press-event",G_CALLBACK(wup_header_press),u);
    gtk_widget_set_tooltip_text(event,"Drag to move · double click to maximize");
    return event;
}
static GtkWidget *wup_title_action(const char *icon_name,
                                    const char *fallback,const char *tip) {
    GtkWidget *button=gtk_button_new();
    if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(),icon_name))
        gtk_container_add(GTK_CONTAINER(button),
            gtk_image_new_from_icon_name(icon_name,GTK_ICON_SIZE_MENU));
    else gtk_container_add(GTK_CONTAINER(button),gtk_label_new(fallback));
    gtk_widget_set_tooltip_text(button,tip);
    gtk_style_context_add_class(gtk_widget_get_style_context(button),
                                "wup-window-action");
    gtk_widget_set_size_request(button,24,22);
    return button;
}
static void wup_header_controls(ui_t *u,GtkWidget *top) {
    GtkWidget *min=wup_title_action("window-minimize-symbolic","−","Minimize");
    GtkWidget *max=wup_title_action("window-maximize-symbolic","□","Maximize");
    GtkWidget *close=wup_title_action("window-close-symbolic","×","Close to tray");
    gtk_style_context_add_class(gtk_widget_get_style_context(close),
                                "wup-window-close");
    gtk_box_pack_end(GTK_BOX(top),close,FALSE,FALSE,0);
    gtk_box_pack_end(GTK_BOX(top),max,FALSE,FALSE,0);
    gtk_box_pack_end(GTK_BOX(top),min,FALSE,FALSE,0);
    g_signal_connect(min,"clicked",G_CALLBACK(wup_win_minimize),u);
    g_signal_connect(max,"clicked",G_CALLBACK(wup_win_maximize),u);
    g_signal_connect(close,"clicked",G_CALLBACK(wup_win_close),u);
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
    snprintf(buf, sizeof buf, "%s: %s",
             wup_i18n_lookup(u->i18n,"Status"),
             wup_i18n_lookup(u->i18n,server_status(u->srv)));
    gtk_label_set_text(GTK_LABEL(u->status_lbl), buf);
    if (u->stream_btn) {
        int active = st == SRV_STREAMING || st == SRV_WAITING_PAD;
        if (u->stream_icon_mode != active) {
            gtk_button_set_label(GTK_BUTTON(u->stream_btn),
                                 wup_i18n_lookup(u->i18n,active ? "Stop stream" : "Start stream"));
            button_symbol(u->stream_btn, active ? "media-playback-stop-symbolic"
                                                : "media-playback-start-symbolic");
            u->stream_icon_mode = active;
        }
    }
}

ui_t *ui_new(settings_t *s, server_t *srv) {
    ui_t *u = g_new0(ui_t, 1);
    u->s = s; u->srv = srv;
    u->i18n = wup_i18n_new(s->language_id);
    g_strlcpy(s->language_id,wup_i18n_selected(u->i18n),
              sizeof(s->language_id));

    u->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(u->window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(u->window),TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(u->window),
                                "wup-settings");
    u->stream_icon_mode = -1;
    gtk_window_set_title(GTK_WINDOW(u->window), "WUP Stream — Settings");
    {
        GdkPixbuf *ic = load_icon();
        if (ic) {
            gtk_window_set_icon(GTK_WINDOW(u->window), ic);
            gtk_window_set_default_icon(ic);
            g_object_unref(ic);
        }
    }
    gtk_window_set_default_size(GTK_WINDOW(u->window), 960, 682);
    g_signal_connect(u->window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    gtk_widget_set_size_request(u->window, 680, 530);

    /* WUP_V7: borderless main window + transparent hit-test edges. */
    GtkWidget *window_overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(u->window), window_overlay);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window_overlay), outer);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(outer), scroll, TRUE, TRUE, 0);
    wup_install_resize_edges(GTK_OVERLAY(window_overlay), u);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(page), 5);
    gtk_container_add(GTK_CONTAINER(scroll), page);

    /* WUP_V7: the *whole blank titlebar* is a move handle, not only
     * the small logo. Buttons and dropdowns still handle their clicks. */
    GtkWidget *title_events = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(title_events), FALSE);
    gtk_widget_add_events(title_events, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(title_events, "button-press-event",
                     G_CALLBACK(wup_header_press), u);
    gtk_box_pack_start(GTK_BOX(outer), title_events, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(outer), title_events, 0);
    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(top, 4);
    gtk_widget_set_margin_bottom(top, 2);
    gtk_container_add(GTK_CONTAINER(title_events), top);
    u->status_lbl = gtk_label_new("Status: ...");
    gtk_label_set_ellipsize(GTK_LABEL(u->status_lbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(u->status_lbl), 65);
    gtk_label_set_xalign(GTK_LABEL(u->status_lbl), 0);
    gtk_box_pack_start(GTK_BOX(top),wup_header_brand(u),FALSE,FALSE,5);
    gtk_box_pack_start(GTK_BOX(top), u->status_lbl, FALSE, FALSE, 0);
    gtk_widget_set_tooltip_text(u->status_lbl, "Current stream connection status");
    GtkWidget *themes = wup_theme_dropdown(u);  /* JSON themes, live dropdown */
    gtk_box_pack_start(GTK_BOX(top), themes, FALSE, FALSE, 3);
    GtkWidget *languages=wup_language_dropdown(u);
    gtk_box_pack_start(GTK_BOX(top),languages,FALSE,FALSE,3);
    GtkWidget *top_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(top), top_spacer, TRUE, TRUE, 0);
    u->stream_btn = gtk_button_new_with_label("Start stream");
    button_symbol(u->stream_btn, "media-playback-start-symbolic");
    g_signal_connect(u->stream_btn, "clicked", G_CALLBACK(on_stream_clicked), u);
    gtk_box_pack_start(GTK_BOX(top), u->stream_btn, FALSE, FALSE, 0);
    wup_header_controls(u,top);

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
    wup_apply_theme(u); /* visualizer exists now; theme it too */
    wup_apply_language(u); /* apply saved locale to constructed widgets */
    /* Configure-event is emitted when window size changes. */
    g_signal_connect(u->window, "configure-event",
                     G_CALLBACK(on_settings_configure), u);

    GdkPixbuf *pb = load_icon();
    /* Keep GtkStatusIcon compatibility confined to this short block. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    u->tray = gtk_status_icon_new();
    if (pb) {
        gtk_status_icon_set_from_pixbuf(u->tray, pb);
        g_object_unref(pb);
    } else {
        gtk_status_icon_set_from_icon_name(u->tray, "network-workgroup");
    }
    gtk_status_icon_set_tooltip_text(u->tray, "wupstreamd");
    G_GNUC_END_IGNORE_DEPRECATIONS
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
    if (u->themes) g_ptr_array_unref(u->themes);
    if (u->i18n) wup_i18n_free(u->i18n);
    if (u->theme_css) {
        GdkScreen *screen = gdk_screen_get_default();
        if (screen)
            gtk_style_context_remove_provider_for_screen(
                screen, GTK_STYLE_PROVIDER(u->theme_css));
        g_object_unref(u->theme_css);
    }
    free(u);
}
