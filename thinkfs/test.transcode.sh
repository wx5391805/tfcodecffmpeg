#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib

for i in {1..1}
do
# comment numactl for better performance
#  numactl --cpunodebind=0 --localalloc \
../bin/ffmpeg  -c:v h264_tfvid  -i ./IMG_1224.MP4 -bf 0  -c:v h264_tfenc -c:a copy ./output$i.MP4 &
done

#sleep 5
wait

echo ---------------------------------------------------------------------

