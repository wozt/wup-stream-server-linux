#pragma once
#include <gtk/gtk.h>
#include "i18n.h"
int kms_permissions_ready(void);
int kms_permissions_ensure(GtkWindow *parent, const wup_i18n_t *tr);
