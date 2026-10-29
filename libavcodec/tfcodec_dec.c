/*
 * ThinkForce TF7140 hardware video decoders (h264_tf, hevc_tf, mpeg2_tf, mpeg4_tf, vp8_tf, mjpeg_tf)
 * through the TFDL2 SDK decoder API (tfdec/libtfdec.h). The same build runs on the host against the vendor
 * libtfdec.so, or against any other library that provides the same API.
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
 * Model: packets go in with tfdec_enqueue_buffer(); the engine calls back with one decoded picture at a time
 * (contiguous I420 of the visible size) and expects tfdec_return_output() for it. Pictures are handed to the
 * caller without a copy (the engine buffer becomes the AVFrame's buffer) when the plane layout allows it.
 *
 * Everything the engine callback touches lives in TFDecShared, which is reference counted: the context holds
 * one reference and every zero-copy frame one more, so a frame may outlive the decoder; the session is destroyed
 * with the last reference. After close the callback hands pictures straight back.
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "tfdec/libtfdec.h"

#include "libavutil/fifo.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

typedef struct TFDecPicture {
    uint8_t *data;
    int size;
    uint64_t ts;
} TFDecPicture;

typedef struct TFDecShared {
    atomic_int refs;
    TFDEC_HANDLE h;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    AVFifo *pictures;       /* TFDecPicture, filled by the engine callback */
    int eos_received;
    int closed;
    int pictures_in;        /* pictures the callback has queued so far */
} TFDecShared;

typedef struct TFDecContext {
    const AVClass *class;
    TFDecShared *sh;
    AVPacket *pkt;          /* fetched but not yet accepted by the engine */
    int eos_pending;        /* input ended; EOS goes once the engine has taken every input buffer */
    int eos_sent;
    int input_slots;        /* free input slots of an idle session */
    int extradata_sent;
    int is_jpeg;
    int enqueued;           /* JPEG: images handed to the engine */
    int width, height;
    int role;
    /* options */
    int out_buffers;
    int zero_copy;
    char *device;
} TFDecContext;

static void shared_unref(TFDecShared *sh)
{
    if (atomic_fetch_sub(&sh->refs, 1) != 1)
        return;
    /* close drained the queue and marked it closed, so callbacks no longer queue anything */
    if (sh->h)
        tfdec_destroy(sh->h);
    av_fifo_freep2(&sh->pictures);
    pthread_cond_destroy(&sh->cv);
    pthread_mutex_destroy(&sh->mu);
    av_free(sh);
}

static void tfdec_callback(TFDEC_HANDLE h, void *buffer, int size, unsigned long timestamp, unsigned int flag,
                           void *user)
{
    TFDecShared *sh = user;
    int give_back = 1;

    pthread_mutex_lock(&sh->mu);
    if (!sh->closed && buffer && size > 0) {
        TFDecPicture p = { buffer, size, timestamp };
        if (av_fifo_write(sh->pictures, &p, 1) >= 0) {
            give_back = 0;
            sh->pictures_in++;
        }
    }
    if (flag & TFDEC_BUFFER_FLAG_EOS)
        sh->eos_received = 1;
    pthread_cond_broadcast(&sh->cv);
    pthread_mutex_unlock(&sh->mu);
    if (give_back && buffer)
        tfdec_return_output(h, buffer);
}

static void tfdec_release_picture(void *opaque, uint8_t *data)
{
    TFDecShared *sh = opaque;
    tfdec_return_output(sh->h, data);
    shared_unref(sh);
}

static int tfdec_open_session(AVCodecContext *avctx)
{
    TFDecContext *ctx = avctx->priv_data;
    TFDecShared *sh = av_mallocz(sizeof(*sh));
    if (!sh)
        return AVERROR(ENOMEM);
    atomic_init(&sh->refs, 1);
    pthread_mutex_init(&sh->mu, NULL);
    pthread_cond_init(&sh->cv, NULL);
    sh->pictures = av_fifo_alloc2(ctx->out_buffers, sizeof(TFDecPicture), AV_FIFO_FLAG_AUTO_GROW);
    if (!sh->pictures) {
        shared_unref(sh);
        return AVERROR(ENOMEM);
    }
    sh->h = tfdec_create(ctx->device, ctx->role, ctx->width, ctx->height, ctx->out_buffers, tfdec_callback, sh);
    if (!sh->h) {
        av_log(avctx, AV_LOG_ERROR, "tfdec_create(%s, %dx%d) failed\n", ctx->device, ctx->width, ctx->height);
        shared_unref(sh);
        return AVERROR_EXTERNAL;
    }
    ctx->sh = sh;
    ctx->eos_pending = ctx->eos_sent = ctx->extradata_sent = 0;
    /* tfdec_query_input_queue() does not dispatch on the session type and must not be called on a JPEG session
     * (it locks a video session's mutex at the wrong offset and hangs). */
    ctx->input_slots = ctx->is_jpeg ? 1 : tfdec_query_input_queue(sh->h);
    ctx->enqueued = 0;
    return 0;
}

static void tfdec_close_session(TFDecContext *ctx)
{
    TFDecShared *sh = ctx->sh;
    TFDecPicture p;
    if (!sh)
        return;
    ctx->sh = NULL;
    pthread_mutex_lock(&sh->mu);
    sh->closed = 1;
    while (av_fifo_read(sh->pictures, &p, 1) >= 0)
        tfdec_return_output(sh->h, p.data);
    pthread_mutex_unlock(&sh->mu);
    shared_unref(sh);
}

static av_cold int tfdec_init(AVCodecContext *avctx)
{
    TFDecContext *ctx = avctx->priv_data;

    switch (avctx->codec_id) {
    case AV_CODEC_ID_H264:       ctx->role = DECODER_H264; break;
    case AV_CODEC_ID_HEVC:       ctx->role = DECODER_HEVC; break;
    case AV_CODEC_ID_MPEG2VIDEO: ctx->role = DECODER_MPG2; break;
    case AV_CODEC_ID_MPEG4:      ctx->role = DECODER_MPG4; break;
    case AV_CODEC_ID_VP8:        ctx->role = DECODER_VP8;  break;
    case AV_CODEC_ID_MJPEG:      ctx->role = DECODER_JPEG; ctx->is_jpeg = 1; break;
    default:                     return AVERROR_BUG;
    }
    ctx->width  = avctx->width  ? avctx->width  : avctx->coded_width;
    ctx->height = avctx->height ? avctx->height : avctx->coded_height;
    if (ctx->width <= 0 || ctx->height <= 0) {
        av_log(avctx, AV_LOG_ERROR, "the engine needs the picture size before the first packet\n");
        return AVERROR(EINVAL);
    }
    avctx->width   = ctx->width;
    avctx->height  = ctx->height;
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->pkt = av_packet_alloc();
    if (!ctx->pkt)
        return AVERROR(ENOMEM);
    return tfdec_open_session(avctx);
}

static av_cold int tfdec_close(AVCodecContext *avctx)
{
    TFDecContext *ctx = avctx->priv_data;
    tfdec_close_session(ctx);
    av_packet_free(&ctx->pkt);
    return 0;
}

static void tfdec_flush(AVCodecContext *avctx)
{
    TFDecContext *ctx = avctx->priv_data;
    av_packet_unref(ctx->pkt);
    tfdec_close_session(ctx);
    if (tfdec_open_session(avctx) < 0)
        av_log(avctx, AV_LOG_ERROR, "could not reopen the engine session after a flush\n");
}

/* Turn one engine picture into a frame, without a copy when the planes are aligned like FFmpeg's own. */
static int tfdec_output(AVCodecContext *avctx, TFDecShared *sh, TFDecPicture *p, AVFrame *frame)
{
    TFDecContext *ctx = avctx->priv_data;
    const int w = ctx->width, h = ctx->height;
    const size_t luma = (size_t)w * h, chroma = (size_t)(w / 2) * (h / 2);
    int ret;

    if (p->size < (int)(luma + 2 * chroma)) {
        av_log(avctx, AV_LOG_ERROR, "picture of %d bytes is too small for %dx%d\n", p->size, w, h);
        tfdec_return_output(sh->h, p->data);
        return AVERROR_EXTERNAL;
    }
    frame->width  = w;
    frame->height = h;
    frame->format = AV_PIX_FMT_YUV420P;
    if (ctx->zero_copy && w % 64 == 0 && h % 2 == 0 && ((uintptr_t)p->data & 63) == 0
        && luma % 64 == 0 && chroma % 64 == 0) {
        atomic_fetch_add(&sh->refs, 1);
        frame->buf[0] = av_buffer_create(p->data, p->size, tfdec_release_picture, sh, 0);
        if (!frame->buf[0]) {
            tfdec_return_output(sh->h, p->data);
            shared_unref(sh);  /* the context still holds its own reference */
            return AVERROR(ENOMEM);
        }
        frame->data[0] = p->data;
        frame->data[1] = p->data + luma;
        frame->data[2] = p->data + luma + chroma;
        frame->linesize[0] = w;
        frame->linesize[1] = frame->linesize[2] = w / 2;
    } else {
        uint8_t *src[4] = { p->data, p->data + luma, p->data + luma + chroma, NULL };
        const int src_linesize[4] = { w, w / 2, w / 2, 0 };
        ret = ff_get_buffer(avctx, frame, 0);
        if (ret < 0) {
            tfdec_return_output(sh->h, p->data);
            return ret;
        }
        av_image_copy2(frame->data, frame->linesize, src, src_linesize, AV_PIX_FMT_YUV420P, w, h);
        tfdec_return_output(sh->h, p->data);
    }
    if (ctx->is_jpeg)
        frame->color_range = AVCOL_RANGE_JPEG;
    frame->pts     = (int64_t)p->ts;
    frame->pkt_dts = AV_NOPTS_VALUE;
    return 0;
}

static void wait_ms(TFDecShared *sh, int ms)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += (long)ms * 1000000;
    t.tv_sec  += t.tv_nsec / 1000000000;
    t.tv_nsec %= 1000000000;
    pthread_cond_timedwait(&sh->cv, &sh->mu, &t);
}

static int tfdec_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    TFDecContext *ctx = avctx->priv_data;
    TFDecShared *sh = ctx->sh;
    int idle_ms = 0, ret;

    if (!sh)
        return AVERROR_EXTERNAL;
    for (;;) {
        TFDecPicture p;
        pthread_mutex_lock(&sh->mu);
        if (av_fifo_read(sh->pictures, &p, 1) >= 0) {
            pthread_mutex_unlock(&sh->mu);
            return tfdec_output(avctx, sh, &p, frame);
        }
        if (sh->eos_received) {
            pthread_mutex_unlock(&sh->mu);
            return AVERROR_EOF;
        }
        pthread_mutex_unlock(&sh->mu);

        if (ctx->eos_sent) {
            /* draining: wait for the engine's remaining pictures and its end of stream */
            pthread_mutex_lock(&sh->mu);
            if (!av_fifo_can_read(sh->pictures) && !sh->eos_received)
                wait_ms(sh, 20);
            pthread_mutex_unlock(&sh->mu);
            if ((idle_ms += 20) > 10000) {
                av_log(avctx, AV_LOG_ERROR, "no end of stream from the engine within 10 s\n");
                return AVERROR_EXTERNAL;
            }
            continue;
        }

        if (ctx->eos_pending && ctx->is_jpeg) {
            /* The JPEG path has no end-of-stream signal: one picture per image, so it ends when every image
             * handed over has come back. */
            pthread_mutex_lock(&sh->mu);
            if (sh->pictures_in >= ctx->enqueued) {
                sh->eos_received = 1;
                pthread_mutex_unlock(&sh->mu);
                continue;
            }
            wait_ms(sh, 20);
            pthread_mutex_unlock(&sh->mu);
            if ((idle_ms += 20) > 10000) {
                av_log(avctx, AV_LOG_ERROR, "%d of %d images came back from the engine\n", sh->pictures_in,
                       ctx->enqueued);
                return AVERROR_EXTERNAL;
            }
            continue;
        }
        if (ctx->eos_pending) {
            /* An EOS queued behind unconsumed input made the engine drop the last pictures (2139 -> 2137
             * frames): wait until it has taken every input buffer, as tfcodecffmpeg did. */
            if (tfdec_query_input_queue(sh->h) < ctx->input_slots && idle_ms < 5000) {
                pthread_mutex_lock(&sh->mu);
                if (!av_fifo_can_read(sh->pictures))
                    wait_ms(sh, 2);
                pthread_mutex_unlock(&sh->mu);
                idle_ms += 2;
                continue;
            }
            ret = tfdec_enqueue_buffer(sh->h, NULL, 0, 0, TFDEC_BUFFER_FLAG_EOS);
            while (ret == TFDEC_STATUS_QUEUE_IS_FULL) {
                av_usleep(1000);
                ret = tfdec_enqueue_buffer(sh->h, NULL, 0, 0, TFDEC_BUFFER_FLAG_EOS);
            }
            if (ret != TFDEC_STATUS_SUCCESS)
                return AVERROR_EXTERNAL;
            ctx->eos_sent = 1;
            idle_ms = 0;
            continue;
        }
        if (!ctx->pkt->data) {
            ret = ff_decode_get_packet(avctx, ctx->pkt);
            if (ret == AVERROR_EOF) {
                ctx->eos_pending = 1;
                continue;
            }
            if (ret < 0)
                return ret;  /* EAGAIN: no packet yet, nothing decoded yet */
        }
        if (!ctx->extradata_sent) {
            /* MPEG-2/MPEG-4 headers may live only in extradata; the engine needs them in the stream. */
            ctx->extradata_sent = 1;
            if ((avctx->codec_id == AV_CODEC_ID_MPEG4 || avctx->codec_id == AV_CODEC_ID_MPEG2VIDEO)
                && avctx->extradata_size > 0
                && (ctx->pkt->size < avctx->extradata_size
                    || memcmp(ctx->pkt->data, avctx->extradata, avctx->extradata_size))) {
                ret = tfdec_enqueue_buffer(sh->h, avctx->extradata, avctx->extradata_size,
                                           (unsigned long)AV_NOPTS_VALUE, TFDEC_BUFFER_FLAG_ENDOFFRAME);
                if (ret != TFDEC_STATUS_SUCCESS)
                    av_log(avctx, AV_LOG_WARNING, "the engine did not take the stream headers (%d)\n", ret);
            }
        }
        ret = tfdec_enqueue_buffer(sh->h, ctx->pkt->data, ctx->pkt->size, (unsigned long)ctx->pkt->pts,
                                   TFDEC_BUFFER_FLAG_ENDOFFRAME);
        if (ret == TFDEC_STATUS_SUCCESS) {
            av_packet_unref(ctx->pkt);
            ctx->enqueued++;
            continue;
        }
        if (ret != TFDEC_STATUS_QUEUE_IS_FULL) {
            av_log(avctx, AV_LOG_ERROR, "the engine rejected a packet (%d)\n", ret);
            return AVERROR_EXTERNAL;
        }
        /* input full: a picture should come out soon */
        pthread_mutex_lock(&sh->mu);
        if (!av_fifo_can_read(sh->pictures) && !sh->eos_received)
            wait_ms(sh, 2);
        pthread_mutex_unlock(&sh->mu);
    }
}

#define OFFSET(x) offsetof(TFDecContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "out_buffers", "engine output buffers for the session", OFFSET(out_buffers), AV_OPT_TYPE_INT, { .i64 = 8 }, 1, 20, VD },
    { "zero_copy", "hand engine pictures over without a copy when the layout allows", OFFSET(zero_copy), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, VD },
    { "device", "decoder device", OFFSET(device), AV_OPT_TYPE_STRING, { .str = "/dev/mv500" }, 0, 0, VD },
    { NULL }
};

#define TFDEC(NAME, ID, BSFS, LONG) \
    static const AVClass tf_##NAME##_dec_class = { \
        .class_name = #NAME "_tf", \
        .item_name  = av_default_item_name, \
        .option     = options, \
        .version    = LIBAVUTIL_VERSION_INT, \
    }; \
    const FFCodec ff_##NAME##_tf_decoder = { \
        .p.name         = #NAME "_tf", \
        CODEC_LONG_NAME(LONG " (ThinkForce TF7140)"), \
        .p.type         = AVMEDIA_TYPE_VIDEO, \
        .p.id           = ID, \
        .priv_data_size = sizeof(TFDecContext), \
        .init           = tfdec_init, \
        .close          = tfdec_close, \
        FF_CODEC_RECEIVE_FRAME_CB(tfdec_receive_frame), \
        .flush          = tfdec_flush, \
        .p.priv_class   = &tf_##NAME##_dec_class, \
        .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING | AV_CODEC_CAP_HARDWARE, \
        .bsfs           = BSFS, \
        .p.wrapper_name = "tfcodec", \
        .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_NOT_INIT_THREADSAFE, \
    };

TFDEC(h264,  AV_CODEC_ID_H264,       "h264_mp4toannexb", "H.264")
TFDEC(hevc,  AV_CODEC_ID_HEVC,       "hevc_mp4toannexb", "HEVC")
TFDEC(mpeg2, AV_CODEC_ID_MPEG2VIDEO, NULL,               "MPEG-2 video")
TFDEC(mpeg4, AV_CODEC_ID_MPEG4,      NULL,               "MPEG-4 part 2")
TFDEC(vp8,   AV_CODEC_ID_VP8,        NULL,               "VP8")
TFDEC(mjpeg, AV_CODEC_ID_MJPEG,      NULL,               "Motion JPEG")
