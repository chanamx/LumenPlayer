# Video compatibility implementation plan

**Goal:** 扩大本地视频的软件播放覆盖，消除原生 SourceReader 缺少解码器和 TS 动态协商造成的播放缺口，并保持字幕等待、暂停、定位和 EOF 行为。

**Architecture:** 保留系统 MFPlay/EVR 首选和现有 WASAPI/GDI 软件输出。增加一个 FFmpeg 本地文件解码适配器，将视频转换成 BGRA、音频转换成输出设备 PCM，复用现有缓冲、时钟和交互。原生软件准备失败或运行失败时只回退一次，位置和播放意图保持；字幕采集使用同一解码适配器的仅音频模式覆盖原生无法打开的容器。禁止解码适配器自行打开外部网络/引用资源。

**Tech Stack:** C++17、Media Foundation、WASAPI/GDI、固定散列的 FFmpeg 9.0 LGPL shared 构建、现有 sherpa-onnx。

**Validation refinement:** 软件视频统一优先 FFmpeg，普通纯音频先走系统路径；系统视频读取器作为依赖缺失时的备用。这样绕过已复现的 TS 动态协商问题并统一定位/排空逻辑，避免扩展名特判不断增加。AVI 定位测试的首轮阈值还暴露了样本本身 0.066→0.200 秒的合法时间戳空档，已按目标附近实际可用帧验证，不能将这一空档当成原生解码器错误。

**Track validation refinement:** 系统来源会忽略 FFV1 视频轨而只暴露音频，因此原生音频快路径缩至文件头确认的 WAV/FLAC/AIFF，其余容器统一独立解析。所有有效样本增加视频/纯音频轨结构断言，视频还必须实际出帧。H.264 4:4:4 已复现系统解码无法结束、析构关闭阻塞；按已选流 profile 在创建系统拓扑前转入软件路径。

## Decisions

- 仅修复 Windows API 不足以补齐本机 HEVC/Ogg 的解码器/源解析缺口；外部转码会增加等待、临时磁盘和进程控制成本。选择进程内软件解码回退，不生成转码副本，不调用外部 ffmpeg.exe 播放。
- 使用 FFmpeg 官方下载页列出的 BtbN LGPL shared release/9.0 构建，固定归档散列，保留头文件、来源及许可；只随播放器附带五个运行库，按需动态载入。
- 无 Git 仓库，修改前源码、1.3 EXE、配置和测试证据已备份到 build/backups/pre-video-compatibility-20261019.zip。

## Steps

1. src/ffmpeg.h/.cpp：本地 Unicode 文件 AVIO、容器/流选择、发送/接收/EOF 排空、定位清空、BGRA 色彩/像素比例、音频重采样与有界资源使用。
2. src/player.h/.cpp：复用软件输出，准备失败与运行失败的单次回退、动态尺寸、时间起点规范化；暂停定位及解码 EOF 保持一致。
3. src/player.cpp、src/main.cpp：字幕仅音频回退、准确状态和失败反馈，保留模型等待屏障、加载复用及用户播放意图。
4. build.ps1：固定 SDK 头文件与 DLL/许可打包，保留 INI；缺少依赖时明确构建错误。
5. test/make_compat_fixtures.py、test/regression.cpp：增加 HEVC 8/10 bit、AV1/VP8/VP9 WebM、FLV、MPEG2/TS、不同色度/尺寸、非零时间起点、旋转、视频无音轨及错误样本。每条软件路径实际解码，定位、片尾和重播不以仅打开成功代替。
6. TS 及回退重复压力测试、完整原生回归、正式程序实际视频与字幕烟测；记录支持矩阵、时序、内存与新增依赖大小。
7. 版本、README、验收报告和 manifest，核验配置与所有交付文件散列；清理本轮临时完整 SDK/构建副本，保留复现脚本、原始依赖和备份。

## Completion

2026-10-19 已完成：完整回归 650 项断言，68 条格式/偏好组合含 66 条有效媒体播放和 2 条损坏输入拒绝；直接解码 10 个视频并验证 5 种 PCM/float 输出。正式 1.4.0 EXE 的系统 H.264、软件 HEVC、特殊字符安装路径下的 WebM/实时字幕烟测均通过。1.3/1.4 软件播放交错各测四次，原始结果和指标边界见 docs/video-validation.md。当前用户 INI 与安装前快照相同，新增依赖已固定散列及许可；保留 SDK/核心源码归档，清理约 696.62 MiB 重复构建和测试副本。版本、README、当前验收报告与 release-manifest.json 已更新。
