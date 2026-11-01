# Zipformer CTC INT8 接入验收（Lumen Player 1.2.0）

> 历史记录：当前播放等待逻辑、兼容性结果与正式程序散列见 [1.3 播放与兼容性验收](playback-validation.md)。本报告对应的 1.2 程序和证据已备份；test/results 会随新测试更新。

日期：2026-10-19。环境：Windows-10-10.0.19045-SP0，Intel Core i5-7200U（2 核 / 4 线程）。正式程序为 `D:/LumenPlayer/dist/LumenPlayer.exe`。

## 已交付行为

- 默认本地引擎由 Vosk 更换为 `sherpa-onnx-streaming-zipformer-ctc-zh-int8-2025-06-30`。保留 `engine=0` 配置映射，设置显示 Zipformer CTC；浏览器 SODA 仍是选项 1。
- sherpa-onnx 1.13.8 原生 C API + 随同一 release 提供的 ONNX Runtime 1.28.2；CPU 单线程、greedy、16kHz/80-bin 特征。没有 Python 运行依赖，不需要独立显卡。
- 在后台加载模型，解码线程处理归一化 float 音频，每次最多 200ms。队列以条件变量唤醒，最多保留 2 秒音频，超载时丢弃旧积压并重建流。
- 字幕先显示 partial；静音 0.8 秒的 endpoint 规则和最长 15 秒分段规则负责提交。实际提交还受模型右侧上下文影响，0.8 秒不是端到端显示延迟。
- 跳转立即清空旧 partial、重建识别流，并使旧代次的在途识别失效。音频采集交付与定位互斥，暂停时定位也生效。结束输入和停止均补齐末句，避免重复提交或对已结束的流继续喂音频。
- 导出使用模型字级时间戳与 segment start_time，叠加真实媒体起点；补充的静音不延长字幕时间。关闭字幕释放流、识别器和两套原生 DLL，历史字幕可继续导出。

## 功能验证

完整原生回归 **139 项断言通过**；日志见 `test/results/regression.log`。断言含不同窗口尺寸/缩放的重复布局检查。最终回归在 dist 已删除全部 Vosk 模型与运行库后执行。

| 范围 | 实际覆盖 |
| --- | --- |
| 播放与界面 | 软件/硬件优先路径、WAV/MP3/M4A/FLAC、H.264/AAC MP4、无音轨视频、播放/暂停/定位/结束/重播、现有输入与布局检查 |
| 流式字幕 | 中文真实音频、首个 partial、暂停后定位立即清除旧结果、片尾末句、结束后重播、停止时提交在途句子 |
| 时间戳与导出 | 非零媒体起点、连续 endpoint 后分段偏移、不超出真实音频末尾、SRT 保存与回读 |
| 输入适配与资源 | 48k 双声道 downmix/重采样、反复加载与关闭、原生 DLL 确实卸载、无媒体不加载 |
| 正式 EXE | 安装后的真实进程加载 CTC 与 ONNX Runtime、未加载 Vosk、进入输入空闲、窗口响应及正常退出 |

开发期间的首轮组合专项曾出现一次“播放到 EOF 且有已提交字幕”的超时，之后加入了详细状态诊断；独立专项和两轮完整回归通过，未再次复现。该现象没有足够证据归因于某个代码缺陷，不宣称完成所有负载条件下的稳定性证明。

## 本机实测

| 指标 | 本次记录 |
| --- | ---: |
| UI 发起模型加载返回 | 0.06 ms |
| 模型后台加载到可用 | 3428 ms |
| 模型就绪后从头播放到首条 partial | 528 ms |
| 8.825 秒样例，进程 CPU 时间 / 音频时长 | 0.1841 |
| 回归进程私有提交：加载前 / 已加载 / 停止后 | 146.4 / 267.3 / 46.7 MiB |
| 正式 EXE 烟测识别时私有提交 / Working set | 265.2 / 290.9 MiB |
| 正式 EXE 进入输入空闲 | 171 ms |

这些是本机单次记录，不是稳定基准。CPU RTF 包含同一原生进程的音频播放、识别、UI/测试消息循环；本次相当于平均约占一个逻辑核 18.4% 的 CPU 时间，不能直接视为任务管理器整机 CPU 百分比。首个 partial 从播放动作计时，采集允许最多约 0.3 秒预读，不等同于通用语音活动起点到文字显示延迟。私有提交不等同于物理内存；完整回归的加载前值还受此前媒体解码流程影响。GPU 识别未测试。

原始记录：

- `METRIC software_audio_open_ms 287.93`
- `METRIC software_video_open_ms 117.30`
- `METRIC software_video_eof_wait_ms 3052.28`
- `METRIC hardware_preferred_open_ms 319.84 actual_hw=1`
- `METRIC caption_start_dispatch_ms 0.06`
- `METRIC caption_model_load_ms 3427.84`
- `METRIC caption_first_partial_ms 528.19 media_pos=0.515`
- `METRIC caption_playback_cpu_rtf 0.1841 wall_s=8.946 audio_s=8.825`
- `METRIC caption_private_bytes before=153505792 loaded=280240128 stopped=48955392`

## 识别质量复核

使用前轮相同 WenetSpeech DEV0 音频（5.6115 秒），这次经过正式 C++ 接入路径：

- 参考：对我做了介绍啊那么我想说的是呢大家如果对我的研究感兴趣呢嗯
- 输出：对我做了介绍啊那么我想说的是呢大家如果对我的研究感兴趣呢
- 字符编辑距离 1 / 29，CER **3.45%**；完整结果见 `ctc-native-quality.json` 和 `ctc-wenetspeech.srt`。

参考标注来自 [EvalScope WenetSpeech 示例](https://evalscope.readthedocs.io/zh-cn/latest/benchmarks/wenet_speech.html)。只有这一条有标注音频，不能代表噪声、口音、中英混说、专名、音乐背景及整套数据集的准确率。项目自身语音样例另测通过，但没有用它宣称通用 CER。

## 交付与保留

- EXE：419840 bytes，SHA-256 `46d9c1dcd9292453ff801d8218f030279fa743cd3605f7fdeb21640d26d6d53f`。
- 本地权重与 tokens 合计 154.8 MiB；发布文件合计 **176.4 MiB**（不含 INI/日志），比原包增加约 58.6 MiB。
- 从 dist 删除的旧 Vosk 资源 116.7 MiB，删除前 18 个文件逐一确认与 `third_party/m`、`third_party/v` 原件 SHA-256 一致；原件保留。
- 清理本轮下载压缩包及 staging 中的重复运行资源 306.3 MiB，保留构建所需的 third_party 原件、已验证 EXE 和备份；下次构建会重新组装 staging。
- 替换前源码、EXE 与 INI 在 `build/backups/pre-zipformer-20261018.zip`。安装前当前 INI 快照为 `build/backups/pre-zipformer-install-20261019.ini`；安装和烟测后散列一致 `729c908ac9eb39b6cbb979485335be3ee01fe4392b370b08aae1b894e53fe6f6`。
- 较早源码备份的 INI 音量为 0.85，安装前当前值为 0.82；交付保留当前 0.82，没有用旧备份回滚用户设置。
- 原生运行库来自 [sherpa-onnx v1.13.8](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v1.13.8)，模型包与 runtime 包来源/散列记录于 `dist/licenses/Zipformer-model-NOTICE.txt`。构建脚本从本地固定依赖组装，不联网更新版本。

运行库开源许可不等于权重许可。选定模型包 README 指向的 [原始 checkpoint](https://huggingface.co/yuekai/icefall-asr-multi-zh-hans-zipformer-large) 有访问条件，模型包未附独立权重许可；本次为本机接入，未接受 gated 条款或公开发布，公开再分发需核对模型条款。

## 边界与复验

UI PNG 来自播放器自身 Direct2D 缓冲，已查看设置小窗口及实际字幕渲染；它们不是桌面鼠标实点截图。未完成真实浏览器 SODA 联调、独立显卡 provider、数小时长片/持续超载压力、全部硬件/编码器和人工唇音同步验收。倍速识别保持原有暂停采集行为。

```powershell
.\test.ps1
.\build.ps1 -Install
.\test\smoke-release.ps1
python .\test\finalize_ctc_report.py
```

烟测要求关闭已有播放器且当前设置启用了本地字幕；不修改当前偏好。完整文件清单与散列见 `release-manifest.json`。
