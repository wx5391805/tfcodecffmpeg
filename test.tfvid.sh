export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:~/tfdl2/tfenc:~/tfdl2/tfdec
for i in {1..2}
do
  echo y |  time ./ffmpeg -c:v h264_tfvid  -i "/home/wx/IMG_1224.MP4"  -pix_fmt yuv420p ~/output$i.yuv &
done
