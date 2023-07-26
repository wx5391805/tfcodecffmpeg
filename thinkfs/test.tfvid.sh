#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib
for i in {1..1}
do
# comment numactl for better performance
# numactl --cpunodebind=0 --localalloc \

echo y | time ../bin/ffmpeg -c:v h264_tfvid -dev /dev/mv500 -i ./IMG_1224.MP4  -pix_fmt yuv420p ./output$i.yuv &
done
