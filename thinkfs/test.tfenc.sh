#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib

echo y |  ../bin/ffmpeg -i ./IMG_1224.MP4  -pix_fmt nv12 ./output.yuv
wait

echo ---------------generate a yuv file, now encoding start on 2 process------

sleep 3

for i in {1..2}
do
# comment numactl for better performance
numactl --cpunodebind=0 --localalloc \
echo y | time ../bin/ffmpeg -s 1920x1080 -pix_fmt nv12 -i ./output.yuv -c:v h264_tfenc -dev /dev/al_enc output$i.h264 &

#numactl --cpunodebind=1 --localalloc \
echo y | time ../bin/ffmpeg -s 1920x1080 -pix_fmt nv12 -i ./output.yuv -c:v h264_tfenc -dev /dev/al_enc2 output2_$i.h264 &


done

# Wait for all background jobs to finish
wait

echo --------------------------test tfenc end----------------------------------
