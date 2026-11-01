# Lumen Player 1.3 播放与兼容性验收

> 历史版本记录。当前 1.4 视频解码、格式矩阵与性能结果见 [视频兼容性验收](video-validation.md)。下文所引的 `test/results` 工作文件会随新测试更新，1.3 原始证据已收入 `build/backups/pre-video-compatibility-20261019.zip`。

日期：2026-10-19。平台：Windows-10-10.0.19045-SP0；本机 Intel Core i5-7200U，2 核 / 4 线程。

## 已交付行为

开启实时字幕后，启动或打开有音轨的媒体先准备解码并停在起点，等模型及音频采集就绪再按播放意图开始。等待时手动暂停，就绪后保持暂停；取消字幕立即释放等待，后台模型准备结束后自行释放。切换文件复用同一个正在加载或已就绪的模型，旧文件音频与识别结果通过停止 tap、重置流代次清除。无音轨视频直接播放且保留字幕偏好。模型加载或字幕采集失败会提示并释放等待，媒体可继续播放。

系统播放路径等待 MFPlay 媒体就绪事件，不通过提前播放获取尺寸。硬件优先直接尝试实际系统路径，避免仅凭全局 MFT 枚举排除可用的 DXVA/EVR；故障回退保留位置、播放/暂停意图与字幕等待状态。界面“系统”只标识 MFPlay/EVR 路径，不能作为 GPU 解码单元使用证明。

软件路径读取真实输出格式、正/负行跨度、有效显示区域、像素宽高比与 NV12 色彩矩阵/范围，校验帧缓冲并响应格式变化。实际测试中 H.264 的 322×180 画面会变为 336×192 解码缓冲，现按 aperture 裁掉填充。基础格式协商失败后，仅重试一次系统高级视频处理器，本机 WMV2 样例由此能够软解。片尾使用显式剩余流排空，避免停用已结束视频流时使残余音频转换失败。

音频优先使用可转换的 48k 双声道 PCM，分别尝试 float32、PCM16 和经过子格式校验的设备原生布局；每次初始化重试创建新 AudioClient。共享音频使用事件驱动，并保留轮询回退；输出缓冲写入失败和设备失效不会无声忙等。默认设备变化或连接失效后重建软件播放连接，保留位置及暂停状态；暂时不可用的音频按退避间隔最多重试三次。解码与音频线程捕获 C++ 异常，字幕采集错误有明确结束状态。

## 原生验证

完整测试通过 **258 项断言**，其中布局检查会在多尺寸和缩放下重复。完整日志：`test/results/regression.log`；媒体诊断：`backend.log`。

- 初始等待位置为 0，未向软件音频设备提交数据；模型完成后从起点开始，加载期间请求暂停不会被覆盖。
- 加载中两次换文件保留同一个加载线程；已就绪模型在新视频上直接复用；取消等待不加入加载线程等待。
- 无音轨、加载失败、采集失败、软硬解回退、片尾和重播、SRT 回读均覆盖。
- 使用真实 MF 缓冲验证带填充和倒置的 RGB、2D buffer、NV12 色域及短缓冲拒绝；真实视频验证有效显示区域和非方形像素。
- 音频失效、默认设备 ID 变化、暂时不可用重试、连续输出失效达到三次后停止重连及更换设备后可恢复、硬件错误为测试注入；后续恢复确实重新建立本机音频和解码连接。没有修改系统默认设备或做物理热拔插。
- 正式 EXE 烟测记录起点等待、模型与 tap 就绪后从 0.000 秒释放播放，加载期间窗口响应、正常退出、INI 保留均通过。

## 本机格式矩阵

以下是生成的短片在当前系统组件上的实际结果。两列为偏好设置，单元格注明最终实际路径；系统路径可内部使用软件解码。本轮未新增第三方解码包，不能据此宣称所有 Windows 或所有同扩展名媒体都支持。

| 样本 | 软件偏好 | 硬件优先 |
| --- | --- | --- |
| pcm24-96k-stereo.wav | 通过（软件路径） | 通过（软件路径） |
| pcm32-44k-mono.wav | 通过（软件路径） | 通过（软件路径） |
| float32-48k-51.wav | 通过（软件路径） | 通过（软件路径） |
| aac-51.m4a | 通过（软件路径） | 通过（软件路径） |
| wma.wma | 通过（软件路径） | 通过（软件路径） |
| alac.m4a | 通过（软件路径） | 通过（软件路径） |
| opus.ogg | 未能打开，安全拒绝 | 未能打开，安全拒绝 |
| h264.mov | 通过（软件路径） | 通过（系统路径） |
| h264.mkv | 通过（软件路径） | 通过（系统路径） |
| h264.ts | 解码错误，安全停止 | 通过（系统路径） |
| mpeg4.avi | 通过（软件路径） | 通过（系统路径） |
| mjpeg.avi | 通过（软件路径） | 通过（系统路径） |
| wmv.wmv | 通过（软件路径） | 通过（系统路径） |
| anamorphic.mp4 | 通过（软件路径） | 通过（系统路径） |
| hevc.mp4 | 未能打开，安全拒绝 | 未能打开，安全拒绝 |
| 视频 空格 #%25.mov | 通过（软件路径） | 通过（系统路径） |
| truncated.mp4 | 未能打开，安全拒绝 | 未能打开，安全拒绝 |

原始明细为 `compatibility-results.json`，样本生成配方为 `test/make_compat_fixtures.py`。HEVC MP4 与 Ogg/Opus 样例在本机没有可用的原生处理路径；损坏样本也应被拒绝。H.264 TS 的系统路径通过，但软件路径在多轮运行中仍间歇出现 `MF_E_TRANSFORM_TYPE_NOT_SET (0xC00D6D60)`，上表保留最终全量回归结果，不能视为稳定支持。程序对这些失败会停止并提示，避免长期黑屏或死循环。

## 时序与性能记录

| 指标 | 当前样本 |
| --- | ---: |
| 开启字幕时打开 WAV、返回 UI | 73.2 ms |
| 模型加载期间连续两次换文件 | 264.2 ms |
| 已就绪模型切换视频 | 243.3 ms |
| 模型就绪后从头播放到首条文字 | 506.8 ms |
| 原生 8.825 秒字幕样例的进程 CPU RTF | 0.1753 |
| 正式进程启动到模型和采集就绪 | 4400 ms |
| 正式进程工作集 / 私有提交 | 355.9 / 363.5 MiB |

独立视频性能对照：从本轮备份取 1.2.0 EXE，与最终候选 EXE 分别放在隔离目录，均为软件解码、字幕关闭、音量 0；同一 6 秒 H.264/AAC 视频，观察到 SourceReader 初始化后等待 0.5 秒，再采样 3 秒。两版本交错各 4 次，并逐轮交换先后顺序，下表为中位数。此对照不包含 ASR，不读取用户 INI；`playback-comparison.json` 保存原始值。首轮仅按输入空闲对齐的结果也保留在 `playback-comparison-initial.json`；旧版输入空闲可以早于媒体准备，新旧版采样起点因而有偏差，最终采用媒体准备标记对齐。

| 指标 | 1.2.0 | 1.3.0 |
| --- | ---: | ---: |
| 打开媒体到输入空闲 | 125.7 ms | 322.1 ms |
| 启动到观察到媒体准备标记 | 342.4 ms | 331.6 ms |
| 3 秒采样的进程 CPU 时间 | 1.859 s | 2.383 s |
| 工作集 | 83.6 MiB | 82.8 MiB |
| 私有提交 | 62.5 MiB | 62.1 MiB |

上述未改动正式 EXE 的 CPU 时间中位数变化 **+28.2%**，不能声称总 CPU 占用下降。为解释这一差异，又从备份及当前源码生成了仅增加计时/帧计数的隔离诊断构建，使用相同编译器、选项和资源；生产源码与 dist 未被插桩。读取并绘制有效画面后计数，准备后等待 0.5 秒，从实际呈现的第一帧开始采样至少 3 秒，同时记录进程 CPU 和媒体时间推进。两版交错各 4 次，结果为中位数：

| 诊断指标 | 1.2 功能代码 | 1.3 功能代码 |
| --- | ---: | ---: |
| 实际送出视频帧率（源为 30 fps） | 23.23 fps | 30.02 fps |
| 每 1 秒实时时间推进的媒体秒数 | 0.774 | 1.001 |
| 进程 CPU 时间 / 呈现帧 | 29.13 ms | 26.70 ms |

本样本新版维持更接近正常的播放节奏，单位时间呈现帧量增加；归一到呈现帧的进程 CPU 时间变化 -8.3%。因此总 CPU 时间增加与更多实际播放工作相关；这项判断来自上述诊断对照，不能推广为所有素材的确定因果结论。原始记录为 `playback-profile.json`，复现脚本为 `test/profile_playback.py` 和 `test/profile-playback.ps1`。这不是显示器扫描输出测量，也不证明音频听感或每个像素的呈现质量。

这些是本机短片、少量进程样本，包含窗口、播放、解码及渲染开销，不代表所有编码/分辨率的确定提升。输入空闲与观察到媒体准备标记均不是首帧呈现时间。ASR 的首条文字计时允许约 0.3 秒音频预读；CPU RTF 不等于整机 CPU 百分比。物理工作集与私有提交分开记录。WenetSpeech 短样例仍保留原生输出用于复核，单条 CER 不能代表全场景准确率。

原始过程测量：

- `METRIC software_audio_open_ms 255.95`
- `METRIC software_video_open_ms 100.14`
- `METRIC software_video_eof_wait_ms 2965.58`
- `METRIC hardware_preferred_open_ms 232.84 actual_hw=1`
- `METRIC wasapi_event_driven 1`
- `METRIC caption_enabled_file_open_ms 73.25`
- `METRIC caption_loading_two_switches_ms 264.17`
- `METRIC caption_model_load_ms 3327.85`
- `METRIC caption_first_partial_ms 506.84 media_pos=0.525`
- `METRIC caption_playback_cpu_rtf 0.1753 wall_s=8.828 audio_s=8.825`
- `METRIC caption_private_bytes before=84500480 loaded=310112256 stopped=59248640`
- `METRIC caption_warm_video_switch_ms 243.34`

## 文件、备份与边界

正式 EXE：`D:/LumenPlayer/dist/LumenPlayer.exe`，版本 1.3.0，429568 bytes，SHA-256 `5d3e49db1675aaa285da46a94219a0361461cd6cf16191c8b29fa374499c2aa5`。发布目录文件合计 176.4 MiB（不含 INI/日志）；没有增加播放器运行依赖。

1.2.0 源码、EXE、配置和证据备份：`build/backups/pre-playback-compatibility-20261019.zip`，其中 EXE SHA-256 为 `46d9c1dcd9292453ff801d8218f030279fa743cd3605f7fdeb21640d26d6d53f`。安装前 INI 快照：`build/backups/pre-playback-install-20261019.ini`；当前 INI SHA-256 为 `729c908ac9eb39b6cbb979485335be3ee01fe4392b370b08aae1b894e53fe6f6`，与安装前一致。模型与 runtime 继续使用 1.2 已核验的相同文件，权重来源与许可说明保留。

交付前清理本轮构建目录中与正式目录逐文件一致的重复模型、运行库、许可文件，以及隔离性能比较/诊断构建的临时程序和源码，共 178.0 MiB。保留最终程序、测试 EXE、构建对象、原始源码、正式依赖、备份和结果数据；重新运行 build.ps1 会再生成可直接运行的完整 staging 目录。明细见 `cleanup-playback.json`。

这是本机 Windows 10 的原生验收。未做所有 GPU/驱动、实际硬解单元分析、物理设备热拔插、数小时长片、HDR/全编码组合、真实浏览器 SODA 和桌面逐项实点测试。PNG 由播放器自己的 Direct2D 缓冲导出。旧报告是历史记录，其旧版本日志已收入备份，当前 test/results 会随新测试更新。

复验：`.\test.ps1`，`.\build.ps1 -Install`，`.\test\smoke-release.ps1 -Media test/sample.mp4`，`.\test\measure-playback.ps1`，`python .\test\profile_playback.py`，`.\test\profile-playback.ps1`，`python .\test\finalize_playback_report.py`。烟测要求没有已有播放器进程且本地字幕偏好已开启；会保留当前配置。性能脚本使用独立配置；清理记录保留本次交付时的数据，重新构建会再次生成临时目录。

API 依据：[SourceReader 数据处理与格式变化](https://learn.microsoft.com/en-us/windows/win32/medfound/processing-media-data-with-the-source-reader)、[有效显示区域](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-mt-minimum-display-aperture-attribute)、[系统高级视频处理器](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-source-reader-enable-advanced-video-processing)、[WASAPI 共享与事件初始化](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize)。
