export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~/tfdl2/tfenc:~/tfdl2/tfdec
for i in {1..1}

do

 echo y |  ./ffmpeg  -c:v h264_cuvid  -i "/home/wx/IMG_1224.MP4" -bf 0  -c:v h264_nvenc -c:a copy /home/wx/output$i.MP4 &

 done

sleep 5
wait

echo ---------------------------------------------------------------------

#-hwaccel cuvid