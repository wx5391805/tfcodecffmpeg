#!/bin/bash
# H.264 -> HEVC 硬件转码，并用软件解码核对帧数与 PSNR。用法：./test.transcode.sh input.mp4
FF=${FF:-../bin/ffmpeg}
IN=${1:?input.mp4}
OUT=/tmp/tftranscode.mp4
$FF -hide_banner -nostdin -y -c:v h264_tf -i "$IN" -c:v hevc_tf -b:v 4M -c:a copy $OUT 2>&1 | tr '\r' '\n' | grep '^frame=' | tail -1
echo "frames: $($FF -hide_banner -nostdin -i $OUT -f null - 2>&1 | tr '\r' '\n' | grep '^frame=' | tail -1 | grep -oE 'frame= *[0-9]+')"
echo "psnr:   $($FF -hide_banner -nostdin -i $OUT -i "$IN" -lavfi '[0:v][1:v]psnr' -f null - 2>&1 | grep -oE 'average:[0-9.]+')"
