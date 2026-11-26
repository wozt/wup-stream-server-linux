#pragma once
#include <gtk/gtk.h>
#include "i18n.h"
int kms_permissions_ready(void);
void kms_permissions_manage(GtkWindow *parent, const wup_i18n_t *tr);
int kms_permissions_ensure(GtkWindow *parent, const wup_i18n_t *tr);
