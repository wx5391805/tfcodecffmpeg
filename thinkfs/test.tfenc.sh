#!/bin/bash
# 把输入的前 600 帧转成 nv12 原始数据，再分别用 h264_tf、hevc_tf 编码。用法：./test.tfenc.sh input.mp4
FF=${FF:-../bin/ffmpeg}
IN=${1:?input.mp4}
$FF -hide_banner -nostdin -y -i "$IN" -an -frames:v 600 -pix_fmt nv12 -f rawvideo /tmp/tfenc_src.nv12 2>/dev/null
SIZE=$($FF -hide_banner -i "$IN" 2>&1 | grep -oE 'Video: .*, [0-9]+x[0-9]+' | grep -oE '[0-9]+x[0-9]+$' | head -1)
for c in h264_tf hevc_tf; do
  printf "%s: " $c
  $FF -hide_banner -nostdin -y -f rawvideo -pix_fmt nv12 -s $SIZE -r 30 -i /tmp/tfenc_src.nv12 -c:v $c -b:v 6M /tmp/tfenc_out_$c.mp4 2>&1 | tr '\r' '\n' | grep '^frame=' | tail -1
done
rm -f /tmp/tfenc_src.nv12
