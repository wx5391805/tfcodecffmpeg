/*
 * ThinkForce TF7140 hardware video encoders (h264_tf, hevc_tf) through the TFDL2 SDK encoder API
 * (tfenc/tfenc_api.h). The same build runs on the host against the vendor libtfenc.so (which talks to
 * tfenc_service), or against any other library that provides the same API.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * Model: tfenc_process_frame() takes one NV12 frame and returns once the engine has it; coded data comes back
 * through the callback in input order (the engine does not use B-frames), a zero-length callback marks the end
 * of stream after tfenc_process_frame(NULL, 0). Every I picture is an IDR with SPS/PPS (VPS for HEVC) in band.
 */

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "tfenc/tfenc_api.h"

#include "libavutil/fifo.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"

typedef struct TFEncChunk {
    uint8_t *data;
    int size;
} TFEncChunk;

typedef struct TFEncContext {
    const AVClass *class;
    TF_HANDLE h;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    AVFifo *chunks;         /* TFEncChunk, filled by the engine callback */
    AVFifo *pts;            /* int64_t, in input order */
    int eos_sent, eos_received, closed, mu_ready;
    uint8_t *frame_buf;
    size_t frame_size;
    AVFrame *frame;
    /* options */
    int rc;
    int profile;
} TFEncContext;

static void tfenc_on_output(void *user, void *data, int len)
{
    AVCodecContext *avctx = user;
    TFEncContext *ctx = avctx->priv_data;
    TFEncChunk c = { NULL, len };

    if (len > 0) {
        c.data = av_malloc(len);
        if (c.data)
            memcpy(c.data, data, len);
    }
    pthread_mutex_lock(&ctx->mu);
    if (len <= 0)
        ctx->eos_received = 1;
    else if (ctx->closed || !c.data || av_fifo_write(ctx->chunks, &c, 1) < 0)
        av_free(c.data);
    pthread_cond_broadcast(&ctx->cv);
    pthread_mutex_unlock(&ctx->mu);
}

static av_cold int tfenc_init(AVCodecContext *avctx)
{
    TFEncContext *ctx = avctx->priv_data;
    tfenc_setting st = { 0 };
    tfenc_callback cb = { tfenc_on_output, avctx };
    int ret;

    pthread_mutex_init(&ctx->mu, NULL);
    pthread_cond_init(&ctx->cv, NULL);
    ctx->mu_ready = 1;
    ctx->chunks = av_fifo_alloc2(8, sizeof(TFEncChunk), AV_FIFO_FLAG_AUTO_GROW);
    ctx->pts    = av_fifo_alloc2(16, sizeof(int64_t), AV_FIFO_FLAG_AUTO_GROW);
    ctx->frame  = av_frame_alloc();
    if (!ctx->chunks || !ctx->pts || !ctx->frame)
        return AVERROR(ENOMEM);
    if (avctx->width % 2 || avctx->height % 2) {
        av_log(avctx, AV_LOG_ERROR, "width and height must be even\n");
        return AVERROR(EINVAL);
    }
    ctx->frame_size = (size_t)avctx->width * avctx->height * 3 / 2;
    ctx->frame_buf  = av_malloc(ctx->frame_size);
    if (!ctx->frame_buf)
        return AVERROR(ENOMEM);

    st.pix_format   = PIXFMT_NV12;
    st.width        = avctx->width;
    st.height       = avctx->height;
    if (avctx->codec_id == AV_CODEC_ID_H264)
        st.profile = ctx->profile >= 0 ? ctx->profile : PROFILE_AVC_HIGH;
    else
        st.profile = PROFILE_HEVC_MAIN;
    st.level        = avctx->level > 0 ? avctx->level : 51;
    st.bit_rate     = avctx->bit_rate > 0 ? avctx->bit_rate : 4000000;
    st.max_bit_rate = avctx->rc_max_rate > 0 ? avctx->rc_max_rate : st.bit_rate * 2;
    st.gop          = avctx->gop_size > 0 ? avctx->gop_size : 60;
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
        st.frame_rate = (avctx->framerate.num + avctx->framerate.den / 2) / avctx->framerate.den;
    else if (avctx->time_base.num > 0)
        st.frame_rate = avctx->time_base.den / avctx->time_base.num;
    if (st.frame_rate == 0 || st.frame_rate > 240)
        st.frame_rate = 30;
    st.device_id    = 0;
    st.rc_mode      = ctx->rc;

    ret = tfenc_encoder_create(&ctx->h, &st, cb);
    if (ret != TFENC_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "tfenc_encoder_create failed (%d)\n", ret);
        ctx->h = NULL;
        return AVERROR_EXTERNAL;
    }
    av_log(avctx, AV_LOG_VERBOSE, "%dx%d %u b/s (max %u) gop %u %u fps rc %d\n", st.width, st.height,
           st.bit_rate, st.max_bit_rate, st.gop, st.frame_rate, st.rc_mode);
    return 0;
}

static av_cold int tfenc_close(AVCodecContext *avctx)
{
    TFEncContext *ctx = avctx->priv_data;
    TFEncChunk c;

    if (ctx->mu_ready) {
        pthread_mutex_lock(&ctx->mu);
        ctx->closed = 1;
        pthread_mutex_unlock(&ctx->mu);
    }
    if (ctx->h)
        tfenc_encoder_destroy(ctx->h);  /* no callback after this returns */
    ctx->h = NULL;
    while (ctx->chunks && av_fifo_read(ctx->chunks, &c, 1) >= 0)
        av_free(c.data);
    av_fifo_freep2(&ctx->chunks);
    av_fifo_freep2(&ctx->pts);
    av_frame_free(&ctx->frame);
    av_freep(&ctx->frame_buf);
    if (ctx->mu_ready) {
        pthread_cond_destroy(&ctx->cv);
        pthread_mutex_destroy(&ctx->mu);
        ctx->mu_ready = 0;
    }
    return 0;
}

/* NV12 or YUV420P frame -> tightly packed NV12 in frame_buf */
static void pack_nv12(AVCodecContext *avctx, const AVFrame *f, uint8_t *dst)
{
    const int w = avctx->width, h = avctx->height;
    uint8_t *uv = dst + (size_t)w * h;
    for (int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * w, f->data[0] + (size_t)y * f->linesize[0], w);
    if (f->format == AV_PIX_FMT_NV12) {
        for (int y = 0; y < h / 2; y++)
            memcpy(uv + (size_t)y * w, f->data[1] + (size_t)y * f->linesize[1], w);
    } else {
        for (int y = 0; y < h / 2; y++) {
            const uint8_t *u = f->data[1] + (size_t)y * f->linesize[1];
            const uint8_t *v = f->data[2] + (size_t)y * f->linesize[2];
            uint8_t *d = uv + (size_t)y * w;
            for (int x = 0; x < w / 2; x++) {
                d[2 * x]     = u[x];
                d[2 * x + 1] = v[x];
            }
        }
    }
}

/* An IDR (H.264) or IRAP (HEVC) slice anywhere in the access unit makes it a key frame. */
static int is_key(enum AVCodecID id, const uint8_t *p, int n)
{
    for (int i = 0; i + 3 < n; i++) {
        if (p[i] || p[i + 1] || p[i + 2] != 1)
            continue;
        int t = id == AV_CODEC_ID_H264 ? (p[i + 3] & 0x1f) : ((p[i + 3] >> 1) & 0x3f);
        if (id == AV_CODEC_ID_H264 ? t == 5 : (t >= 16 && t <= 21))
            return 1;
        if (id == AV_CODEC_ID_H264 ? t == 1 : t < 16)
            return 0;
        i += 3;
    }
    return 0;
}

static void wait_ms(TFEncContext *ctx, int ms)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += (long)ms * 1000000;
    t.tv_sec  += t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000;
    pthread_cond_timedwait(&ctx->cv, &ctx->mu, &t);
}

static int tfenc_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    TFEncContext *ctx = avctx->priv_data;
    int idle_ms = 0, ret;

    for (;;) {
        TFEncChunk c;
        pthread_mutex_lock(&ctx->mu);
        if (av_fifo_read(ctx->chunks, &c, 1) >= 0) {
            pthread_mutex_unlock(&ctx->mu);
            int64_t pts = AV_NOPTS_VALUE;
            av_fifo_read(ctx->pts, &pts, 1);
            ret = ff_get_encode_buffer(avctx, pkt, c.size, 0);
            if (ret >= 0) {
                memcpy(pkt->data, c.data, c.size);
                pkt->pts = pkt->dts = pts;
                if (is_key(avctx->codec_id, c.data, FFMIN(c.size, 4096)))
                    pkt->flags |= AV_PKT_FLAG_KEY;
            }
            av_free(c.data);
            return ret;
        }
        if (ctx->eos_received) {
            pthread_mutex_unlock(&ctx->mu);
            return AVERROR_EOF;
        }
        if (ctx->eos_sent) {
            wait_ms(ctx, 20);
            pthread_mutex_unlock(&ctx->mu);
            if ((idle_ms += 20) > 10000) {
                av_log(avctx, AV_LOG_ERROR, "no end of stream from the engine within 10 s\n");
                return AVERROR_EXTERNAL;
            }
            continue;
        }
        pthread_mutex_unlock(&ctx->mu);

        ret = ff_encode_get_frame(avctx, ctx->frame);
        if (ret == AVERROR_EOF) {
            ret = tfenc_process_frame(ctx->h, NULL, 0);
            if (ret != TFENC_SUCCESS)
                return AVERROR_EXTERNAL;
            ctx->eos_sent = 1;
            continue;
        }
        if (ret < 0)
            return ret;
        pack_nv12(avctx, ctx->frame, ctx->frame_buf);
        ret = av_fifo_write(ctx->pts, &ctx->frame->pts, 1);
        av_frame_unref(ctx->frame);
        if (ret < 0)
            return ret;
        ret = tfenc_process_frame(ctx->h, ctx->frame_buf, ctx->frame_size);
        if (ret != TFENC_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR, "tfenc_process_frame failed (%d)\n", ret);
            return AVERROR_EXTERNAL;
        }
    }
}

#define OFFSET(x) offsetof(TFEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "rc", "rate control", OFFSET(rc), AV_OPT_TYPE_INT, { .i64 = RC_CBR }, RC_CBR, RC_VBR, VE, .unit = "rc" },
    { "cbr", "constant bit rate", 0, AV_OPT_TYPE_CONST, { .i64 = RC_CBR }, 0, 0, VE, .unit = "rc" },
    { "vbr", "variable bit rate", 0, AV_OPT_TYPE_CONST, { .i64 = RC_VBR }, 0, 0, VE, .unit = "rc" },
    { "profile", "H.264 profile", OFFSET(profile), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, PROFILE_AVC_HIGH, VE, .unit = "profile" },
    { "baseline", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PROFILE_AVC_BASELINE }, 0, 0, VE, .unit = "profile" },
    { "main",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PROFILE_AVC_MAIN },     0, 0, VE, .unit = "profile" },
    { "high",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PROFILE_AVC_HIGH },     0, 0, VE, .unit = "profile" },
    { NULL }
};

static const FFCodecDefault defaults[] = {
    { "b", "4M" },
    { "g", "60" },
    { NULL },
};

#define TFENC(NAME, ID, LONG) \
    static const AVClass tf_##NAME##_enc_class = { \
        .class_name = #NAME "_tf", \
        .item_name  = av_default_item_name, \
        .option     = options, \
        .version    = LIBAVUTIL_VERSION_INT, \
    }; \
    const FFCodec ff_##NAME##_tf_encoder = { \
        .p.name         = #NAME "_tf", \
        CODEC_LONG_NAME(LONG " (ThinkForce TF7140)"), \
        .p.type         = AVMEDIA_TYPE_VIDEO, \
        .p.id           = ID, \
        .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE, \
        .priv_data_size = sizeof(TFEncContext), \
        CODEC_PIXFMTS(AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P), \
        .init           = tfenc_init, \
        FF_CODEC_RECEIVE_PACKET_CB(tfenc_receive_packet), \
        .close          = tfenc_close, \
        .defaults       = defaults, \
        .p.priv_class   = &tf_##NAME##_enc_class, \
        .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP, \
        .p.wrapper_name = "tfcodec", \
    };

TFENC(h264, AV_CODEC_ID_H264, "H.264")
TFENC(hevc, AV_CODEC_ID_HEVC, "HEVC")
