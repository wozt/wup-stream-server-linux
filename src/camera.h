#pragma once
#include "wupstream.h"
typedef struct camera_t camera_t;
camera_t *camera_new(void);
void camera_free(camera_t *c);
const unsigned char *camera_poll(camera_t *c, wupstream *pad, int announce);
