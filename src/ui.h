#pragma once
#include "settings.h"
#include "server.h"

typedef struct ui_t ui_t;

ui_t *ui_new(settings_t *s, server_t *srv);
void ui_refresh_status(ui_t *u);   /* call periodically (e.g. 500ms timeout) */
void ui_free(ui_t *u);
