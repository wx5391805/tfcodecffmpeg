export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~/tfdl2/tfenc

time ./ffmpeg -i "/home/wx/IMG_1224.MP4" -pix_fmt nv12 ~/output.yuv
sleep 2
echo ---------------------------------------------------------------------

for i in {1..2}
do
  echo y |  time ./ffmpeg -s 1920x1080 -pix_fmt nv12 -i ~/output.yuv -c:v h264_tfenc ~/output$i.h264 &
done

# Wait for all background jobs to finish
#wait
