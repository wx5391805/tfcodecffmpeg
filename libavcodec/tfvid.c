#include "libavutil/buffer.h"
#include "libavutil/mathematics.h"
#include "libavutil/hwcontext.h"
#include "libavutil/fifo.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "decode.h"
#include "hwconfig.h"
#include "internal.h"

#include "tfdec/libtfdec.h"
#include <pthread.h>

typedef struct TfvidContext
{
    AVClass *avclass;
    void* handle;
    int *key_frame;
    char* tf_dev;
    int64_t prev_pts;
    int decoder_flushing;
    int nb_inbuffers;
    int nb_outbuffers;
    AVFifoBuffer *frame_queue;
    int codec_type;
    pthread_mutex_t mutex;
    volatile int closed;

    char *crop_expr;
    char *resize_expr;
    struct {
        int left;
        int top;
        int right;
        int bottom;
    } crop;

    struct {
        int width;
        int height;
    } resize;

} TfvidContext;
typedef struct TfvidParsedFrame
{
    void* data;
    int len;
    unsigned long timestamp;
    int iskey;
}TfvidParsedFrame;

#define OFFSET(x) offsetof(TfvidContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "surfaces", "Maximum tfdec output buffer to be used for decoding", OFFSET(nb_inbuffers), AV_OPT_TYPE_INT, { .i64 = 5 }, 0, INT_MAX, VD },
    { "surfaces", "Maximum output buffer to be used for decoding", OFFSET(nb_outbuffers), AV_OPT_TYPE_INT, { .i64 = 20 }, 0, INT_MAX, VD },
    { "dev",      "device to be used for decoding", OFFSET(tf_dev), AV_OPT_TYPE_STRING, { .str = "/dev/mv500" }, 0, 0, VD },
    { NULL }
};

static int tfvid_is_buffer_full(AVCodecContext *avctx)
{
    TfvidContext *ctx = avctx->priv_data;
    int delay = 0;

    return (av_fifo_size(ctx->frame_queue) / sizeof(TfvidParsedFrame)) + delay >= ctx->nb_outbuffers;
}
static void ff_tfdec_callback(TFDEC_HANDLE session, void * buffer, int size, unsigned long timestamp, unsigned int flag, void * user_data){
    AVCodecContext *avctx = user_data;
    TfvidContext *ctx = avctx->priv_data;
    // if(flag)
    //     av_log(avctx, AV_LOG_WARNING, "%s [in] flag %u %lu size %d \n", __func__,flag,(unsigned long)pthread_self(),av_fifo_size(ctx->frame_queue)/sizeof(TfvidParsedFrame));

    TfvidParsedFrame frame ={0};
    frame.len = size;
    frame.data = malloc(size);
    frame.timestamp = timestamp;
    frame.iskey = ((flag & (1<<20)) > 0);
    
    memcpy(frame.data,buffer,size);

    pthread_mutex_lock(&ctx->mutex);  
    if(ctx->closed)
        goto end;
    while(tfvid_is_buffer_full(avctx)){

        //'output buffer full warnning'
        av_log(avctx, AV_LOG_WARNING, "%s tfdec output queue is full %d \n",__func__,av_fifo_size(ctx->frame_queue)/ sizeof(TfvidParsedFrame));
        pthread_mutex_unlock(&ctx->mutex);

        usleep(1000);
        pthread_mutex_lock(&ctx->mutex);  
        if(ctx->closed)
            goto end;
    }
    av_fifo_generic_write(ctx->frame_queue, &frame, sizeof(TfvidParsedFrame), NULL);
    tfdec_return_output(session,buffer);
    frame.data = NULL;
end:
    free(frame.data);
    pthread_mutex_unlock(&ctx->mutex);

    // av_log(avctx, AV_LOG_WARNING, "%s [out] %p\n", __func__,frame.data);
}
static void tfvid_flush(AVCodecContext *avctx)
{
    TfvidContext *ctx = avctx->priv_data;
    av_log(avctx, AV_LOG_ERROR, "%s [in]\n",__func__);
        

    pthread_mutex_lock(&ctx->mutex);  

    while (av_fifo_size(ctx->frame_queue)) {
        TfvidParsedFrame parsed_frame;        
        av_fifo_generic_read(ctx->frame_queue, &parsed_frame, sizeof(TfvidParsedFrame), NULL);
        free(parsed_frame.data);
        av_log(avctx, AV_LOG_ERROR, "free reserved frame\n");
    }
    av_fifo_freep(&ctx->frame_queue);

    ctx->frame_queue = av_fifo_alloc(ctx->nb_outbuffers * sizeof(TfvidParsedFrame));
    if (!ctx->frame_queue) {
        av_log(avctx, AV_LOG_ERROR, "Failed to recreate frame queue on flush\n");
        return;
    }

    pthread_mutex_unlock(&ctx->mutex);  

    if(ctx->handle){
        tfdec_destroy(ctx->handle);
        ctx->handle = NULL;
    }


    int probed_width = avctx->coded_width ? avctx->coded_width : 1280;
    int probed_height = avctx->coded_height ? avctx->coded_height : 720;

    ctx->handle = tfdec_create(ctx->tf_dev, ctx->codec_type, 
                    probed_width, probed_height, 
                    ctx->nb_inbuffers, ff_tfdec_callback, avctx);
    if (!ctx->handle) {
        av_log(avctx, AV_LOG_ERROR, "create tfdec failed. %d x %d\n",probed_width,probed_height);
    }


    ctx->prev_pts = INT64_MIN;
    ctx->decoder_flushing = 0;

    av_log(avctx, AV_LOG_ERROR, "%s [out]\n",__func__);
}

static av_cold int tfvid_decode_end(AVCodecContext *avctx)
{
    TfvidContext *ctx = avctx->priv_data;

    ctx->closed = 1;

    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    // av_log(avctx, AV_LOG_ERROR, "end 0 %p %lu\n",ctx->handle,(unsigned long)pthread_self());

    pthread_mutex_lock(&ctx->mutex);  

    while (av_fifo_size(ctx->frame_queue)) {
        TfvidParsedFrame parsed_frame;        
        av_fifo_generic_read(ctx->frame_queue, &parsed_frame, sizeof(TfvidParsedFrame), NULL);
        free(parsed_frame.data);
        av_log(avctx, AV_LOG_ERROR, "free reserved frame\n");
    }
    av_fifo_freep(&ctx->frame_queue);
    
    pthread_mutex_unlock(&ctx->mutex);  


    //destroy will call callback, can't be in lock region
    if(ctx->handle){
        tfdec_destroy(ctx->handle);
        ctx->handle = NULL;
    }
    

    av_freep(&ctx->key_frame);
    // av_log(avctx, AV_LOG_WARNING, "%s [out]\n", __func__);
    return 0;
}
static av_cold int tfvid_decode_init(AVCodecContext *avctx)
{
    TfvidContext *ctx = avctx->priv_data;

    av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);
    
    uint8_t *extradata;
    int extradata_size;
    int ret = 0;

    enum AVPixelFormat pix_fmts[2] = { 
                                       AV_PIX_FMT_YUV420P,
                                       AV_PIX_FMT_NONE };

    int probed_width = avctx->coded_width ? avctx->coded_width : 1280;
    int probed_height = avctx->coded_height ? avctx->coded_height : 720;
    int probed_bit_depth = 8;

    const AVPixFmtDescriptor *probe_desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    if (probe_desc && probe_desc->nb_components)
        probed_bit_depth = probe_desc->comp[0].depth;

    // Accelerated transcoding scenarios with 'ffmpeg' require that the
    // pix_fmt be set to AV_PIX_FMT_CUDA early. The sw_pix_fmt, and the
    // pix_fmt for non-accelerated transcoding, do not need to be correct
    // but need to be set to something. We arbitrarily pick NV12.
    ret = ff_get_format(avctx, pix_fmts);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_get_format failed: %d\n", ret);
        return ret;
    }
    avctx->pix_fmt = ret;

    if (ctx->resize_expr && sscanf(ctx->resize_expr, "%dx%d",
                                   &ctx->resize.width, &ctx->resize.height) != 2) {
        av_log(avctx, AV_LOG_ERROR, "Invalid resize expressions\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (ctx->crop_expr && sscanf(ctx->crop_expr, "%dx%dx%dx%d",
                                 &ctx->crop.top, &ctx->crop.bottom,
                                 &ctx->crop.left, &ctx->crop.right) != 4) {
        av_log(avctx, AV_LOG_ERROR, "Invalid cropping expressions\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    
    int tfdec_codec_type;

    switch (avctx->codec->id) {
#if CONFIG_H264_TFVID_DECODER
    case AV_CODEC_ID_H264:
        tfdec_codec_type = DECODER_H264;
        break;
#endif
#if CONFIG_HEVC_TFVID_DECODER
    case AV_CODEC_ID_HEVC:
        tfdec_codec_type= DECODER_HEVC;
        break;
#endif
#if CONFIG_MJPEG_TFVID_DECODER
    case AV_CODEC_ID_MJPEG:
        tfdec_codec_type=  DECODER_JPEG;
        break;
#endif
#if CONFIG_MPEG2_TFVID_DECODER
    case AV_CODEC_ID_MPEG2VIDEO:
        tfdec_codec_type=  DECODER_MPG2;
        break;
#endif
#if CONFIG_MPEG4_TFVID_DECODER
    case AV_CODEC_ID_MPEG4:
        tfdec_codec_type=  DECODER_MPG4;
        break;
#endif
#if CONFIG_VP8_TFVID_DECODER
    case AV_CODEC_ID_VP8:
        tfdec_codec_type=  DECODER_VP8;
        break;
#endif
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid codec!\n");
        return AVERROR_BUG;
    }

    if (avctx->codec->bsfs) {
        const AVCodecParameters *par = avctx->internal->bsf->par_out;
        extradata = par->extradata;
        extradata_size = par->extradata_size;
    } else {
        extradata = avctx->extradata;
        extradata_size = avctx->extradata_size;
    }


    // ctx->cuparseinfo.ulMaxNumDecodeSurfaces = ctx->nb_surfaces;
    // ctx->cuparseinfo.ulMaxDisplayDelay = (avctx->flags & AV_CODEC_FLAG_LOW_DELAY) ? 0 : 4;
    // ctx->cuparseinfo.pUserData = avctx;
    // ctx->cuparseinfo.pfnSequenceCallback = cuvid_handle_video_sequence;
    // ctx->cuparseinfo.pfnDecodePicture = cuvid_handle_picture_decode;
    // ctx->cuparseinfo.pfnDisplayPicture = cuvid_handle_picture_display;

    ctx->handle = tfdec_create(ctx->tf_dev, tfdec_codec_type, 
                    probed_width, probed_height, 
                    ctx->nb_inbuffers, ff_tfdec_callback, avctx);
                    
    if (!ctx->handle) {
        av_log(avctx, AV_LOG_ERROR, "create tfdec failed. %d x %d on %s\n",probed_width,probed_height,ctx->tf_dev);
        return AVERROR(ENOMEM);
    }

    // set out buffers > input buffers, avoid the aka @'output buffer full warnning'
    ctx->nb_outbuffers = tfdec_query_input_queue(ctx->handle);
    ctx->frame_queue = av_fifo_alloc(ctx->nb_outbuffers * sizeof(TfvidParsedFrame));
    av_log(avctx, AV_LOG_WARNING, "decode queue init size %d, max %d \n",av_fifo_size(ctx->frame_queue),ctx->nb_outbuffers);
    if (!ctx->frame_queue) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    ctx->key_frame = av_mallocz(ctx->nb_outbuffers * sizeof(int));
    if (!ctx->key_frame) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    ctx->closed = 0;
    
    ctx->prev_pts = INT64_MIN;

    if (!avctx->pkt_timebase.num || !avctx->pkt_timebase.den)
        av_log(avctx, AV_LOG_WARNING, "Invalid pkt_timebase, passing timestamps as-is.\n");

    av_log(avctx, AV_LOG_WARNING, "%s [out] type %d %s\n", __func__,tfdec_codec_type,ctx->tf_dev);
    return 0;

error:
    tfvid_decode_end(avctx);
    return ret;
}
extern int analyzeh264Frame(uint8_t* data,int len);
static int tfvid_decode_packet(AVCodecContext *avctx, const AVPacket *avpkt)
{
    TfvidContext *ctx = avctx->priv_data;
    int ret = 0, eret = 0, is_flush = ctx->decoder_flushing;

    av_log(avctx, AV_LOG_TRACE, "tfvid_decode_packet\n");

    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    if (is_flush && avpkt && avpkt->size)
        return AVERROR_EOF;

    if (tfvid_is_buffer_full(avctx) && avpkt && avpkt->size)
        return AVERROR(EAGAIN);

    static int64_t pc = 0;
    int64_t timestamp;
    unsigned int flag = 0;
    int iskey = 0;
    if (avpkt && avpkt->size) {

        pc++;
        // av_log(avctx, AV_LOG_WARNING, "%s [out] len %d #%d\n", __func__,avpkt->size,pc);
        iskey = (analyzeh264Frame(avpkt->data,avpkt->size > 1000?1000:avpkt->size) > 0);
        if (avpkt->pts != AV_NOPTS_VALUE) {
            // cupkt.flags = CUVID_PKT_TIMESTAMP;
            if (avctx->pkt_timebase.num && avctx->pkt_timebase.den)
                timestamp = av_rescale_q(avpkt->pts, avctx->pkt_timebase, (AVRational){1, 10000000});
            else
                timestamp = avpkt->pts;
        }else{
            av_log(avctx, AV_LOG_WARNING, "No pts packet\n");
        }
    } else {
        flag = TFDEC_BUFFER_FLAG_EOS;
        ctx->decoder_flushing = 1;
    }
    flag |= (iskey?(1<<20):0);//TODO tfdec won't return flag now


    int ret2 = TFDEC_STATUS_SUCCESS;
    do{
        // usleep(1000*1);
        if(avpkt && avpkt->size)
            ret2 = tfdec_enqueue_buffer(ctx->handle, avpkt->data , avpkt->size, timestamp, flag);
        else
            ret2 = tfdec_enqueue_buffer(ctx->handle, NULL, 0, timestamp, flag);//TODO check ok

        if(ret2 == TFDEC_STATUS_QUEUE_IS_FULL){
            // av_log(avctx, AV_LOG_WARNING, "%s tfdec input queue full %d key:%d, #%d\n", __func__,ret2,flag,pc);
            usleep(1000*1);
        }
    }while(ret2 == TFDEC_STATUS_QUEUE_IS_FULL && !ctx->closed);

    // if(flag)
    //     av_log(avctx, AV_LOG_WARNING, "%s [out] %d key:%d, #%d\n", __func__,ret2,flag,pc);

    if(ret2 != TFDEC_STATUS_SUCCESS){
        av_log(avctx, AV_LOG_ERROR, "tfdec error %d\n",ret);
        if(ctx->closed)
            ret = AVERROR_EOF;
        else if(ret != TFDEC_STATUS_QUEUE_IS_FULL)
            ret = AVERROR_EXTERNAL;
    }

error:
    if (ret < 0)
        return ret;
    else if (is_flush)
        return AVERROR_EOF;
    else
        return 0;
}
static int tfvid_output_frame(AVCodecContext *avctx, AVFrame *frame)
{
    TfvidContext *ctx = avctx->priv_data;
    int ret = 0, eret = 0;

    // av_log(avctx, AV_LOG_TRACE, "tfvid_output_frame\n");
    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);

    if (ctx->decoder_flushing) {
        ret = tfvid_decode_packet(avctx, NULL);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    if (!tfvid_is_buffer_full(avctx)) {
        AVPacket pkt = {0};
        ret = ff_decode_get_packet(avctx, &pkt);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
        ret = tfvid_decode_packet(avctx, &pkt);
        av_packet_unref(&pkt);
        // cuvid_is_buffer_full() should avoid this.
        if (ret == AVERROR(EAGAIN))
            ret = AVERROR_EXTERNAL;
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    TfvidParsedFrame parsed_frame; 
    parsed_frame.data = NULL;

    if (av_fifo_size(ctx->frame_queue)) {

        const AVPixFmtDescriptor *pixdesc;       
        unsigned int pitch = 0;
        int offset = 0;
        int i;

        // av_log(avctx, AV_LOG_WARNING, "%s [in] read out %lu size %d \n", __func__,(unsigned long)pthread_self(),av_fifo_size(ctx->frame_queue)/sizeof(TfvidParsedFrame));

        pthread_mutex_lock(&ctx->mutex);
        av_fifo_generic_read(ctx->frame_queue, &parsed_frame, sizeof(TfvidParsedFrame), NULL);
        pthread_mutex_unlock(&ctx->mutex);
        // av_log(avctx, AV_LOG_WARNING, "read %p\n",parsed_frame.data);


        if (avctx->pix_fmt == AV_PIX_FMT_YUV420P) {
            unsigned int offset = 0;

            // frame->format = AV_PIX_FMT_YUV420P;

            pixdesc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
            
            ret = ff_get_buffer(avctx, frame, 0);
            // av_log(avctx, AV_LOG_WARNING, "[wx] frame width %d\n",frame->width);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "ff_get_buffer failed\n");
                goto error;
            }

            // frame->pix_fmt = AV_PIX_FMT_YUV420P;
            // frame->width = avctx->width;
            // frame->height = avctx->height;
            // if(!frame->buf[0]){
            //     ret = av_frame_get_buffer(frame, 32);
            // }
            // if (ret < 0)
            //     goto fail;
            // transfer_frame(frame,parsed_frame);

            /*
             * Note that the following logic would not work for three plane
             * YUV420 because the pitch value is different for the chroma
             * planes.
             */
            // for (i = 0; i < pixdesc->nb_components; i++) {
            //     frame->data[i]     = (uint8_t*)parsed_frame.data + offset;
            //     frame->linesize[i] = (pitch >> (i ? pixdesc->log2_chroma_v : 0));
            //     offset += frame->linesize[i] * (avctx->height >> (i ? pixdesc->log2_chroma_h : 0));
            // }

            int dstOffset = 0,srcOffset = 0;
            for(int i =0 ;i < frame->height;i++){
                memcpy(frame->data[0] + dstOffset,parsed_frame.data + srcOffset,frame->width);
                dstOffset += frame->linesize[0];
                srcOffset += frame->width;
            }
            dstOffset = 0;
            for(int i =0 ;i < frame->height/2;i++){
                memcpy(frame->data[1] + dstOffset,parsed_frame.data + srcOffset,frame->width / 2);
                dstOffset += frame->linesize[1];
                srcOffset += frame->width/2;
            }
            dstOffset = 0;
            for(int i =0 ;i < frame->height/2;i++){
                memcpy(frame->data[2] + dstOffset,parsed_frame.data + srcOffset,frame->width / 2);
                dstOffset += frame->linesize[2];
                srcOffset += frame->width/2;
            }

        } else {
            av_log(avctx, AV_LOG_ERROR, "[wx]not I420\n");
            ret = AVERROR_BUG;
            goto error;
        }

        frame->key_frame = parsed_frame.iskey;
        if(frame->key_frame){
            av_log(avctx, AV_LOG_WARNING, "[wx]key\n");
        }
        if (avctx->pkt_timebase.num && avctx->pkt_timebase.den)
            frame->pts = av_rescale_q(parsed_frame.timestamp, (AVRational){1, 10000000}, avctx->pkt_timebase);
        else
            frame->pts = parsed_frame.timestamp;

    } else if (ctx->decoder_flushing) {
        ret = AVERROR_EOF;
    } else {
        ret = AVERROR(EAGAIN);
    }

error:

    // av_log(avctx, AV_LOG_WARNING, "free %p\n",parsed_frame.data);
    free(parsed_frame.data);

    if (ret < 0)
        av_frame_unref(frame);
    return ret;
}
static int tfvid_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    TfvidContext *ctx = avctx->priv_data;
    AVFrame *frame = data;
    int ret = 0;

    av_log(avctx, AV_LOG_TRACE, "tfvid_decode_frame\n");
    // av_log(avctx, AV_LOG_WARNING, "%s [in]\n", __func__);


    if (!ctx->decoder_flushing) {
        ret = tfvid_decode_packet(avctx, avpkt);
        if (ret < 0)
            return ret;
    }

    ret = tfvid_output_frame(avctx, frame);

    // av_log(avctx, AV_LOG_WARNING, "%s [out] %d\n", __func__);

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        *got_frame = 0;
    } else if (ret < 0) {
        return ret;
    } else {
        *got_frame = 1;
    }

    return 0;
}

static const AVCodecHWConfigInternal *const tfvid_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public = {
            .pix_fmt     = AV_PIX_FMT_YUV420P,
            .methods     = AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX |
                           AV_CODEC_HW_CONFIG_METHOD_INTERNAL,
            .device_type = AV_HWDEVICE_TYPE_CUDA
        },
        .hwaccel = NULL,
    },
    NULL
};
#define DEFINE_TFVID_CODEC(x, X, bsf_name) \
    static const AVClass x##_tfvid_class = { \
        .class_name = #x "_tfvid", \
        .item_name = av_default_item_name, \
        .option = options, \
        .version = LIBAVUTIL_VERSION_INT, \
    }; \
    AVCodec ff_##x##_tfvid_decoder = { \
        .name           = #x "_tfvid", \
        .long_name      = NULL_IF_CONFIG_SMALL("Thinkforce TFVID " #X " decoder"), \
        .type           = AVMEDIA_TYPE_VIDEO, \
        .id             = AV_CODEC_ID_##X, \
        .priv_data_size = sizeof(TfvidContext), \
        .priv_class     = &x##_tfvid_class, \
        .init           = tfvid_decode_init, \
        .close          = tfvid_decode_end, \
        .decode         = tfvid_decode_frame, \
        .receive_frame  = tfvid_output_frame, \
        .flush          = tfvid_flush, \
        .bsfs           = bsf_name, \
        .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING | AV_CODEC_CAP_HARDWARE, \
        .caps_internal  = FF_CODEC_CAP_SETS_FRAME_PROPS, \
        .pix_fmts       = (const enum AVPixelFormat[]){ \
                                                        AV_PIX_FMT_YUV420P, \
                                                        AV_PIX_FMT_NONE }, \
        .hw_configs     = tfvid_hw_configs, \
        .wrapper_name   = "tfvid", \
    };
#if CONFIG_H264_TFVID_DECODER
DEFINE_TFVID_CODEC(h264, H264, "h264_mp4toannexb")
#endif