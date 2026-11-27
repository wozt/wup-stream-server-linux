#define _DEFAULT_SOURCE
#include "camera.h"
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define W 640
#define H 480
#define PACKET 100000
struct camera_t {
    AVCodecContext *decoder;
    AVFrame *frame;
    AVPacket *packet;
    struct SwsContext *rgb, *yuv;
    int fd;
    time_t retry;
    unsigned char bgra[W*H*4], yuyv[W*H*2];
};
static void output_close(camera_t *c) {
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
}
void camera_free(camera_t *c) {
    if (!c) return;
    output_close(c);
    sws_freeContext(c->rgb); sws_freeContext(c->yuv);
    av_packet_free(&c->packet); av_frame_free(&c->frame);
    avcodec_free_context(&c->decoder); free(c);
}
camera_t *camera_new(void) {
    camera_t *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->fd = -1;
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec) c->decoder = avcodec_alloc_context3(codec);
    c->frame = av_frame_alloc(); c->packet = av_packet_alloc();
    if (!c->decoder || !c->frame || !c->packet) goto fail;
    c->decoder->thread_count = 1;
    c->decoder->flags |= AV_CODEC_FLAG_LOW_DELAY;
    if (avcodec_open2(c->decoder, codec, NULL) < 0) goto fail;
    return c;
fail:
    fprintf(stderr, "[camera] H264 decoder unavailable\n");
    camera_free(c); return NULL;
}
static void output_open(camera_t *c) {
    if (c->fd >= 0 || time(NULL) < c->retry) return;
    c->retry = time(NULL) + 5;
    const char *path = getenv("WUP_CAMERA_DEVICE");
    if (!path || !*path) path = "/dev/video42";
    int fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    struct v4l2_capability cap = {0};
    struct v4l2_format fmt = {0};
    if (fd < 0) goto fail;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0 ||
        strncmp((const char *)cap.card, "Wii U GamePad Camera", sizeof cap.card)) goto fail;
    unsigned caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_OUTPUT) || !(caps & V4L2_CAP_READWRITE)) goto fail;
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = W; fmt.fmt.pix.height = H;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0 || fmt.fmt.pix.width != W ||
        fmt.fmt.pix.height != H || fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV ||
        fmt.fmt.pix.bytesperline != W*2 || fmt.fmt.pix.sizeimage != W*H*2) goto fail;
    c->fd = fd; return;
fail:
    if (fd >= 0) close(fd);
    fprintf(stderr, "[camera] cannot announce %s; check v4l2loopback label and permissions\n", path);
}
const unsigned char *camera_poll(camera_t *c, wupstream *pad, int announce) {
    if (announce) output_open(c); else output_close(c);
    int fresh = 0;
    for (int i=0; i<2; ++i) {
        av_packet_unref(c->packet);
        if (av_new_packet(c->packet, PACKET) < 0) break;
        int n = wupstream_camera(pad, c->packet->data, PACKET);
        if (n <= 0) break;
        av_shrink_packet(c->packet, n);
        if (avcodec_send_packet(c->decoder, c->packet) < 0) continue;
        while (avcodec_receive_frame(c->decoder, c->frame) == 0) {
            AVFrame *f = c->frame;
            if (f->width != W || f->height != H) continue;
            c->rgb = sws_getCachedContext(c->rgb, W,H,f->format,W,H,AV_PIX_FMT_BGRA,SWS_FAST_BILINEAR,NULL,NULL,NULL);
            uint8_t *rgb[] = {c->bgra,NULL,NULL,NULL}; int rs[] = {W*4,0,0,0};
            if (c->rgb && sws_scale(c->rgb,(const uint8_t *const *)f->data,f->linesize,0,H,rgb,rs)==H) fresh=1;
            if (c->fd >= 0) {
                c->yuv = sws_getCachedContext(c->yuv,W,H,f->format,W,H,AV_PIX_FMT_YUYV422,SWS_FAST_BILINEAR,NULL,NULL,NULL);
                uint8_t *yuv[] = {c->yuyv,NULL,NULL,NULL}; int ys[] = {W*2,0,0,0};
                if (c->yuv && sws_scale(c->yuv,(const uint8_t *const *)f->data,f->linesize,0,H,yuv,ys)==H) {
                    ssize_t sent = write(c->fd,c->yuyv,sizeof c->yuyv);
                    if (sent != (ssize_t)sizeof c->yuyv && !(sent < 0 && (errno==EAGAIN || errno==EINTR))) output_close(c);
                }
            }
        }
    }
    return fresh ? c->bgra : NULL;
}
