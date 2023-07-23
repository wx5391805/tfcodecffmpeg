export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~/tfdl2/tfenc:~/tfdl2/tfdec
for i in {1..1}
do
  ./ffmpeg  -c:v h264_tfvid  -i "/home/wx/IMG_1224.MP4" -bf 0  -c:v h264_tfenc -c:a copy "/home/wx/output.MP4" &
done

sleep 5
wait

echo ---------------------------------------------------------------------
