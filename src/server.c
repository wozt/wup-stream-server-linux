#define _DEFAULT_SOURCE
#include "server.h"
#include "capture.h"
#include "audio.h"
#include "input.h"
#include "x11util.h"
#include "wupstream.h"
#include <cairo/cairo.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#define FW 854
#define FH 480

#define CAP_MAX_FAILS 8        /* ffmpeg restart attempts before giving up on capture */
#define SESSION_CAP_TRIES 30   /* capture_start attempts per pad session (~1 min) */

struct server_t {
    settings_t *s;
    pthread_t th;
    int running;
    int want_stream;
    int state;
    int restart_capture;
    int reload_keys;
    int reconnect;
    int calib_gyro;
    char status[256];
    pthread_mutex_t mtx;
    wupstream_input_t last_in;
    int has_in;
};

static void set_status(server_t *srv, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pthread_mutex_lock(&srv->mtx);
    vsnprintf(srv->status, sizeof srv->status, fmt, ap);
    pthread_mutex_unlock(&srv->mtx);
    va_end(ap);
}

static int get_state(server_t *srv) {
    pthread_mutex_lock(&srv->mtx);
    int st = srv->state;
    pthread_mutex_unlock(&srv->mtx);
    return st;
}

static void apply_quality_env(const settings_t *s) {
    char v[64];
    snprintf(v, sizeof v, "%d", s->crf > 0 ? s->crf : 18);
    setenv("WUPSTREAM_CRF", v, 1);
    snprintf(v, sizeof v, "%d", s->kbps > 0 ? s->kbps : 10000);
    setenv("WUPSTREAM_KBPS", v, 1);
    /* low latency: libwupstream already presets veryfast+zerolatency;
       we additionally force no B-frames and zero lookahead explicitly */
    char x264[160] = "";
    if (s->low_latency)
        snprintf(x264, sizeof x264, "bframes=0:rc-lookahead=0:sync-lookahead=0:sliced-threads=1");
    if (s->gop > 0) {
        char k[32];
        snprintf(k, sizeof k, "keyint=%d", s->gop);
        if (x264[0]) strncat(x264, ":", sizeof(x264) - strlen(x264) - 1);
        strncat(x264, k, sizeof(x264) - strlen(x264) - 1);
    }
    if (x264[0]) setenv("WUPSTREAM_X264", x264, 1);
    else unsetenv("WUPSTREAM_X264");
}

/* static 854x480 BGRA test frame for "gamepad only" mode (no capture) */
static void build_test_frame(unsigned char *frame) {
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, FW, FH);
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 0.07, 0.08, 0.12);
    cairo_paint(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 44);
    cairo_set_source_rgb(cr, 0.35, 0.75, 1.0);
    const char *t1 = "GAMEPAD ONLY MODE";
    cairo_text_extents_t te;
    cairo_text_extents(cr, t1, &te);
    cairo_move_to(cr, (FW - te.width) / 2, FH / 2 - 24);
    cairo_show_text(cr, t1);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 22);
    cairo_set_source_rgb(cr, 0.65, 0.65, 0.72);
    const char *t2 = "no capture - inputs & sensors active";
    cairo_text_extents(cr, t2, &te);
    cairo_move_to(cr, (FW - te.width) / 2, FH / 2 + 26);
    cairo_show_text(cr, t2);
    cairo_destroy(cr);
    int stride = cairo_image_surface_get_stride(surf);
    const unsigned char *data = cairo_image_surface_get_data(surf);
    for (int y = 0; y < FH; y++)
        memcpy(frame + (size_t)y * FW * 4, data + (size_t)y * stride, (size_t)FW * 4);
    cairo_surface_destroy(surf);
}

/* TV button on the pad: advance to the next monitor (screen source only) */
static void cycle_monitor(server_t *srv, settings_t *s) {
    screen_t screens[16];
    int n = list_screens(screens, 16);
    if (n <= 1) return;
    int cur = -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(screens[i].name, s->source.output)) { cur = i; break; }
    int nxt = (cur + 1) % n;
    snprintf(s->source.output, sizeof s->source.output, "%s", screens[nxt].name);
    pthread_mutex_lock(&srv->mtx);
    srv->restart_capture = 1;
    pthread_mutex_unlock(&srv->mtx);
    settings_save(s);
    set_status(srv, "monitor: %s", screens[nxt].name);
}

/* WUP_V95_CLEAR_REGION: no selected pixels is an intentional black
 * frame, not a capture failure. Keep the encoder and input path running. */
static int region_is_cleared(const settings_t *s) {
    return s->source_type == SRC_REGION &&
           (s->source.w <= 0 || s->source.h <= 0);
}

/* start capture with retries; returns 0 on success */
static int capture_start_retry(capture_t *cap, const settings_t *s, server_t *srv) {
    for (int i = 0; i < SESSION_CAP_TRIES && srv->running && srv->want_stream; i++) {
        if (capture_start(cap, s) == 0) return 0;
        set_status(srv, "source unavailable, retrying (%d)...", i + 1);
        usleep(2000000);
    }
    return -1;
}

static void *server_thread(void *arg) {
    server_t *srv = arg;
    settings_t *s = srv->s;
    static unsigned char frame[FW * FH * 4];
    static int16_t audio_buf[2048 * 2];
    static int16_t mic_buf[2048];

    while (srv->running) {
        if (!srv->want_stream) {
            set_status(srv, "idle");
            srv->state = SRV_IDLE;
            usleep(200000);
            continue;
        }

        srv->state = SRV_WAITING_PAD;
        set_status(srv, "looking for GamePad...");

        apply_quality_env(s);
        wupstream *pad = wupstream_open(s->pad_ip[0] ? s->pad_ip : NULL, 8000);
        if (!pad) {
            set_status(srv, "no GamePad found, retrying...");
            usleep(2000000);
            continue;
        }
        set_status(srv, "GamePad connected, starting capture...");

        capture_t cap = {0};
        audio_out_t aout;
        audio_in_t ain;
        input_ctx_t *ictx = NULL;
        int have_aout = 0, have_ain = 0, cap_ok = 0, cap_fails = 0;

        int static_ok = 0;
        if (s->gamepad_only) {
            build_test_frame(frame);
            static_ok = 1;
        } else if (region_is_cleared(s)) {
            memset(frame, 0, FW * FH * 4);
            set_status(srv, "region cleared: black screen");
        } else if (capture_start_retry(&cap, s, srv) == 0)
            cap_ok = 1;
        else {
            /* source permanently unavailable: keep pad, retry slower via outer loop */
            wupstream_close(pad);
            usleep(5000000);
            continue;
        }

        if (s->audio_pc_to_pad && !s->gamepad_only && audio_out_start(&aout, s->audio_monitor) == 0)
            have_aout = 1;
        if (s->audio_pad_to_pc && !s->gamepad_only && audio_in_start(&ain) == 0)
            have_ain = 1;
        ictx = input_init(s);
        if (ictx && s->input.mode == INMODE_GAMEPAD && !input_gamepad_ok(ictx))
            set_status(srv, "GamePad: /dev/uinput unavailable, using kb/mouse mode");

        srv->state = SRV_STREAMING;
        set_status(srv, "streaming");

        double audio_acc = 0, audio_last_t = 0;
        int frame_count = 0;
        wupstream_input_t in;
        uint32_t prev_btns = 0;
        int lost = 0;

        while (srv->running && srv->want_stream) {
            pthread_mutex_lock(&srv->mtx);
            int rst = srv->restart_capture, rlk = srv->reload_keys, rc = srv->reconnect, cg = srv->calib_gyro;
            srv->restart_capture = srv->reload_keys = srv->reconnect = srv->calib_gyro = 0;
            pthread_mutex_unlock(&srv->mtx);
            if (rc) break;
            if (rst) {
                cap_fails = 0;
                capture_stop(&cap);
                /* An intentionally cleared region does not call XGetImage
                 * or retry capture. Video switches to black immediately. */
                cap_ok = !s->gamepad_only && !region_is_cleared(s) &&
                         (capture_start(&cap, s) == 0);
                if (!cap_ok && !s->gamepad_only && !region_is_cleared(s))
                    cap_fails = 1;
            }
            if (rlk && ictx) input_reload_keys(ictx, s);
            if (cg && ictx) { input_calib_start(ictx); set_status(srv, "gyro calibration: keep the pad flat and still..."); }

            /* gamepad-only mode: static test frame, no capture at all */
            if (s->gamepad_only) {
                if (cap_ok) { capture_stop(&cap); cap_ok = 0; }
                if (!static_ok) {
                    if (s->gamepad_only_black) memset(frame, 0, FW * FH * 4);
                    else build_test_frame(frame);
                    static_ok = 1;
                }
            } else if (region_is_cleared(s)) {
                /* Live fallback: still transmit frames, audio and inputs.
                 * Never count this as a failed capture or drop the pad. */
                if (cap_ok) { capture_stop(&cap); cap_ok = 0; }
                memset(frame, 0, FW * FH * 4);
                static_ok = 0;
                cap_fails = 0;
            } else {
                static_ok = 0;
                /* capture dead or failing: restart capture only, keep pad session */
                if (!cap_ok) {
                    if (++cap_fails > CAP_MAX_FAILS) { lost = 1; break; }
                    set_status(srv, "capture failing, restarting (%d/%d)...", cap_fails, CAP_MAX_FAILS);
                    usleep(400000);
                    cap_ok = (capture_start(&cap, s) == 0);
                    continue;
                }

                /* follow window size every ~30 frames */
                if ((frame_count++ % 30) == 0 && s->source_type == SRC_WINDOW) {
                    int fr = capture_follow(&cap, s);
                    if (fr < 0) {
                        /* window gone or unreadable: mark capture dead */
                        capture_stop(&cap);
                        cap_ok = 0; cap_fails = 1;
                        continue;
                    }
                }

                if (capture_grab(&cap, frame) != 0) {
                    /* capture died: restart it, don't drop the pad */
                    capture_stop(&cap);
                    cap_ok = 0; cap_fails++;
                    if (cap_fails > CAP_MAX_FAILS) { lost = 1; break; }
                    set_status(srv, "capture lost, restarting (%d/%d)...", cap_fails, CAP_MAX_FAILS);
                    continue;
                }
                cap_fails = 0;
            }

            /* keep the touch->mouse map on the streamed area */
            if (ictx) {
                if (cap_ok) {
                    int mx, my, mw, mh;
                    capture_target_rect(&cap, &mx, &my, &mw, &mh);
                    input_set_touch_map(ictx, mx, my, mw, mh);
                } else {
                    input_set_touch_map(ictx, 0, 0, 0, 0);
                }
            }

            /* inputs */
            if (wupstream_input(pad, &in) != 0) { lost = 1; break; }
            pthread_mutex_lock(&srv->mtx);
            srv->last_in = in; srv->has_in = 1;
            pthread_mutex_unlock(&srv->mtx);
            /* TV button: cycle monitors when streaming a full screen */
            if (((in.buttons ^ prev_btns) & WUPSTREAM_TV) &&
                (in.buttons & WUPSTREAM_TV) &&
                s->source_type == SRC_SCREEN && !s->gamepad_only)
                cycle_monitor(srv, s);
            prev_btns = in.buttons;

            if (ictx) {
                input_dispatch(ictx, s, &in);
                double gb[3];
                if (input_calib_poll(ictx, gb)) {
                    for (int i = 0; i < 3; i++) s->input.gyro_bias[i] = gb[i];
                    settings_save(s);
                    set_status(srv, "gyro calibrated (bias %.0f %.0f %.0f)", gb[0], gb[1], gb[2]);
                }
            }

            /* mic pad -> PC */
            if (have_ain && !s->gamepad_only) {
                int got = wupstream_mic(pad, mic_buf, 800);
                if (got > 0) audio_in_write(&ain, mic_buf, got);
            }

            /* canonical loop order: input -> frame -> audio */
            if (wupstream_frame(pad, frame, FW * 4, WUPSTREAM_BGRA) != 0) {
                lost = 1; break;
            }

            /* audio PC -> pad: paced by WALL CLOCK, not by video frames —
               a dropped/hitched frame must not starve the pad's speaker
               (capture2cloud decouples audio and video the same way) */
            if (have_aout && !s->gamepad_only) {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                double t = ts.tv_sec + ts.tv_nsec / 1e9;
                if (audio_last_t == 0) audio_last_t = t;
                audio_acc += (t - audio_last_t) * 48000.0;
                audio_last_t = t;
                if (audio_acc > 2000) audio_acc = 2000;   /* bound latency; audio_buf holds 2048 frames */
                int nf = (int)audio_acc;
                audio_acc -= nf;
                if (nf > 0) {
                    audio_out_read(&aout, audio_buf, nf);
                    wupstream_audio(pad, audio_buf, nf);
                }
                if (getenv("WUPSTREAMD_DEBUG") && (frame_count % 300) == 0)
                    fprintf(stderr, "[audio] ring fill %.0f%% (~%.0f ms)\n",
                            audio_out_level(&aout) * 100.0,
                            audio_out_level(&aout) * 500.0);
            }
        }

        capture_stop(&cap);
        if (have_aout) audio_out_stop(&aout);
        if (have_ain) audio_in_stop(&ain);
        if (ictx) input_free(ictx);
        wupstream_close(pad);

        if (lost) {
            /* pad powered off / link dropped: stop the stream cleanly instead
               of spinning in a reconnect loop */
            set_status(srv, "GamePad disconnected - stream stopped");
            srv->want_stream = 0;
        } else {
            set_status(srv, "stopped");
        }
        srv->state = SRV_STOPPED;
        while (srv->running && !srv->want_stream) usleep(200000);
        if (!srv->running) break;
    }
    srv->running = 0;
    return NULL;
}

server_t *server_new(settings_t *s) {
    server_t *srv = calloc(1, sizeof *srv);
    srv->s = s;
    srv->state = SRV_IDLE;
    snprintf(srv->status, sizeof srv->status, "ready");
    pthread_mutex_init(&srv->mtx, NULL);
    srv->running = 1;
    pthread_create(&srv->th, NULL, server_thread, srv);
    return srv;
}

void server_destroy(server_t *srv) {
    if (!srv) return;
    srv->want_stream = 0;
    srv->running = 0;
    pthread_join(srv->th, NULL);
    pthread_mutex_destroy(&srv->mtx);
    free(srv);
}

void server_request_stream(server_t *srv) { srv->want_stream = 1; }
void server_request_stop(server_t *srv) { srv->want_stream = 0; }
void server_restart_capture(server_t *srv) {
    pthread_mutex_lock(&srv->mtx);
    srv->restart_capture = 1;
    pthread_mutex_unlock(&srv->mtx);
}
void server_request_reconnect(server_t *srv) {
    pthread_mutex_lock(&srv->mtx);
    srv->reconnect = 1;
    pthread_mutex_unlock(&srv->mtx);
}
void server_apply_input_keys(server_t *srv) {
    pthread_mutex_lock(&srv->mtx);
    srv->reload_keys = 1;
    pthread_mutex_unlock(&srv->mtx);
}
void server_calib_gyro(server_t *srv) {
    pthread_mutex_lock(&srv->mtx);
    srv->calib_gyro = 1;
    pthread_mutex_unlock(&srv->mtx);
}

int server_state(server_t *srv) { return get_state(srv); }

int server_input_snapshot(server_t *srv, wupstream_input_t *out) {
    pthread_mutex_lock(&srv->mtx);
    int ok = srv->has_in;
    if (ok) *out = srv->last_in;
    pthread_mutex_unlock(&srv->mtx);
    return ok;
}

const char *server_status(server_t *srv) {
    static char buf[256];
    pthread_mutex_lock(&srv->mtx);
    snprintf(buf, sizeof buf, "%s", srv->status);
    pthread_mutex_unlock(&srv->mtx);
    return buf;
}
