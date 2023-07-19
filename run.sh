#export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../lib
./ffmpeg -hwaccels
#ldd ./ffmpeg
#./ffmpeg -re -stream_loop -1 -i "/home/wx/IMG_1224.MP4" -vcodec h264 -acodec aac -f rtsp -rtsp_transport tcp rtsp://127.0.0.1/live/test
./ffmpeg -re -stream_loop -1 -i "/home/wx/IMG_1224.MP4" -bf 0 -vcodec h264 -acodec aac -f flv rtmp://10.10.11.44/live/video 

