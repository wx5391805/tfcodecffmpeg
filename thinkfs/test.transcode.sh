#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib:/home/wx/wx/ssd/gitlab/ESAC/cprovider/cefprovider/cefprovider/encoder/x264/lib/

../bin/ffmpeg -nostdin -y -c:v h264_tfvid  -i ./IMG_1224.MP4 -bf 0  -c:v h264_tfenc -c:a copy ./output.MP4

echo ----------------------------------end-----------------------------------

