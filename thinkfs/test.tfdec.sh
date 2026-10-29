#!/bin/bash
# 每个解码器设备并发 2 路 H.264 解码，打印各路 fps。用法：./test.tfdec.sh input.mp4
FF=${FF:-../bin/ffmpeg}
IN=${1:?input.mp4}
for d in /dev/mv500 /dev/mv500-[0-9]*; do
  [ -e "$d" ] || continue
  for k in 1 2; do
    $FF -hide_banner -nostdin -c:v h264_tf -device $d -i "$IN" -an -f null - 2>&1 | tr '\r' '\n' | grep '^frame=' | tail -1 | sed "s|^|$d #$k: |" &
  done
done
wait
