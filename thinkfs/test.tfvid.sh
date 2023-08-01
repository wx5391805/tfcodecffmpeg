#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib:/home/wx/wx/ssd/gitlab/ESAC/cprovider/cefprovider/cefprovider/encoder/x264/lib/
decoder=h264_tfvid
input=IMG_1224.MP4
exec="../bin/ffmpeg -y -nostdin " 
#-nostdin 解决ffmpeg多后台进程会破坏控制台的bug

numa() {
  # 检查numactl是否存在
  if command -v numactl >/dev/null 2>&1; then
    # 如果numactl存在，则使用numactl执行命令
    set -x
    numactl --cpunodebind="$1" --localalloc "${@:2}"
    set +x
  else
    # 如果numactl不存在，则直接执行命令
    set -x
    "${@:2}"
    set +x
    echo '***********numactl not installed, performance is not guaranteed***********'
  fi
}


#检查编码器数量，并设置对应numa节点号
devs=("/dev/mv500" "/dev/mv500-2" "/dev/mv500-3" "/dev/mv500-4" "/dev/mv500-5" "/dev/mv500-6" "/dev/mv500-7" "/dev/mv500-8" "/dev/mv500-9" "/dev/mv500-10" "/dev/mv500-11" "/dev/mv500-12")

if [ -e ${devs[6]} ]; then
    numas=(0 2 1 3)
elif [ -e ${devs[3]} ]; then
    numas=(0 1)
else
    numas=(0)
fi

devNumbers=${#numas[@]}

outputFiles=()

for i in $(seq 0 $(($devNumbers - 1)))
do

# numa ${numas[i]}  $exec -c:v $decoder  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null 2>&1 | tee output_$(($i * 3)).log  &
# numa ${numas[i]}  $exec -c:v $decoder  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null 2>&1 | tee  output_$(($i * 3 + 1)).log &
# numa ${numas[i]}  $exec -c:v $decoder  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null 2>&1 | tee  output_$(($i * 3 + 2)).log &


numa ${numas[i]}  $exec -c:v $decoder -dev ${devs[$((i * 3))]}  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null 2>&1 | tee output_$(($i * 3)).log  &
numa ${numas[i]}  $exec -c:v $decoder -dev ${devs[$((i * 3 + 1))]}  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null 2>&1 | tee  output_$(($i * 3 + 1)).log &
numa ${numas[i]}  $exec -c:v $decoder -dev ${devs[$((i * 3 + 2))]}  -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null 2>&1 | tee  output_$(($i * 3 + 2)).log &

outputFiles+=("output_$(($i * 3)).log" "output_$(($i * 3 + 1)).log" "output_$(($i * 3 + 2)).log")

#cpu
# numactl --cpunodebind=0 --localalloc   $exec -c:v h264 -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
# numactl --cpunodebind=1 --localalloc   $exec -c:v h264 -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
# numactl --cpunodebind=2 --localalloc   $exec -c:v h264 -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
# numactl --cpunodebind=3 --localalloc   $exec -c:v h264 -i $input -c:a copy -pix_fmt yuv420p -f null /dev/null &
# echo y | time  ../bin/ffmpeg -c:v $decoder -dev /dev/mv500-3 -threads 1 -i IMG_1224.MP4 -pix_fmt yuv420p ./output$i.yuv &
done
wait

for out in "${outputFiles[@]}"; do
  grep "fps=" ${out}
done

echo --------------------------test tfvid end on $devNumbers devices----------------------------------
