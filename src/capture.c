#define _GNU_SOURCE
#include "capture.h"
#include "x11util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <limits.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <xcb/shm.h>
#include <xcb/xproto.h>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>

#define FW 854
#define FH 480

/* WUP_V912_CAPTURE_OPTIMIZATION: monotonic stages, per successful grab. */
static double capture_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ---------- aspect geometry ---------- */

static void aspect_geom(int sw, int sh, int aspect,
                        int *tw, int *th, int *ox, int *oy) {
    *ox = *oy = 0;
    if (aspect == ASPECT_STRETCH) {
        *tw = FW; *th = FH;
        return;
    }
    double sx = (double)FW / sw, sy = (double)FH / sh;
    if (aspect == ASPECT_LETTERBOX) {
        double k = sx < sy ? sx : sy;
        *tw = (int)(sw * k); *th = (int)(sh * k);
        if (*tw > FW) *tw = FW;
        if (*th > FH) *th = FH;
        *ox = (FW - *tw) / 2; *oy = (FH - *th) / 2;
    } else {
        double k = sx > sy ? sx : sy;
        *tw = (int)(sw * k + 0.5); *th = (int)(sh * k + 0.5);
        if (*tw < FW) *tw = FW;
        if (*th < FH) *th = FH;
    }
}

/* ---------- shm image setup (XCB MIT-SHM, same path as ffmpeg x11grab) ---------- */

static void shm_cleanup(capture_t *c) {
    if (c->use_shm) {
        xcb_shm_detach(c->xcb, c->shmseg);
        xcb_flush(c->xcb);
    }
    if (c->shmaddr && c->shmaddr != (void *)-1) shmdt(c->shmaddr);
    if (c->shmid >= 0) shmctl(c->shmid, IPC_RMID, NULL);
    c->shmaddr = NULL; c->shmid = -1; c->use_shm = 0; c->shmseg = 0;
}

static int setup_image(capture_t *c) {
    int w = c->src_w, h = c->src_h;
    if (w <= 0 || h <= 0 || w > INT_MAX / 4 ||
        (size_t)w > (SIZE_MAX - 4096) / 4 / (size_t)h) return -1;
    c->use_shm = 0;
    c->shmaddr = NULL; c->shmid = -1; c->shmseg = 0;

    /* get the xcb connection bound to the same Xlib display so XIDs match */
    c->xcb = XGetXCBConnection(c->dpy);
    if (!c->xcb) return c->capture_backend == 1 ? -1 : 0;
    /* Explicit GetImage mode never allocates or attaches MIT-SHM. */
    if (c->capture_backend == 1) return 0;

    /* check MIT-SHM presence */
    xcb_query_extension_cookie_t eck =
        xcb_query_extension(c->xcb, 7, "MIT-SHM");
    const xcb_query_extension_reply_t *ext =
        xcb_query_extension_reply(c->xcb, eck, NULL);
    if (!ext || !ext->present) { free((void *)ext); return 0; }

    size_t need = (size_t)w * h * 4 + 4096;
    c->shmid = shmget(IPC_PRIVATE, need, IPC_CREAT | 0600);
    if (c->shmid < 0) { free((void *)ext); return 0; }
    c->shmaddr = shmat(c->shmid, NULL, 0);
    if (c->shmaddr == (void *)-1) {
        shmctl(c->shmid, IPC_RMID, NULL);
        c->shmid = -1; c->shmaddr = NULL;
        free((void *)ext);
        return 0;
    }
    c->shmseg = xcb_generate_id(c->xcb);
    xcb_void_cookie_t ck = xcb_shm_attach(c->xcb, c->shmseg, c->shmid, 0);
    if (xcb_request_check(c->xcb, ck) != NULL) goto fail;
    free((void *)ext);
    c->shm_size = need;
    c->use_shm = 1;
    if (getenv("WUPSTREAMD_DEBUG"))
        fprintf(stderr, "xcb shm: shmid=%d seg=0x%x addr=%p %dx%d\n",
                c->shmid, c->shmseg, c->shmaddr, w, h);
    return 0;

fail:
    shmdt(c->shmaddr); shmctl(c->shmid, IPC_RMID, NULL);
    c->shmid = -1; c->shmaddr = NULL;
    free((void *)ext);
    return 0;
}

static void teardown_image(capture_t *c) {
    shm_cleanup(c);
}

static int setup_scaler(capture_t *c) {
    if (c->sws) { sws_freeContext(c->sws); c->sws = NULL; }
    free(c->tmp); c->tmp = NULL;
    int tw, th, ox, oy;
    aspect_geom(c->src_w, c->src_h, c->aspect, &tw, &th, &ox, &oy);
    c->tmp_w = tw; c->tmp_h = th;
    c->out_x = ox; c->out_y = oy; c->out_w = tw; c->out_h = th;
    c->sws = sws_getContext(c->src_w, c->src_h, AV_PIX_FMT_BGR0,
                            tw, th, AV_PIX_FMT_BGR0,
                            SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (!c->sws) return -1;
    /* The normal path scales directly into the final 854x480 BGRA frame.
     * An intermediate image is needed ONLY for center crop. */
    if (c->aspect != ASPECT_CROP) return 0;
    if (tw <= 0 || th <= 0 || (size_t)tw > SIZE_MAX / 4 / (size_t)th)
        return -1;
    c->tmp = malloc((size_t)tw * (size_t)th * 4);
    return c->tmp ? 0 : -1;
}

/* ---------- public ---------- */

int capture_start(capture_t *c, const settings_t *s) {
    memset(c, 0, sizeof *c);
    c->aspect = s->aspect;
    c->capture_backend = s->capture_backend;
    c->shmid = -1;
    c->dpy = XOpenDisplay(NULL);
    if (!c->dpy) return -1;
    Window root = DefaultRootWindow(c->dpy);

    if (s->source_type == SRC_SCREEN) {
        screen_t *sc = find_screen(s->source.output);
        if (!sc) goto fail;
        c->drawable = root;
        c->cap_x = sc->x; c->cap_y = sc->y;
        c->src_w = sc->w; c->src_h = sc->h;
    } else if (s->source_type == SRC_REGION) {
        if (s->source.w <= 0 || s->source.h <= 0) goto fail;
        c->drawable = root;
        c->cap_x = s->source.x; c->cap_y = s->source.y;
        c->src_w = s->source.w; c->src_h = s->source.h;
    } else {
        if (!s->source.window_id) goto fail;
        c->drawable = s->source.window_id;
        c->is_window = 1;
        c->win_id = s->source.window_id;
        int x, y, w, h;
        if (!get_window_geom(c->dpy, c->win_id, &x, &y, &w, &h)) goto fail;
        c->cap_x = 0; c->cap_y = 0;
        c->src_w = w; c->src_h = h;
        c->last_w = w; c->last_h = h; c->last_x = x; c->last_y = y;
    }

    if (setup_image(c) != 0 || setup_scaler(c) != 0) goto fail;
    return 0;

fail:
    capture_stop(c);
    return -1;
}

int capture_grab(capture_t *c, unsigned char *out) {
    const uint8_t *src_data;
    int src_stride;
    XImage *tmp_img = NULL;
    xcb_get_image_reply_t *xcb_img = NULL;
    double x11_beg = capture_seconds();

    if (c->use_shm) {
        /* XCB MIT-SHM get_image: pixels land directly in our shm segment */
        xcb_shm_get_image_cookie_t ck = xcb_shm_get_image(
            c->xcb, c->drawable, c->cap_x, c->cap_y,
            c->src_w, c->src_h, (uint32_t)~0u,
            XCB_IMAGE_FORMAT_Z_PIXMAP, c->shmseg, 0);
        xcb_generic_error_t *gerr = NULL;
        xcb_shm_get_image_reply_t *rep = xcb_shm_get_image_reply(c->xcb, ck, &gerr);
        if (!rep) {
            if (gerr) free(gerr);
            return -1;
        }
        free(rep);
        src_data = (const uint8_t *)c->shmaddr;
        src_stride = c->src_w * 4;
    } else if (c->capture_backend == 1) {
        /* Core X11 request; the reply owns the pixel storage until scaling ends. */
        if (c->cap_x < INT16_MIN || c->cap_x > INT16_MAX ||
            c->cap_y < INT16_MIN || c->cap_y > INT16_MAX ||
            c->src_w <= 0 || c->src_w > UINT16_MAX ||
            c->src_h <= 0 || c->src_h > UINT16_MAX) return -1;
        const xcb_setup_t *setup = xcb_get_setup(c->xcb);
        if (!setup || setup->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST)
            return -1;
        xcb_get_image_cookie_t ck = xcb_get_image(
            c->xcb, XCB_IMAGE_FORMAT_Z_PIXMAP, c->drawable,
            c->cap_x, c->cap_y, c->src_w, c->src_h, UINT32_MAX);
        xcb_generic_error_t *err = NULL;
        xcb_img = xcb_get_image_reply(c->xcb, ck, &err);
        if (err || !xcb_img) {
            free(err); free(xcb_img); return -1;
        }
        /* Existing scaler expects little-endian BGR0, four bytes per pixel. */
        int format_ok = 0;
        for (xcb_format_iterator_t it = xcb_setup_pixmap_formats_iterator(setup);
             it.rem; xcb_format_next(&it)) {
            if (it.data->depth == xcb_img->depth &&
                it.data->bits_per_pixel == 32 && it.data->scanline_pad == 32 &&
                (xcb_img->depth == 24 || xcb_img->depth == 32)) {
                format_ok = 1; break;
            }
        }
        size_t need = (size_t)c->src_w * c->src_h * 4;
        if (!format_ok || need > INT_MAX ||
            xcb_get_image_data_length(xcb_img) != (int)need) {
            free(xcb_img); return -1;
        }
        src_data = xcb_get_image_data(xcb_img);
        src_stride = c->src_w * 4;
    } else {
        tmp_img = XGetImage(c->dpy, c->drawable, c->cap_x, c->cap_y,
                            c->src_w, c->src_h, AllPlanes, ZPixmap);
        if (!tmp_img) return -1;
        src_data = (const uint8_t *)tmp_img->data;
        src_stride = tmp_img->bytes_per_line;
    }

    c->last_x11_ms = (capture_seconds() - x11_beg) * 1000.0;
    double scale_beg = capture_seconds();
    uint8_t *src_planes[4] = { (uint8_t *)src_data, NULL, NULL, NULL };
    int src_strides[4] = { src_stride, 0, 0, 0 };
    /* sws_scale supports destination row pitch; direct output avoids
     * copying every 854x480 row from an intermediate image. */
    if (c->aspect == ASPECT_LETTERBOX)
        memset(out, 0, (size_t)FW * FH * 4);
    uint8_t *dst = c->aspect == ASPECT_CROP ? c->tmp :
                   out + ((size_t)c->out_y * FW + c->out_x) * 4;
    uint8_t *dst_planes[4] = { dst, NULL, NULL, NULL };
    int dst_strides[4] = { (c->aspect == ASPECT_CROP ? c->tmp_w : FW) * 4, 0, 0, 0 };
    int scaled = sws_scale(c->sws, (const uint8_t *const *)src_planes, src_strides,
                           0, c->src_h, dst_planes, dst_strides);
    if (tmp_img) XDestroyImage(tmp_img);
    free(xcb_img);
    if (scaled != c->tmp_h) return -1;
    if (c->aspect == ASPECT_CROP) {
        int sx = (c->tmp_w - FW) / 2, sy = (c->tmp_h - FH) / 2;
        for (int y = 0; y < FH; y++)
            memcpy(out + (size_t)y * FW * 4,
                   c->tmp + ((size_t)(sy + y) * c->tmp_w + sx) * 4,
                   (size_t)FW * 4);
    }
    c->last_scale_ms = (capture_seconds() - scale_beg) * 1000.0;
    return 0;
}

int capture_follow(capture_t *c, const settings_t *s) {
    (void)s;
    if (!c->is_window || !c->win_id || !c->dpy) return 0;
    int x, y, w, h;
    if (!get_window_geom(c->dpy, c->win_id, &x, &y, &w, &h)) return -1;
    c->last_x = x; c->last_y = y;
    if (w != c->last_w || h != c->last_h) {
        c->last_w = w; c->last_h = h;
        c->src_w = w; c->src_h = h;
        teardown_image(c);
        if (setup_image(c) != 0 || setup_scaler(c) != 0) return -1;
        return 1;
    }
    return 0;
}

void capture_stop(capture_t *c) {
    if (c->sws) { sws_freeContext(c->sws); c->sws = NULL; }
    free(c->tmp); c->tmp = NULL;
    if (c->dpy) {
        teardown_image(c);
        XCloseDisplay(c->dpy);
        c->dpy = NULL;
    }
}

/* root-window rect of what is currently streamed (for touch->mouse mapping) */
void capture_target_rect(const capture_t *c, int *x, int *y, int *w, int *h) {
    if (c->is_window) {
        *x = c->last_x; *y = c->last_y; *w = c->last_w; *h = c->last_h;
    } else {
        *x = c->cap_x; *y = c->cap_y; *w = c->src_w; *h = c->src_h;
    }
}
