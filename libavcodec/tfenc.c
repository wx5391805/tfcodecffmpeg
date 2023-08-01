#include "tfenc.h"

#include "libavutil/internal.h"

#include "avcodec.h"
#include "encode.h"
#include "internal.h"
#include "packet_internal.h"
#define OFFSET(x) offsetof(TfencContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "dev",      "device to be used for encoding", OFFSET(tf_dev), AV_OPT_TYPE_STRING, { .str = "/dev/al_enc" }, 0, 0, VE },
    { "rc",           "Override the preset rate-control",   OFFSET(rc),           AV_OPT_TYPE_INT,   { .i64 = RC_CBR },                                  -1, INT_MAX, VE, "rc" },
    { "vbr",          "Variable bitrate mode",              0,                    AV_OPT_TYPE_CONST, { .i64 = RC_VBR },                       0, 0, VE, "rc" },
    { "cbr",          "Constant bitrate mode",              0,                    AV_OPT_TYPE_CONST, { .i64 = RC_CBR },                       0, 0, VE, "rc" },
    
    { NULL }
};
const AVCodecHWConfigInternal *const ff_tfenc_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(CUDA,  CUDA),
    HW_CONFIG_ENCODER_DEVICE(NONE,  CUDA),
#if CONFIG_D3D11VA
    HW_CONFIG_ENCODER_FRAMES(D3D11, D3D11VA),
    HW_CONFIG_ENCODER_DEVICE(NONE,  D3D11VA),
#endif
    NULL,
};
const enum AVPixelFormat ff_tf_enc_pix_fmts[] = {
    AV_PIX_FMT_NV12,
    // AV_PIX_FMT_YUV420P,
    // AV_PIX_FMT_P010,
    // AV_PIX_FMT_YUV444P,
    // AV_PIX_FMT_P016,      // Truncated to 10bits
    // AV_PIX_FMT_YUV444P16, // Truncated to 10bits
    // AV_PIX_FMT_0RGB32,
    // AV_PIX_FMT_0BGR32,
    AV_PIX_FMT_NONE
};

static const AVCodecDefault defaults[] = {
    { "b", "2M" },
    { "qmin", "-1" },
    { "qmax", "-1" },
    { "qdiff", "-1" },
    { "qblur", "-1" },
    { "qcomp", "-1" },
    { "g", "50" },
    { "bf", "-1" },
    { "refs", "0" },
    { NULL },
};

static av_cold int tfenc_setup_surfaces(AVCodecContext *avctx)
{
    TfencContext *ctx = avctx->priv_data;
    int i, res = 0, res2;

    ctx->nb_buffers = 4;

    ctx->output_buffer_ready_queue = av_fifo_alloc(ctx->nb_buffers * sizeof(TfencBuffer*));
    if (!ctx->output_buffer_ready_queue)
        return AVERROR(ENOMEM);

    ctx->timestamp_list = av_fifo_alloc(ctx->nb_buffers * sizeof(int64_t));
    if (!ctx->timestamp_list)
        return AVERROR(ENOMEM);

    return res;
}

static int output_ready(AVCodecContext *avctx, int flush)
{
    TfencContext *ctx = avctx->priv_data;
    int nb_ready;

    nb_ready = av_fifo_size(ctx->output_buffer_ready_queue)   / sizeof(TfencBuffer*);
    return nb_ready;
}
static void ff_tfenc_callback(void *user_param, void *data, int len)
{
    AVCodecContext *avctx = user_param;
    TfencContext *ctx = avctx->priv_data;
    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);
    TfencBuffer *buffer = malloc(sizeof(TfencBuffer));
    buffer->data = malloc(len);
    buffer->len = len;

    memcpy(buffer->data,data,len);

    // int bufsize = output_ready(avctx,0);
    // av_log(avctx, AV_LOG_WARNING, "%s [in] %d\n", __func__,bufsize);

    // thread-safe? do1
    // out of range?

    pthread_mutex_lock(&ctx->mutex); 
    if(ctx->closed){
        av_log(avctx, AV_LOG_WARNING, "%s callback after end ,len %d\n", __func__,len);
        goto err;
    }
    av_fifo_generic_write(ctx->output_buffer_ready_queue, &buffer, sizeof(buffer), NULL);
err:
    pthread_mutex_unlock(&ctx->mutex); 
}
static av_cold int tfenc_setup_device(AVCodecContext *avctx)
{
    TfencContext *ctx = avctx->priv_data;

    tfenc_callback cb;

    /* encoding setting. */
    tfenc_setting setting;
    setting.pix_format = PIXFMT_NV12;
    setting.width = avctx->width;
    setting.height = avctx->height;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_H264:
        setting.profile = PROFILE_AVC_MAIN;
        break;
    case AV_CODEC_ID_HEVC:
        setting.profile = PROFILE_HEVC_MAIN;
        break;
    default:
        return AVERROR_BUG;
    }
    
    
    setting.level = 51;
    if(avctx->bit_rate > 0)
        setting.bit_rate = avctx->bit_rate;
    else
        setting.bit_rate = 400000*4;


    if(avctx->rc_max_rate > 0)
        setting.max_bit_rate = avctx->rc_max_rate;
    else
        setting.max_bit_rate = setting.bit_rate * 4;

    setting.gop = avctx->gop_size >= 0?avctx->gop_size:60;
    

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
        setting.frame_rate = avctx->framerate.num / avctx->framerate.den;
    } else {
        setting.frame_rate = avctx->time_base.den / (avctx->time_base.num * avctx->ticks_per_frame);
    }
    
    int device = 0;
    if(strcmp(ctx->tf_dev,"/dev/al_enc") == 0){
        device = 0;
    }else{
        sscanf(ctx->tf_dev,"/dev/al_enc%d",&device);
        device --;
    }
    setting.device_id = device;
    setting.rc_mode   = ctx->rc;



    /* callback parameter. */
    cb.param = avctx;
    cb.func = ff_tfenc_callback;
    // TF_HANDLE handle;
    int ret = 0;

    ret = tfenc_encoder_create(&ctx->handle, &setting, cb);

    if (ret != TFENC_SUCCESS){
        av_log(avctx, AV_LOG_ERROR, "create tfenc [%s] [%d] [%d] [%d] [%d] failed \n",ctx->tf_dev,setting.device_id,setting.gop,setting.rc_mode,setting.bit_rate);
        ctx->handle = NULL;
        return AVERROR_EXTERNAL;
    }else{
        av_log(avctx, AV_LOG_WARNING, "create tfenc [%s] [%d] [%d] [%d] [%d] success \n",ctx->tf_dev,setting.device_id,setting.gop,setting.rc_mode,setting.bit_rate);
        return 0;
    }
}
static av_cold int ff_tf_enc_init(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);
    TfencContext *ctx = avctx->priv_data;
    int ret = 0;
    if ((ret = tfenc_setup_device(avctx)) < 0)
        return ret;

    ctx->frame = av_frame_alloc();
    ctx->inputBuffer = malloc(avctx->width * avctx->height * 3 / 2);
    if (!ctx->frame)
        return AVERROR(ENOMEM);

    if ((ret = tfenc_setup_surfaces(avctx)) < 0)
        return ret;
    
    ctx->closed = 0;
    
    av_log(avctx, AV_LOG_WARNING, "%s [out] \n", __func__);
    return 0;
}
 
static av_cold int ff_tf_enc_close(AVCodecContext *avctx)
{
    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    TfencContext *ctx = avctx->priv_data;

    //TODO check thread safe
    //TODO check leaky output buffer // do1

    ctx->closed = 1;
    pthread_mutex_lock(&ctx->mutex);  

    TfencBuffer* out_buf;
    while (output_ready(avctx, avctx->internal->draining)) {
        av_fifo_generic_read(ctx->output_buffer_ready_queue, &out_buf, sizeof(out_buf), NULL);

        free(out_buf->data);
        free(out_buf);
    }

    av_fifo_freep(&ctx->output_buffer_ready_queue);
    pthread_mutex_unlock(&ctx->mutex);

    av_fifo_freep(&ctx->timestamp_list);
    free(ctx->inputBuffer); 

    if(ctx->handle)
        tfenc_encoder_destroy(ctx->handle);

    // av_log(avctx, AV_LOG_WARNING, "%s [out]\n", __func__);
 
    return 0;
}

static int avframe_get_size(AVCodecContext *avctx, AVFrame *frame) {
    int size = 0;
    int i;

    if (frame->format != AV_PIX_FMT_YUV420P && 
        frame->format != AV_PIX_FMT_NV12){
        av_log(avctx, AV_LOG_ERROR, "%s format is not I420 or nv12\n", __func__);
        return -1;
    }
    for (i = 0; i < AV_NUM_DATA_POINTERS; i++) {
        if (frame->data[i] != NULL) {
            int plane_lines = i == 0 ? frame->height : frame->height / 2;
            size += plane_lines * frame->linesize[i];
        }
    }

    return size;
}
static int copy_avframe_to_buffer(AVFrame *frame, uint8_t *buffer) {
    int y, u, v;
    uint8_t *dst = buffer;
    uint8_t *src_y = frame->data[0];
    uint8_t *src_u = frame->data[1];
    uint8_t *src_v = frame->data[2];
    int width = frame->width;
    int height = frame->height;

    // Copy Y plane
    for (y = 0; y < height; y++) {
        memcpy(dst, src_y, width);
        dst += width;
        src_y += frame->linesize[0];
    }

    if (frame->format == AV_PIX_FMT_YUV420P){
        //420 to NV12
        //TODO libyuv
        for (u = 0; u < height / 2; u++) {
            for (v = 0;v < frame->linesize[1]; v++){
                *dst++ = src_u[v];
                *dst++ = src_v[v];
            }
            src_u += frame->linesize[1];
            src_v += frame->linesize[1];
        }
    }else if (frame->format == AV_PIX_FMT_NV12){

        // Copy UV plane
        for (u = 0; u < height / 2; u++) {
            memcpy(dst, src_u, width);
            dst += width;
            src_u += frame->linesize[1];
        }
    }else{
        printf("%s format is not I420 or nv12\n", __func__);
    }
    return 0;

    // Copy Y plane
    for (y = 0; y < height; y++) {
        memcpy(dst, src_y, width);
        dst += width;
        src_y += frame->linesize[0];
    }

    // Copy U plane
    for (u = 0; u < height / 2; u++) {
        memcpy(dst, src_u, width / 2);
        dst += width / 2;
        src_u += frame->linesize[1];
    }

    // Copy V plane
    for (v = 0; v < height / 2; v++) {
        memcpy(dst, src_v, width / 2);
        dst += width / 2;
        src_v += frame->linesize[2];
    }

    return 0;
}

static inline void timestamp_queue_enqueue(AVFifoBuffer* queue, int64_t timestamp)
{
    av_fifo_generic_write(queue, &timestamp, sizeof(timestamp), NULL);
}

static inline int64_t timestamp_queue_dequeue(AVFifoBuffer* queue)
{
    int64_t timestamp = AV_NOPTS_VALUE;
    if (av_fifo_size(queue) > 0)
        av_fifo_generic_read(queue, &timestamp, sizeof(timestamp), NULL);

    return timestamp;
}
static int tfenc_send_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    TfencContext *ctx = avctx->priv_data;
    int res;
    uint8_t* input;
    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    if (frame && frame->buf[0]) {
        int frame_alloc_size = avframe_get_size(avctx,frame);

        input = (uint8_t*)ctx->inputBuffer;//malloc(frame_alloc_size);

        copy_avframe_to_buffer(frame,input);

        tfenc_process_frame(ctx->handle,input,frame_alloc_size);

        // printf("[wx]need size %d %d %d %d\n",frame_alloc_size,frame->width,frame->linesize[0],frame->height);

        timestamp_queue_enqueue(ctx->timestamp_list, frame->pts);

        // free(input);
    } else {

        //EOS
    }

    // av_log(avctx, AV_LOG_WARNING, "%s [out]\n", __func__);
    return 0;
}
static int tfenc_set_timestamp(AVCodecContext *avctx,
                               int pts,
                               AVPacket *pkt)
{
    TfencContext *ctx = avctx->priv_data;

    pkt->dts = timestamp_queue_dequeue(ctx->timestamp_list);
    pkt->pts = pkt->dts;//TODO correct tfenc pts, now use dts instead

    int frameIntervalP = 1;//tfenc hardcode
    pkt->dts -= FFMAX(frameIntervalP - 1, 0) * FFMAX(avctx->ticks_per_frame, 1) * FFMAX(avctx->time_base.num, 1);

    return 0;
}

static int process_output_buffer(AVCodecContext *avctx, AVPacket *pkt, TfencBuffer *buf)
{
    TfencContext *ctx = avctx->priv_data;

    int res = 0;

    enum AVPictureType pict_type;

    res = ff_get_encode_buffer(avctx, pkt, buf->len, 0);
    if (res < 0) {
        goto error;
    }
    memcpy(pkt->data, buf->data, buf->len);

    int maxlen = 1000;
    int nal_type;

    if(buf->len < maxlen)
        maxlen = buf->len;

    if(AV_CODEC_ID_H264 == avctx->codec->id) {
        nal_type = analyzeh264Frame(buf->data,maxlen);
    }else{
        nal_type = analyzeh265Frame(buf->data,maxlen);
    }

    if(nal_type) {
        pkt->flags |= AV_PKT_FLAG_KEY;
        pict_type = AV_PICTURE_TYPE_I;
        // printf("[wx] frameI %d\n",buf->len);
    }else{
        pict_type = AV_PICTURE_TYPE_P;
        // printf("[wx] frameP %d\n",buf->len);
    }

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->pict_type = pict_type;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    int fakeQP = 51;//TODO correct tfenc qp

    ff_side_data_set_encoder_stats(pkt,
        (fakeQP - 1) * FF_QP2LAMBDA, NULL, 0, pict_type);

    res = tfenc_set_timestamp(avctx, 0, pkt);
    if (res < 0)
        goto error2;
    
    return 0;

error:
    timestamp_queue_dequeue(ctx->timestamp_list);

error2:
    return res;
}
static int ff_tf_enc_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    TfencContext *ctx = avctx->priv_data;
    AVFrame *frame = ctx->frame;
    TfencBuffer *out_buf;
    int res;
    // av_log(avctx, AV_LOG_VERBOSE, "Not implement.\n");

    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    if (!frame->buf[0]) {
        res = ff_encode_get_frame(avctx, frame);
        if (res < 0 && res != AVERROR_EOF)
            return res;
    }

    res = tfenc_send_frame(avctx, frame);
    if (res < 0) {
        if (res != AVERROR(EAGAIN))
            return res;
    } else
        av_frame_unref(frame);

    if (output_ready(avctx, avctx->internal->draining)) {
        av_fifo_generic_read(ctx->output_buffer_ready_queue, &out_buf, sizeof(out_buf), NULL);

        res = process_output_buffer(avctx, pkt, out_buf);

        if (res)
            return res;

        free(out_buf->data);
        free(out_buf);
    } else if (avctx->internal->draining) {
        return AVERROR_EOF;
    } else {
        return AVERROR(EAGAIN);
    }

    return 0;
}
 

static av_cold void ff_tf_encode_flush(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    TfencContext *ctx = avctx->priv_data;

    tfenc_send_frame(avctx, NULL);
    av_fifo_reset(ctx->timestamp_list);
    av_log(avctx, AV_LOG_WARNING, "%s [out]\n", __func__);
}
static const AVClass h264_tfenc_class = {
    .class_name = "h264_tfenc",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};
AVCodec ff_h264_tfenc_encoder = {
    .name           = "h264_tfenc",
    .long_name      = NULL_IF_CONFIG_SMALL("TFENC H264 Encoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = ff_tf_enc_init,
    .receive_packet = ff_tf_enc_receive_packet,
    .close          = ff_tf_enc_close,
    .flush          = ff_tf_encode_flush,
    .priv_data_size = sizeof(TfencContext),
    .priv_class     = &h264_tfenc_class,
    .defaults       = defaults,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_ENCODER_FLUSH | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,    
    .pix_fmts       = ff_tf_enc_pix_fmts,
    .wrapper_name   = "tfenc",
};
static const AVClass hevc_tfenc_class = {
    .class_name = "hevc_tfenc",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};
AVCodec ff_hevc_tfenc_encoder = {
    .name           = "hevc_tfenc",
    .long_name      = NULL_IF_CONFIG_SMALL("TFENC Hevc Encoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HEVC,
    .init           = ff_tf_enc_init,
    .receive_packet = ff_tf_enc_receive_packet,
    .close          = ff_tf_enc_close,
    .flush          = ff_tf_encode_flush,
    .priv_data_size = sizeof(TfencContext),
    .priv_class     = &hevc_tfenc_class,
    .defaults       = defaults,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |
                      AV_CODEC_CAP_ENCODER_FLUSH | AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,    
    .pix_fmts       = ff_tf_enc_pix_fmts,
    .wrapper_name   = "tfenc",
};