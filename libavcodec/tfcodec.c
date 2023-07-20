#include <stddef.h>
#include <stdint.h>
#include "tfenc.h"
static int h264_start_code(uint8_t* data,int len){
    if(len >= 3){
        if(data[0] == 0x00 && data[1] == 0x00){
            if(data[2] == 0x01)return 3;
            else if(data[2] == 0x00){
                if(len >= 4 && data[3] == 0x01)return 4;
            }
        }
    }
    return 0;
}
int analyzeh264Frame(uint8_t* data,int len){
    int i = 0;
    int nal_type = 0;
    for(;i < len;i++){
        int startLen = h264_start_code(data +i,len - i);
        if(startLen && startLen < len){
            //
            i += startLen;
            int flag = (data[i] & 0x1f);
            if(flag == 9)continue;
            if(flag == 7){
                continue;
            }
            if(flag == 8){
                continue;
            }
            if(flag == 6){//sei
                continue;
            }
            if(flag == 5){//idr
                return flag;
            }
            if(flag == 1){
                nal_type = 1;// non-idr slice
                break;
            }
        }
    }
    
    // now all tfenc I frame is IDR

    // if(!nal_type)return false;
    // nal_bitstream bs;
    // nal_bs_init(&bs,data + i + 1,len - i - 1);
    // nal_bs_read_ue(&bs);//first mb
    // auto slice_type = nal_bs_read_ue(&bs);
    // switch (slice_type)
    // {
    // case 2:case 7:
    //     return true;
    
    // default:
    //     return false;
    // }
    return 0;
}