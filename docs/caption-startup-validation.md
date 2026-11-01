# Lumen Player 1.4.1 字幕模型启动验收

日期：2026-10-19。平台：Windows-10-10.0.19045-SP0；Intel Core i5-7200U，2 核 / 4 线程。

## 交付结果

正式版本 **1.4.1** 已安装到 `dist/LumenPlayer.exe`。开启实时字幕、同一视频从新进程启动到模型/tap 就绪并释放起点播放，中位数从 **3681 ms** 降到 **2086 ms**，减少 **43.3%**。比较为本轮原样备份的 1.4.0 EXE 与最终 1.4.1 EXE，交错各 4 次。

模型仍为 2025-06-30 的 Zipformer CTC INT8，sherpa-onnx 1.13.8 / ORT 1.28.2 原生运行库和 CPU 单线程、greedy、端点参数均不变。用同版本 ORT 将扩展图优化提前执行，运行目录采用预先序列化的 `model.int8.ort`，省去每次创建会话时重复执行的图优化。没有通过后台常驻模型、降低识别设置或跳过起点等待来缩短表面时间。

## 实现与资源行为

- `src/soda.cpp` 优先选择 ORT 文件；旧包只有 ONNX 文件时仍支持原路径。增加 DLL 加载、会话创建、其余准备与总时间的分段日志，便于以后区分瓶颈。
- `test/prepare_caption_model.py` 使用本项目原生 DLL 转换，校验原始模型和运行库散列；不会使用全局 Python 的不同 ORT 版本，也不会联网下载。采用 CPU `ORT_ENABLE_EXTENDED`，没有布局优化、重新量化、剪枝或参数缩减。
- 模型仅在有音轨媒体开启字幕时加载。关闭字幕释放识别器、模型和运行库；换文件复用就绪模型的行为保持，取消、暂停、无音轨和启动失败的等待处理均保留。
- 运行模型 163,175,496 字节（155.62 MiB），比原始文件增加 **0.84 MiB**。运行目录只保留新文件，原始 ONNX 完整保存在 `third_party/models/sherpa-onnx-streaming-zipformer-ctc-zh-int8-2025-06-30`。安装只删除散列匹配的旧运行副本，自定义文件保留。完整运行包 291.74 MiB。
- ORT 序列化不保证每次得到相同字节。本次交付使用固定文件散列 `a21776f1202c3e2488399791e7f986bdc20e2bd0d4ba47578c9326deb6c89f37`，每次重建先对比两段原生识别输出再记录该产物的准确散列；来源和选项见模型目录 `lumen-model.json`。运行库仍由既有开源许可覆盖，权重条款保持原限制。

## 正式程序对照

同一 6 秒 1280×720 H.264/AAC 视频，软件解码、实时字幕开启、音量 0。每次使用新进程及独立配置，轮换先后顺序；观察起点等待、模型启动、tap 就绪和 `Caption playback released at 0.000s pending=1` 后取等待时间。随后播放 850 ms，采集内存和 CPU，再正常关闭。CPU 列包括启动与这段播放，不是单独推理耗时。输入空闲和播放等待释放是不同指标。

| 指标，中位数 | 1.4.0 原始 ONNX | 1.4.1 预优化 ORT |
| --- | ---: | ---: |
| 启动到释放播放等待 | 3680.6 ms | 2086.2 ms |
| 启动到输入空闲 | 253.3 ms | 329.9 ms |
| 工作集 | 312.0 MiB | 301.2 MiB |
| 私有提交 | 300.1 MiB | 290.1 MiB |
| 进程峰值工作集 | 314.9 MiB | 446.3 MiB |
| 此窗口进程 CPU 时间 | 3.883 s | 2.273 s |

当前默认 ORT 加载方式的启动峰值工作集升高，而稳态工作集和私有提交略降。额外测试了文件映射及映射初始权重，未消除这一峰值，未将这些加载选项接入正式程序；原始记录为 `startup-mapping-probes.json`。启动时内存峰值是本次速度收益的实测取舍，不能只用稳态数字概括资源开销。

各版本第一次采样分别为 4465 / 5466 ms。没有清空系统文件缓存或重启电脑，不能把此数值当成断电后的磁盘冷启动；主指标是新进程的实际播放等待。完整记录 `caption-startup-comparison.json` 包含 EXE 散列、版本、选中模型及响应状态，逐轮日志保存在 `startup-benchmark-logs`。

本轮首个新版样本总等待较旧版首轮长。其日志中，WASAPI 就绪到 FFmpeg 库加载完成的间隔约 2.9 秒，模型会话本身为 1942 ms。因此本轮可以证明重复新进程的中位等待下降，不能保证每一次首次启动都更快，也不能把该次解码库准备的开销算作模型初始化。

| 顺序版本 | 轮次 | 释放播放等待 | 工作集 | 窗口响应 |
| --- | ---: | ---: | ---: | --- |
| candidate | 1 | 5466.0 ms | 299.8 MiB | True |
| baseline | 1 | 4465.4 ms | 312.4 MiB | True |
| baseline | 2 | 3654.4 ms | 311.6 MiB | True |
| candidate | 2 | 1991.8 ms | 301.0 MiB | True |
| candidate | 3 | 1987.0 ms | 302.9 MiB | True |
| baseline | 3 | 3685.2 ms | 311.0 MiB | True |
| baseline | 4 | 3676.0 ms | 313.2 MiB | True |
| candidate | 4 | 2180.5 ms | 301.4 MiB | True |

## 识别、交互与视频验证

完整原生回归通过 **651 项断言**；68 条格式/偏好组合仍为 **66 条有效媒体播放通过、2 条损坏输入拒绝**。覆盖 UI 输入/布局、系统和软件解码、定位/片尾/重播、故障回退、字幕等待、加载中换文件/暂停/取消、无音轨、EOF 末句及资源释放。完整日志 `regression.log`，分段模型记录见 `backend.log`。

转换前后在同一个原生单线程 C API 中，对 `zh_speech.wav` 和 `wenetspeech-dev0.wav` 的最终文本及首次文字所需音频量一致；当前播放器的 WenetSpeech 输出也与历史留存一致。此验证覆盖两段音频及现有实时流回归，不构成新语料榜单或所有语音输出逐字相同的保证。模型优化只改变加载/图表示，没有增加精度降低步骤。

- 模型准备专项原生记录：1736.1 ms（含该测试内文件切换/暂停步骤）。
- 就绪后从头播放到首条文字：原生 WAV 497.3 ms；WebM/Opus 654.5 ms。
- 已就绪模型切换视频：247.9 ms；短中文播放进程 CPU RTF 0.1576。
- 正式 EXE 的系统 H.264、软件 HEVC、中文/空格/`#`/`%` 软件目录下 WebM 均验证了 ORT 选中、模型及 tap 就绪、起点释放、窗口响应和正常关闭。
- 用户 INI 与安装前快照完全一致，三个 ASR DLL、五个视频 DLL、tokens 和原始 ONNX 散列均核验。

## 恢复、清理与边界

修改前的源码、1.4.0 EXE、配置和证据保存在 `build/backups/pre-caption-startup-20261019.zip`。原始模型无需从备份解压，仍保留在 `third_party/models`；恢复旧程序时可将原始 `model.int8.onnx` 放回运行模型目录。当前程序也兼容只保留原始 ONNX 的旧包。

已清理本轮重复模型候选、隔离测试包及构建运行副本，共 1476.41 MiB；正式模型、原始模型、源码、依赖、备份与验收结果保留。明细 `cleanup-caption-startup.json`，交付散列见根目录 `release-manifest.json`。

这是本机 Windows 10 x64 验收，未覆盖全部 CPU/系统、磁盘冷缓存、数小时视频或所有真实语音。未启用硬件专属布局优化有助于保留 CPU 可移植性，但不能用本机结果证明所有旧 CPU 的性能。实际 GPU 解码和视频功能边界沿用 1.4 报告；原始 1.4 证据已备份，工作目录 test/results 会随新测试更新。

复验：`python test/prepare_caption_model.py`（已有模型会校验后复用）、`.\test.ps1`、`.\build.ps1 -Install`、`.\test\smoke-release.ps1 -Media test/sample.mp4`、`.\test\smoke-release.ps1 -Media test/results/compat/hevc.mp4 -ExpectCompatibility -ResultName release-hevc-smoke.json`、`.\test\smoke-path-release.ps1`、`.\test\measure-caption-startup.ps1`。安装前需另存当时的用户 INI，清理记录反映本次交付，重建会再生成临时副本。

原始依据：[ORT 离线图优化](https://onnxruntime.ai/docs/performance/graph-optimizations)、[ORT 格式与加载](https://onnxruntime.ai/docs/performance/model-optimizations/ort-format-models.html)、[同版本 sherpa Zipformer 会话创建](https://github.com/k2-fsa/sherpa-onnx/blob/v1.13.8/sherpa-onnx/csrc/online-zipformer2-ctc-model.cc)。
