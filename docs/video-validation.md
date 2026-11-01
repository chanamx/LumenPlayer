# Lumen Player 1.4 视频兼容性验收

> 历史 1.4.0 记录。当前 1.4.1 启动优化、完整回归及正式程序等待实测见 [字幕启动验收](caption-startup-validation.md)。下文 `test/results` 工作文件会随新测试更新，1.4.0 证据已收入 `build/backups/pre-caption-startup-20261019.zip`。

日期：2026-10-19。平台：Windows-10-10.0.19045-SP0；Intel Core i5-7200U，2 核 / 4 线程。当前正式程序版本 1.4.0。

## 已交付行为

系统 MFPlay/EVR 继续作为硬件优先路径；软件视频及其他多流容器统一使用进程内 FFmpeg。仅对文件头确认的 WAV、FLAC、AIFF 优先使用原生音频读取器。原生读取器可能隐藏不认识的视频轨，本轮 FFV1 样例曾只暴露 FLAC 音频；现在使用独立容器解析避免误判纯音频。测试也要求每个有效视频保留视频轨并实际出帧。

系统 H.264 解码器在 High 4:4:4 文件上可以建立拓扑，却无法正常播完，随后关闭阻塞于 msmpeg2vdec/mfcore。已通过本轮测试进程的 LLDB 调用栈确认；现在在 SetMediaItem 前读取已选流的实际 profile，Baseline/Main/High 之外直接转入软件解码，避免激活不支持的系统解码器。故障原始日志和栈摘要保留在 `test/results/diagnostics`。常见 H.264 仍使用系统优先路径。

FFmpeg 适配器直接通过 Unicode 本地文件 AVIO 读取，禁止自行打开外部引用；不启动 ffmpeg.exe 播放、不产生转码副本。统一处理视频 BGRA 转换、BT.601/709/2020 矩阵及范围、像素宽高比、90/180/270 度旋转、动态分辨率、10-bit SDR 输入和音频格式转换。转换缓冲有边界校验；解码线程数限制为 1–4，音频沿用有界队列和 WASAPI 事件驱动输出。

定位按视频流关键帧索引回退，并解码少量前置帧，丢弃部分跳过 RGB 转换。修复了 MPEG-2 PS 在片尾后向前定位落入 PES/GOP 中间、FLV 首视频关键帧晚于音频起点导致回到开头失败、封装时间起点或头部填充影响时长的问题。片尾完整排空解码器及重采样器，重播清理包、解码、采样状态和字幕代次；亚秒级文件也会重新开始字幕输入。

FFmpeg 视频的字幕 tap 使用仅音频解码，覆盖 WebM/Opus 等系统采集缺口。实时字幕保持起点等待模型和采集就绪的行为，加载时暂停、取消字幕、切换文件复用模型、无音轨跳过等待均保留。停止最后一个解码器/tap 后卸载视频 DLL，停止字幕后释放识别器与模型。软硬解回退、音频设备重建继续保留位置、暂停状态与字幕等待意图。

## 完整回归与正式程序

完整测试通过 **650 项原生断言**。其中包含多尺寸和 DPI 布局、键盘/鼠标交互、起点等待、模型复用、定位/片尾/重播、音频设备失效注入及有界重连、格式矩阵、直接解码与采样、字幕实际识别和 SRT 导出。最终日志：`test/results/regression.log`；媒体日志：`backend.log`。

- **68 条格式/偏好组合：66 条有效媒体实际播放通过，2 条截断输入安全拒绝。** 每个软件视频均检查暂停首帧、向前定位预览、定位后再次排空和片尾重播。源文件共 32 个编解码样例，外加 Unicode 路径副本及损坏输入，分别使用软件和硬件优先偏好。
- **10 个视频的直接解码检查**：包含 HEVC 10-bit、AV1、ProRes、FFV1、MPEG-2、TS、非零起点及动态分辨率，验证真实帧数、音频采样、归一化时间、片尾时长、EOF 后定位。音频转换覆盖 PCM8/16/24/32 和 float，检查时长、对齐及非静音输出。
- 三个旋转方向均与独立 FFmpeg CLI 的参考像素比较；非方形像素、321×179 非整齐尺寸、322×180 有效画面、动态变为 640×360、B 帧及较长 GOP 也纳入验证。
- WebM/Opus 视频实际联测模型、起点等待、首段文字、暂停定位清空旧上下文、片尾末句及 SRT。无音轨 HEVC 验证直接播放并保留实时字幕开关。
- 正式 EXE 分别以 H.264 MP4、HEVC MP4 和 VP9/Opus WebM 启动，验证窗口响应、模型及 tap 就绪、从 0.000 秒释放等待、运行库加载、正常退出和配置保留。WebM 使用完整正式目录的独立副本及软件偏好，同时验证含中文、空格、`#`、`%` 的软件路径下模型和视频 DLL 均可加载；三次烟测的 EXE 散列与 dist 完全一致。
- 媒体路径含中文、空格、`#`、`%`；隔离性能测试中的正式候选程序安装路径含中文及空格。没有修改系统长路径策略。

## 本机格式矩阵

下表是生成短片在本机的实际路径。系统路径内部也可能软件解码，“硬件优先”不构成已验证 GPU 解码单元使用的证据。通过短样例不能推断所有同扩展名媒体、任意码率/分辨率和所有显卡都通过。

| 样例 | 软件偏好 | 硬件优先 |
| --- | --- | --- |
| pcm24-96k-stereo.wav | 通过：原生音频 | 通过：原生音频 |
| pcm32-44k-mono.wav | 通过：原生音频 | 通过：原生音频 |
| float32-48k-51.wav | 通过：原生音频 | 通过：原生音频 |
| aac-51.m4a | 通过：FFmpeg | 通过：FFmpeg |
| wma.wma | 通过：FFmpeg | 通过：FFmpeg |
| alac.m4a | 通过：FFmpeg | 通过：FFmpeg |
| opus.ogg | 通过：FFmpeg | 通过：FFmpeg |
| h264.mov | 通过：FFmpeg | 通过：系统 |
| h264.mkv | 通过：FFmpeg | 通过：系统 |
| h264.ts | 通过：FFmpeg | 通过：系统 |
| mpeg4.avi | 通过：FFmpeg | 通过：系统 |
| mjpeg.avi | 通过：FFmpeg | 通过：系统 |
| wmv.wmv | 通过：FFmpeg | 通过：系统 |
| anamorphic.mp4 | 通过：FFmpeg | 通过：系统 |
| hevc.mp4 | 通过：FFmpeg | 通过：FFmpeg |
| hevc10.mp4 | 通过：FFmpeg | 通过：FFmpeg |
| vp8.webm | 通过：FFmpeg | 通过：系统 |
| vp9.webm | 通过：FFmpeg | 通过：系统 |
| vp9-10.webm | 通过：FFmpeg | 通过：系统 |
| av1.webm | 通过：FFmpeg | 通过：FFmpeg |
| flv.flv | 通过：FFmpeg | 通过：FFmpeg |
| mpeg2.mpg | 通过：FFmpeg | 通过：FFmpeg |
| prores.mov | 通过：FFmpeg | 通过：FFmpeg |
| ffv1.mkv | 通过：FFmpeg | 通过：FFmpeg |
| odd444.mkv | 通过：FFmpeg | 通过：FFmpeg |
| bframes.mp4 | 通过：FFmpeg | 通过：系统 |
| offset.ts | 通过：FFmpeg | 通过：系统 |
| rotate90.mp4 | 通过：FFmpeg | 通过：系统 |
| rotate180.mp4 | 通过：FFmpeg | 通过：系统 |
| rotate270.mp4 | 通过：FFmpeg | 通过：系统 |
| hevc-silent.mp4 | 通过：FFmpeg | 通过：FFmpeg |
| dynamic.ts | 通过：FFmpeg | 通过：系统 |
| 视频 空格 #%25.mov | 通过：FFmpeg | 通过：系统 |
| truncated.mp4 | 拒绝损坏输入 | 拒绝损坏输入 |

明细为 `compatibility-results.json` 和 `decoder-results.json`；配方为 `test/make_compat_fixtures.py`。原先 1.3 本机无法打开的 HEVC、Ogg/Opus 及间歇报错的 TS 软件路径，本轮都实际解码并完成播放。

## 性能与内存

对照从修改前备份取出的 1.3.0 EXE 与最终 1.4.0 EXE，各自放在隔离目录。相同 6 秒 1280×720 H.264/AAC 视频，软件偏好、字幕关闭、音量 0；观察解码器就绪后等待 0.5 秒，再采样 3 秒。两版本交错各 4 次，轮换先后顺序，下表为中位数。

| 指标 | 1.3 原生软件路径 | 1.4 FFmpeg 软件路径 |
| --- | ---: | ---: |
| 启动到输入空闲 | 418.4 ms | 438.2 ms |
| 启动到解码器就绪 | 419.2 ms | 439.1 ms |
| 约 3 秒窗口进程 CPU 时间 | 3.023 s | 1.812 s |
| 工作集 | 81.9 MiB | 72.5 MiB |
| 私有提交 | 64.4 MiB | 61.5 MiB |

此窗口 CPU 时间变化 **-40.1%**。这是同一视频在两条软件播放路径下的进程采样，含解码、转换、输出和绘制。没有用该指标推断逐帧耗时、低功耗模式、4K/8K 或所有视频实时播放能力。输入空闲和解码器就绪也不是显示器上首帧出现时间。所有采样窗口保持窗口响应并正常退出，原始记录为 `video-comparison.json`，含每次 EXE 散列及实际采样时长。

| 字幕联测指标 | 本轮记录 |
| --- | ---: |
| 已就绪模型切换视频 | 317.9 ms |
| 原生 WAV 播放到首条流式文字 | 467.7 ms |
| FFmpeg WebM 播放到首条流式文字 | 692.5 ms |
| 正式 MP4 进程到模型/tap 就绪 | 11288 ms |
| 正式 WebM 进程到模型/tap 就绪 | 8959 ms |
| 正式 WebM 含字幕工作集 / 私有提交 | 283.6 / 269.0 MiB |

首条文字来自短中文样例，模型已就绪后开始计时；完整启动时间包含首次模型加载。模型和解码参数没有更换，本轮重点为媒体采集/播放兼容性。保留的 WenetSpeech 单句输出与此前一致（29 字中漏末尾一个语气词，CER 1/29），不能作为语料级准确率或新质量提升结论。

## 依赖、体积与恢复

正式 EXE 453,120 字节。五个新增 FFmpeg DLL 合计 **119,990,784 字节（114.43 MiB）**；包含模型、识别运行库及许可的完整 `dist` 正式文件为 **290.89 MiB**，不含用户配置/日志。更广的独立软件解码覆盖增加了安装体积，DLL 不全部等于常驻内存，内存以以上实测为准。

FFmpeg 版本 `n9.0.2-28-g669f5ab083-20261018`，来自官方下载安装页列出的 BtbN LGPL shared release/9.0 构建。导出的配置不启用 GPL/nonfree；五个未修改 DLL 动态载入且可单独替换。版本、来源、散列和编译选项见 `third_party/ffmpeg/provenance.json`、`build-configuration.json` 及 `dist/licenses`。项目保留 SDK 归档（73.6 MiB）和对应 FFmpeg 核心提交源码归档（16.5 MiB），恢复脚本优先使用本地已验证归档，不会接受滚动下载的新散列。

用户配置与本轮安装前快照完全一致。修改前源码、1.3.0 程序、配置及证据保存在 `build/backups/pre-video-compatibility-20261019.zip`。本轮清理重复构建/测试目录共 696.62 MiB，保留正式依赖、固定归档、源码、测试结果与备份；明细为 `cleanup-video.json`。交付散列见根目录 `release-manifest.json`。

## 验证边界与复验

本轮未实现 HDR 到 SDR 色调映射、FFmpeg GPU 解码、完整 ASS 特效或多音轨选择界面。未验证所有 Windows/GPU/驱动、4K/8K 实时性、数小时长片、物理设备热拔插、真实浏览器 SODA 和桌面所有控件逐项实点。已覆盖的设备故障为原生测试注入后重建真实音频输出。使用的 FFmpeg 构建最低保证 Windows 10 22H2；旧系统兼容性不能由本机结果推断。UI PNG 来自播放器自身绘制缓冲。

```powershell
.\test.ps1
.\build.ps1 -Install
.\test\smoke-release.ps1 -Media test/sample.mp4
.\test\smoke-release.ps1 -Media test/results/compat/hevc.mp4 -ExpectCompatibility -ResultName release-hevc-smoke.json
.\test\smoke-path-release.ps1
.\test\measure-video.ps1
.\test\cleanup-video.ps1 -Execute
python .\test\finalize_video_report.py
```

正式烟测要求已有播放器关闭、当前字幕偏好开启，并保留配置。安装前需另存当时的 INI 快照。重新构建会再次生成临时副本，清理记录保留的是本次交付时的清理结果。旧 1.3 报告及性能脚本保留为历史记录，其 `test/results` 明细可能已由本轮结果替换，旧证据可从备份恢复。

API / 来源依据：[FFmpeg 官方下载](https://ffmpeg.org/download.html)、[BtbN 构建与系统范围](https://github.com/BtbN/FFmpeg-Builds/blob/master/README.md)、[发送/接收解码 API](https://ffmpeg.org/doxygen/8.0/group__lavc__encdec.html)、[自定义 AVIO](https://ffmpeg.org/doxygen/8.0/avio_read_callback_8c-example.html)、[微软 H.264 profile/色度限制](https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder)、[FFmpeg 许可](https://ffmpeg.org/legal.html)。生产代码按本地固定 9.0 头文件编译，以上 8.0 API 文档只用于稳定接口语义参考。

## 完整测试原始指标

- `METRIC software_audio_open_ms 363.64`
- `METRIC software_video_open_ms 145.32`
- `METRIC software_video_eof_wait_ms 715.30`
- `METRIC hardware_preferred_open_ms 268.59 actual_hw=1`
- `METRIC wasapi_event_driven 1`
- `METRIC caption_enabled_file_open_ms 121.88`
- `METRIC caption_loading_two_switches_ms 315.49`
- `METRIC caption_model_load_ms 5686.27`
- `METRIC caption_first_partial_ms 467.67 media_pos=0.499`
- `METRIC caption_playback_cpu_rtf 0.2355 wall_s=8.827 audio_s=8.825`
- `METRIC caption_private_bytes before=104308736 loaded=309161984 stopped=85667840`
- `METRIC caption_warm_video_switch_ms 317.95`
- `METRIC compatibility_caption_first_partial_ms 692.53`
