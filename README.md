FFmpeg with tfcodec
=============
FFmpeg 集成libtfdec+libtfenc 硬件编解码库，可在使用ffmpeg命令行工具、API 进行编解码时获得硬件加速

在Thinkforce 7140/7180系列服务器编译、运行

支持各类Linux操作系统

## 环境准备

以ubuntu为例, 要求gcc 9.4以上

1.clone代码，下载tfdl2 sdk

``` sh
git clone https://github.com/wx5391805/tfcodecffmpeg.git
cd tfcodecffmpeg
git submodule init
git submodule update
```

2.安装编解码器驱动，重启服务器后需重复上述脚本，注意管理员权限
``` sh
sudo thinkfs/tfdl2sdk/tfdl2/driver/codec/buildTFCoderDriver.sh 
sudo thinkfs/tfdl2sdk/tfdl2/tfenc/tfenc_service
```

使用 `lsmod | grep mve` 可看到 `mve_driver*`字样说明驱动安装成功

使用`ps -ef | grep tfenc` 可看到后台`tfenc_service`服务

## 编译

基本需求gcc 9.4以上，并确保有`tfdl2 sdk`,其他依赖一般是可选项，参考ffmpeg通用编译方法

以Ubuntu为例，clone代码切换到对应分支执行：

``` sh
./configure --prefix=/opt/ffmpeg/  #如果要单独指定tfdl位置，添加 --tfdldir=/path/to/tfdl2/, 否则默认在 ffmpeg/thinkfs/tfdl2sdk/tfdl2
make -j40
make install
```

即可在/opt/ffmpeg 生成安装包

## 使用

解压ffmpeg-tfcodec预编译包，或者[编译](#编译安装) 完成进入安装目录
``` sh
tar -zxvf ffmpeg.tar.gz
#或者 cd /opt/ffmpeg
.
├── bin        --可执行文件
├── include    --头文件
├── lib        --库文件
├── share      
└── test       --测试文件
cd test
```

### 解码

将`MP4/avc`文件解码为`yuv420p` raw data

``` sh
./test.tfvid.sh
#../bin/ffmpeg -c:v h264_tfvid -dev /dev/mv500 -i ./IMG_1224.MP4  -pix_fmt yuv420p ./output$i.yuv
```
可看到

` Stream #0:0 -> #0:0 (h264 (h264_tfvid) -> rawvideo (native))`

打印，说明成功调用h264_tfvid 解码器，可看到fps等信息:

`frame= 2072 fps=668 q=-0.0 Lsize=N/A time=00:01:25.65 bitrate=N/A speed=27.6x`

#### 参数说明:

`-c:v` 指定解码器为`h264_tfvid`硬件h264解码器，可使用`c:v h264`则为ffmpeg原生软件解码器

`-dev` 指定解码器设备

#### 性能：

| FFMPEG解码(1080P MP4 (h264 Main) -> yuvI420)      |  fps |
|-----------------------------------|---------------:|
| 7180硬件                  | 1998          |
| 7180硬件                  | 3872          |
| cuvid（NVIDIA GTX3090）          | 772           |
| 7140 软件(h264/libx264)          | 2904          |



### 编码
`./test.tfenc.sh` 将`nv12`raw data 编码为h264/hevc 

多进程同时编码，总fps约等于每个fps打印的累加

#### 参数说明:

`-c:v` 指定编码器为`h264_tfenc`硬件h264编码器，`hevc_tfenc` 为h265编码器

`-dev` 指定编码器设备

其他配置参见libavcode说明文件

#### 性能：

| FFMPEG编码(1080P NV12 -> h264 Main)    |  fps |
|---------------------------------|---------------:|
| 7140 硬件                        | 400          |
| 7180 硬件                        | 762          |
| nvenc（NVIDIA GTX3090）          | 470          |
| 7140 软件(h264/libx264)          | 327          |

### 转码
`./test.transcode.sh`

使用tfvid解码，再用tfenc重新编码mp4，生成新的mp4视频

## 



# ==========End============

FFmpeg README
=============

FFmpeg is a collection of libraries and tools to process multimedia content
such as audio, video, subtitles and related metadata.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides a mean to alter decoded Audio and Video through chain of filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](https://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](https://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](https://ffmpeg.org/ffprobe.html) is a simple analysis tool to inspect
  multimedia content.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

The offline documentation is available in the **doc/** directory.

The online documentation is available in the main [website](https://ffmpeg.org)
and in the [wiki](https://trac.ffmpeg.org).

### Examples

Coding examples are available in the **doc/examples** directory.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under
GPL. Please refer to the LICENSE file for detailed information.

## Contributing

Patches should be submitted to the ffmpeg-devel mailing list using
`git format-patch` or `git send-email`. Github pull requests should be
avoided because they are not part of our review process and will be ignored.
