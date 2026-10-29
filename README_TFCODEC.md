FFmpeg 9 with tfcodec（tfcodec9.0 分支）
=============

在 FFmpeg 9.0.2 上集成 TFDL2 SDK 的 libtfdec / libtfenc，调用 TF7140/7180 的硬件视频编解码。
本分支在 tfcodec4.4 的基础上按 FFmpeg 9 的新接口重写，编解码器名统一为 `*_tf`。English: see below.

## 支持

| 名称 | 类型 | 说明 |
|---|---|---|
| h264_tf, hevc_tf | 解码 | 自动加 `*_mp4toannexb`，可直接解 MP4/MKV |
| mpeg2_tf, mpeg4_tf | 解码 | 流头在 extradata 时会先送给引擎 |
| vp8_tf | 解码 | 与 FFmpeg 软件解码逐帧一致 |
| mjpeg_tf | 解码 | 基线 4:2:0 JPEG；输出全范围色彩 |
| h264_tf, hevc_tf | 编码 | 输入 nv12 / yuv420p；`-b:v -maxrate -g -level -rc cbr/vbr -profile baseline/main/high` |

这些编解码器不会被 FFmpeg 默认选用，需按名字指定（`-c:v h264_tf`）。不加 `--enable-libtfcodec` 时
不会编译这些编解码器。

## 环境准备

只需要 TFDL2 SDK，以 SDK 解压在 `/root/tfdl2` 为例：

1. 安装编解码驱动与固件（需管理员权限）：`sudo /root/tfdl2/driver/codec/buildTFCoderDriver.sh`，
   用 `lsmod | grep -E "mve_driver|al_module"` 确认已加载。
2. 启动编码服务（需管理员权限，重启后需再次运行）：`sudo /root/tfdl2/tfenc/tfenc_service`。
   同一时间只能有一个 tfenc_service 使用编码器。只解码时可以不启动。

## 编译

```sh
./configure --prefix=/opt/ffmpeg --tfdldir=/root/tfdl2
make -j40 && make install
```

`--tfdldir` 指向 SDK 目录即启用硬件编解码，并使用 SDK 里的头文件与 libtfdec.so / libtfenc.so；
生成的 ffmpeg 记录了这两个库的路径，运行时不需要设置 `LD_LIBRARY_PATH`（SDK 目录移动后需重新编译）。
其他依赖与官方 FFmpeg 相同，均为可选。

## 使用

```sh
# 解码
ffmpeg -c:v h264_tf -i in.mp4 -f null -
# 转码（解码与编码都在硬件上）
ffmpeg -c:v h264_tf -i in.mp4 -c:v hevc_tf -b:v 4M -c:a copy out.mp4
# 指定解码器设备
ffmpeg -c:v h264_tf -device /dev/mv500-2 -i in.mp4 -f null -
# JPEG
ffmpeg -c:v mjpeg_tf -i in.jpg out.png
```

解码选项：`-device`（默认 /dev/mv500）、`-out_buffers`（引擎输出缓冲，默认 8）、`-zero_copy`（默认开）。

## 性能（TF7140A，Linux 7.2.6，1080p H.264 Main）

| 项目 | fps |
|---|---:|
| 单个解码器 | 约 680 |
| 3 个解码器各 2 路合计 | 约 1700 |
| H.264 → HEVC 4 Mb/s 转码 | 约 320 |
| H.264 6 Mb/s 编码 | 约 250–290 |

正确性：H.264 / HEVC / VP8 解码与 FFmpeg 软件解码逐帧一致（framemd5）；MPEG-2/4、MJPEG 对软件解码 PSNR
64/63/71 dB；H.264 编码 8 Mb/s 对源 PSNR 约 47.6 dB。

## 与 tfcodec4.4 相比修掉的问题

- 解码：两次“队列满”检查之间会丢包；MPEG-4 缺流头不出图。
- 编码：从不发 EOS，结尾几帧丢失；时间戳与输出队列各 4 格且不检查，生产快于消费时会覆盖并崩溃；初始化失败后关闭会崩溃。
- JPEG：`tfdec_query_input_queue` 不支持 JPEG 会话（会锁错互斥量而挂起），JPEG 会话不调用它。

## 已知限制

- 未支持：10-bit（Main10 / NV12_10B）、码流中途改分辨率（解码器按容器给出的尺寸创建）、hwcontext / 硬件帧、VP9（SDK 无此角色）。
- 编码器只开放码率、GOP、profile、level；SDK 未提供画质相关的高级参数。
- 未提供 mjpeg_tf 编码器：E200 的 JPEG 量化表固定（约相当于 libjpeg 质量 25），码率参数不起作用，
  同样大小下软件 JPEG 画质更好且更快。

## 测试脚本

`thinkfs/` 下：`test.tfdec.sh`（多解码器并发）、`test.tfenc.sh`、`test.transcode.sh`、`test.jpeg.sh`，
参数为输入视频，例如 `FF=/opt/ffmpeg/bin/ffmpeg ./test.tfdec.sh thinkfs/IMG_1224.MP4`。

---

## English

FFmpeg 9.0.2 with hardware video decoding and encoding on ThinkForce TF7140/7180 through the TFDL2 SDK
(libtfdec / libtfenc). A rewrite of the tfcodec4.4 branch for FFmpeg 9's codec API.

- Decoders `h264_tf`, `hevc_tf`, `mpeg2_tf`, `mpeg4_tf`, `vp8_tf`, `mjpeg_tf`; encoders `h264_tf`, `hevc_tf`
  (NV12 / YUV420P input; options `-b:v -maxrate -g -level -rc cbr|vbr -profile baseline|main|high`).
- They are never picked by default; select them by name, e.g. `ffmpeg -c:v h264_tf -i in.mp4 -c:v hevc_tf -b:v 4M out.mp4`.
  Without `--enable-libtfcodec` they are not built.
- Setup needs only the TFDL2 SDK (say in /root/tfdl2): install the codec drivers and firmware with
  `sudo /root/tfdl2/driver/codec/buildTFCoderDriver.sh`, start exactly one `sudo /root/tfdl2/tfenc/tfenc_service`
  (encoding only), then `./configure --prefix=/opt/ffmpeg --tfdldir=/root/tfdl2 && make -j40 && make install`.
  `--tfdldir` enables the codecs and records the SDK's library directories in ffmpeg, so no LD_LIBRARY_PATH.
- Decoder options: `-device` (default /dev/mv500), `-out_buffers` (default 8), `-zero_copy` (default on).
- Measured on TF7140A, 1080p H.264: about 680 fps per decoder, about 1700 fps over three decoders, about 320 fps
  for H.264 to HEVC transcoding. H.264, HEVC and VP8 output is frame-exact against FFmpeg's software decoders.
- Not supported: 10-bit, mid-stream resolution changes, hwcontext, VP9, a JPEG encoder (the E200's JPEG
  quantisation is fixed at roughly libjpeg quality 25).
- Tests: `FF=/opt/ffmpeg/bin/ffmpeg thinkfs/test.*.sh <input video>`.
