#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib

echo y |  ../bin/ffmpeg -i ./IMG_1224.MP4  -pix_fmt nv12 ./output.yuv
wait

echo ----------------------------tfenc on 2 process-----------------------------------------

for i in {1..2}
do
# comment numactl for better performance
# echo y |  numactl --cpunodebind=0 --localalloc \
echo y |  time ../bin/ffmpeg -s 1920x1080 -pix_fmt nv12 -i ./output.yuv -c:v h264_tfenc -dev /dev/al_enc ./output$i.h264 &

done

# Wait for all background jobs to finish
wait
