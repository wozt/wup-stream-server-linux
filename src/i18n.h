#pragma once
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

typedef struct wup_i18n wup_i18n_t;
wup_i18n_t *wup_i18n_new(const char *requested_id);
void wup_i18n_free(wup_i18n_t *tr);
void wup_i18n_reload(wup_i18n_t *tr);
void wup_i18n_select(wup_i18n_t *tr, const char *id);
const char *wup_i18n_selected(const wup_i18n_t *tr);
const char *wup_i18n_lookup(const wup_i18n_t *tr, const char *english);
void wup_i18n_fill_combo(wup_i18n_t *tr, GtkComboBoxText *combo);
void wup_i18n_translate_tree(GtkWidget *widget, const wup_i18n_t *tr,
                             GtkWidget *skip_dynamic_status);
