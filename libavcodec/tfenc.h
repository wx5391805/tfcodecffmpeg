#ifndef AVCODEC_TF_ENC_H264_H
#define AVCODEC_TF_ENC_H264_H
 
#include "config.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"
#include "hwconfig.h"
#include "avcodec.h"


#include "tfenc/tfenc_api.h"
 
typedef struct TfencContext {
    void* handle
}TfencContext;
 
#endif