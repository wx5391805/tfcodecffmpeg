#ifndef AVCODEC_TF_ENC_H264_H
#define AVCODEC_TF_ENC_H264_H
 
#include "config.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"
#include "hwconfig.h"
#include "avcodec.h"


#include <pthread.h>
#include "tfenc/tfenc_api.h"
int analyzeh264Frame(uint8_t* data,int len);
int analyzeh265Frame(uint8_t* data,int len);

typedef struct TfencBuffer
{
    void* data;
    int len;
} TfencBuffer;

typedef struct TfencContext {
    AVClass        *class;
    void* handle;
    void* inputBuffer;
    AVFrame *frame;
    int nb_buffers;
    pthread_mutex_t mutex;
    volatile int closed;
    char* tf_dev;
    int rc;
    AVFifoBuffer *output_buffer_ready_queue;
    AVFifoBuffer *timestamp_list;
}TfencContext;
 
#endif