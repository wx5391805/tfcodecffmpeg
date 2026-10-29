#!/bin/bash
# 生成 300 帧 MJPEG，用 mjpeg_tf 硬件解码，与软件解码比较 PSNR，并测速度。用法：./test.jpeg.sh input.mp4
FF=${FF:-../bin/ffmpeg}
IN=${1:?input.mp4}
SRC=/tmp/tfjpeg_src.avi
$FF -hide_banner -nostdin -y -i "$IN" -an -frames:v 300 -c:v mjpeg -q:v 3 -pix_fmt yuvj420p $SRC 2>/dev/null
echo "psnr: $($FF -hide_banner -nostdin -c:v mjpeg_tf -i $SRC -i $SRC -lavfi '[0:v][1:v]psnr' -f null - 2>&1 | grep -oE 'average:[0-9.]+')"
$FF -hide_banner -nostdin -c:v mjpeg_tf -i $SRC -f null - 2>&1 | tr '\r' '\n' | grep '^frame=' | tail -1
rm -f $SRC
