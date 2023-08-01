#!/bin/bash
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib:/home/wx/wx/ssd/gitlab/ESAC/cprovider/cefprovider/cefprovider/encoder/x264/lib/

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
devs=("/dev/al_enc" "/dev/al_enc2" "/dev/al_enc3" "/dev/al_enc4")

if [ -e ${devs[3]} ]; then
    numas=(0 2 1 3)
elif [ -e ${devs[1]} ]; then
    numas=(0 1)
else
    numas=(0)
fi

devNumbers=${#numas[@]}


echo ---------------prepare a yuv file------

../bin/ffmpeg -y -i ./IMG_1224.MP4  -pix_fmt nv12 ./output.yuv

echo ---------------generated a yuv file, now encoding 2 process on $devNumbers devices------

sleep 3

for i in {1..2}
do

    for j in $(seq 0 $(($devNumbers - 1)))
    do
    numa ${numas[j]} \
    ../bin/ffmpeg -nostdin -y -s 1920x1080 -pix_fmt nv12 -i ./output.yuv -c:v h264_tfenc -dev ${devs[j]} output${j}_${i}.h264 &

    #echo "Running iteration with j=$j,i=$i, numas=${numas[j]}, devs=${devs[j]}"
    done

done

# Wait for all background jobs to finish
wait

echo --------------------------test tfenc end on $devNumbers devices----------------------------------
