#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib
decoder=h264_tfvid
input=IMG_1224.MP4
exec=../bin/ffmpeg
for i in {1..1}
do


numactl --cpunodebind=0 --localalloc   time  $exec -c:v $decoder -dev /dev/mv500-3  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
numactl --cpunodebind=0 --localalloc   time  $exec -c:v $decoder -dev /dev/mv500-2  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
numactl --cpunodebind=0 --localalloc   time  $exec -c:v $decoder -dev /dev/mv500    -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
#numactl --cpunodebind=1 --localalloc   time  $exec -c:v $decoder -dev /dev/mv500-6  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
#numactl --cpunodebind=1 --localalloc   time  $exec -c:v $decoder -dev /dev/mv500-5  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
#numactl --cpunodebind=1 --localalloc   time  $exec -c:v $decoder -dev /dev/mv500-4  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &


#cpu
# numactl --cpunodebind=0 --localalloc  echo y | time  $exec -c:v h264 -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
# numactl --cpunodebind=1 --localalloc  echo y | time  $exec -c:v h264 -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
# numactl --cpunodebind=2 --localalloc  echo y | time  $exec -c:v h264 -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
# numactl --cpunodebind=3 --localalloc  echo y | time  $exec -c:v h264 -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
# echo y | time  ../bin/ffmpeg -c:v $decoder -dev /dev/mv500-3 -threads 1 -i IMG_1224.MP4 -pix_fmt yuv420p ./output$i.yuv &
done
wait
echo -----------------ffmpeg tfvid test end-----------------------------
