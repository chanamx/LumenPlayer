# Lumen Player 2.2.5

Windows 原生视频与音频播放器。标准播放模式使用固定版本的 libmpv、FFmpeg、libplacebo 和 libass；保留原有 Media Foundation / FFmpeg 兼容模式。Lumen 管理原生界面、播放列表及可选模型的离线实时字幕与翻译。

## 使用

打开 dist/LumenPlayer.exe，保留同目录 DLL、licenses 和 shaders。设置与列表分别保存在 LumenPlayer.ini、LumenPlaylists.dat。日志只保留当前与一个前序文件。实时字幕结果在内存中，只有手动保存才写出 SRT；关闭软件丢弃未保存结果。安装包不包含语音或翻译模型；首次使用在设置的“模型与语言”中选择下载。

设置中的“音轨 · 字幕轨 · 音频 · 播放控制”，以及 T / 鼠标右键，可打开增强菜单。“画质与 GLSL”打开图像调节菜单。标准／兼容模式的选择在下一次打开媒体时生效。

## 解码、网络与渲染

标准模式使用 gpu-next / Direct3D 11。硬件优先自动选择可用解码器，失败时使用软件；支持当前视频内切换硬件优先／软件策略，状态来自实际解码器。AV1、HEVC 10 bit 等格式可通过软件补充本机硬件缺少的解码能力。具体硬解覆盖取决于显卡、驱动与编码配置。

网络入口支持 HTTP(S)、RTSP(S)、RTMP(S)、RTP、UDP、SRT，支持 HLS 和 DASH。MPD 作为一个媒体源，分片不展开成零散列表条目。协议能否连接仍取决于服务器、认证、网络与媒体编码。M3U / M3U8 频道清单作为独立列表文件夹；HLS 清单保持单个条目。保留节目名、相对地址、签名查询参数，重复导入时合并去重。

播放、网络加载与核心控制在后台处理，连续定位采用最终请求。媒体与 GPU 缓存受限，放在应用目录 cache/mpv。

HDR 输出可自动匹配显示设备，也可固定 SDR。可调色调映射方法、色域映射、动态峰值分析、平滑响应、显示峰值与参考白，支持外部 mpv GLSL / hook。SDR 映射主要使用参考白，HDR 输出上限使用显示峰值。自动 HDR 输出需要系统、显卡与显示器共同支持。

## 音频与功放

可选输出设备、系统共享 PCM、独占 PCM、IEC 61937 压缩直通。支持 AC3、EAC3、DTS、DTS-HD、TrueHD 的直通请求；设备或独占格式不接受时回退默认设备的共享 PCM，显示实际输出与原因。设备菜单是用户偏好，运行状态才表示实际协商结果。

实时字幕、变速与音高处理需要 PCM；这些情况下暂时关闭压缩直通，恢复条件后按用户偏好重新输出。压缩节目音频不能同时做播放器音量、下混或时间伸缩处理。

Atmos / DTS:X 扩展可通过兼容压缩载体交给支持的 HDMI 功放解码。普通 PCM 解码表示床声道／兼容音频解码，不能恢复已丢失的对象或高度信息。本版不宣称实现本地 Atmos / DTS:X 对象重建。软件封装与原始包保留可独立验证，不替代物理功放及声场验收。当前主机为立体声设备，实际 HDR 显示与对象功放输出有额外设备验收边界。

## 轨道与字幕

音轨、内嵌与外挂字幕轨显示语言、标题、编码、默认／强制标记，可手动切换、关闭字幕或恢复自动。语言优先级提供中文／英文／日文及自定义代码。每个新文件重新按语言偏好和容器默认标记选择，不复用上一文件的临时轨道编号。

标准模式由 libass 保留 ASS / SSA 原始排版、定位、颜色、动画、卡拉 OK 和字体，支持容器内嵌字体。支持 SRT、VTT、SMI / SAMI 等 FFmpeg 字幕格式，亦可导入受支持的图形字幕。SAMI 同文件内的多个语言样式类由当前解复用器作为一个字幕轨处理，不自动拆成多个语言轨。

实时字幕可选择 Zipformer CTC INT8 中文识别，以及 Moonshine 日语、英语和德语流式识别，离线 CPU 单线程。定制核心从选中音轨取 PCM，同一网络播放连接不另开识别连接，静音不影响识别输入。拖动、定位、切轨、循环重置采集时间线。实时字幕开启时优先显示识别结果，避免与文件字幕重叠；关闭后恢复文件字幕。

已开启实时字幕时打开新媒体，会等模型准备完成再开始播放；观看过程中在设置中开关实时字幕由用户控制播放状态。网络源稍后出现音轨时会自动启动识别，模型在后台加载。加载网络媒体时可按 Esc 取消实际连接，再打开其他媒体。

### 模型与语言

按 G 打开设置，选择“模型与语言”。先选模型和下载源，点“下载安装”；完成后点“用作识别”或“用作翻译”。下载过程可取消，重试继续已下载的部分。仅下载不会自动启用模型，按 C 开启实时字幕。

| 模型 | 用途 | 适用选择 |
| --- | --- | --- |
| Zipformer CTC INT8 | 中文识别 | 保留原有中文质量基线；属于既有模型 |
| Moonshine 日语 Tiny | 日语识别 | 优先轻量实时识别 |
| Moonshine 英语 Tiny | 英语识别 | 优先较低占用与延迟 |
| Moonshine 英语 Small | 英语识别 | 较高识别质量，占用和延迟也更高 |
| Moonshine 德语 Tiny | 德语识别 | 补充选项，验证范围较日英窄 |
| NanoSakura 0.3B INT8 | 日语文本译为简体中文 | 约 282.5 MiB；轻量专用翻译，搭配日语识别使用 |
| Hy-MT2 1.8B Q4 | 多语种文本翻译 | 中配以上可选，需先安装对应语音识别模型 |
| Index-Translate 2B Q4 | 多语种文本翻译 | 中配以上可选，需先安装对应语音识别模型 |

Hy-MT2、Index 翻译目标可选中文、英语、日语、德语、法语；这不代表提供对应所有语言的语音识别。中英互译可分别选择中文／英语识别模型与翻译目标；中日互译同理。NanoSakura 只支持日语→简体中文：先将“日语 · Moonshine Tiny”用作识别，目标设为中文，再将 NanoSakura 用作翻译。源语言由所选识别模型决定，暂不自动切换混合语言。法语语音识别尚未列入可安装列表。

翻译在句末后生成，准备期间或本句未及时完成时先显示识别原文。翻译队列有上限，性能不足时较早句子保留原文。Hy-MT2、Index 在当前 i5-7200U 测试机上每句通常还需数秒；NanoSakura 使用较小的专用模型，仍有整句翻译开销。低配机器可选“仅显示原文”。有译文后，保存字幕可选择原文或双语 SRT。

翻译也可能误译。本轮新接口测试中，Index 曾把日语“降低两度”译成“降低两次”；Hy-MT2 在这一例中保留了温度单位。小范围的连通验证不代表所有片源或语义都正确。

下载源包括固定版本的 Moonshine 官方、Hugging Face、HF 社区镜像、魔搭社区，以及 GitHub 发布和 API 备用入口，具体以所选模型显示的来源为准。自动模式在连接失败、响应不匹配或校验失败时尝试其他入口；收到 403/429/503 后，本次安装不再请求该站点，可继续尝试独立来源。手动选择来源时，模型主体保持该选择；缺少对应辅助文件或运行组件时会显示说明。每个文件核对大小与 SHA256，全部文件通过后才显示已安装。日语、德语现有 ORT 包尚未找到完整一致的第二个下载渠道，保留官方源。下载文件与模型在应用目录 models 下；已有中文模型目录继续识别，不自动迁移或删除。应用的“删除模型”会先停止使用该模型，再清理其模型文件与未完成下载，保留无关文件。升级与系统卸载均保留用户自行下载的模型，可在卸载前通过模型列表主动删除。

CAT-Translate 0.8B 已完成下载与日英测试，但仍出现原语言返回和关键条件错译，本轮未列入可安装模型。此前的模型比较及实测范围见 [docs/priority-caption-results-2026-10-27.md](docs/priority-caption-results-2026-10-27.md)；2.1.0 模型管理验收见 [docs/caption-model-manager-2026-10-27.md](docs/caption-model-manager-2026-10-27.md)。本次 NanoSakura、下载源及 2.2.0 实测见 [docs/models-2.2.0-2026-10-29.md](docs/models-2.2.0-2026-10-29.md)。

## 操作

| 操作 | 效果 |
| --- | --- |
| 空格 / 单击画面 | 播放、暂停 |
| ← / 点按 → | 后退、前进 5 秒 |
| 按住键盘 → | 临时倍率；松开恢复之前的倍率与播放状态 |
| 长按左键 | 临时使用设置的长按倍率（1.25–8×），松开恢复 |
| T / 鼠标右键 | 音轨、字幕、音频与播放菜单 |
| 菜单“播放速度” | 0.1–12×，默认保持音高，可输入自定义值 |
| , / . | 暂停并后退／前进一帧 |
| [ / ] / 反斜杠 | 设 A 点／B 点／清除 A–B 循环 |
| ↑ / ↓ / 滚轮、M | 音量、静音与恢复 |
| F / F11 / 双击画面 | 全屏 |
| O / Ctrl+O、Ctrl+U | 打开文件／网络地址 |
| S / C、G / F1 | 字幕／实时字幕；设置／帮助 |
| P、⏮ / ⏭、PageUp / PageDown | 列表／上一项／下一项 |
| Tab / Shift+Tab、回车 | 选择并操作控件 |
| Home / End、Esc | 开头／结尾；取消媒体加载／关闭浮层／退出全屏 |

逐帧、循环、定位与变速需要源支持；无可定位时间线的直播禁用这些操作。极端变速保持音高，但仍可能改变瞬态和声音自然度。

播放列表占据右侧完整高度，控制条随之缩短。可拖动分隔线调宽，双击或“重置”恢复。保存列表文件夹及上次位置；支持多选添加、自然排序导入磁盘文件夹、顺序连播、跳过缺失文件。

## 构建与验证

2026-10-24 窗口白边修复及验证范围见 [docs/window-frame-validation-2026-10-24.md](docs/window-frame-validation-2026-10-24.md)。

历史窗口修复发布的证据校验入口为 `python test/finalize_window_frame_release.py --check-only`；完整核心与网络历史发布校验使用 `test/finalize_core_release.py`，其要求对应构建的全矩阵证据。2.1.0 模型管理发布使用 `python test/finalize_caption_manager.py --check-only` 核对本轮独立记录；历史二进制绑定记录不代表新版本重新通过了全部旧硬件与协议矩阵。

2.2.5 使用 `python test/finalize_click_response_release.py --check-only` 核对本版源码、正式运行文件、安装包和实测证据；该检查不要求用户使用后的设置和列表仍保持发布时内容。2.2.4 的 `test/finalize_hold_fix_release.py`、2.2.3 的 `test/finalize_click_fix_release.py`、2.2.2 的 `test/finalize_tuning_release.py`、2.2.1 的 `test/finalize_model_ui_release.py`、2.2.0 的 `test/finalize_nanosakura_release.py` 保留为历史发布校验。2.1.2 实施结果与验证范围见 [docs/optimization-results-2.1.2-2026-10-28.md](docs/optimization-results-2.1.2-2026-10-28.md)，对应的 `test/finalize_optimization_release.py` 同样保留。升级后卸载会保护旧卸载记录中仍被标记删除的设置和列表，备份失败时在删除前停止。

下载、缓存与构建输出留在本项目。默认编译器使用 third_party/mpv/build-provenance.json 记录的既有 LLVM-MinGW UCRT 工具链；可用 build.ps1 的 -CompilerPath 显式指定兼容的 UCRT 编译器。构建记录编译器身份，并检查生成的 EXE/DLL 导入表，拒绝 MSVCRT 产物。先运行 python test/build_mpv_core.py 构建固定版 LGPL 核心，再运行 ./build.ps1 -Target Release。发布构建在 build/staging，dist 在验收后替换，构建不覆盖用户设置和列表。

2026-10-24 磁盘与字幕内存优化的前后测量、实时性和 CPU 开销见 [docs/footprint-validation-2026-10-24.md](docs/footprint-validation-2026-10-24.md)，完整发布证据见 [docs/core-validation.md](docs/core-validation.md)。正式目录与新安装包支持 Windows 透明文件压缩，降低实际磁盘占用；同目录的 caption-cpu.conf 用于降低字幕模型的分配器缓存，缺失或损坏时自动采用原默认配置。历史深度调优及 Ponytail 记录分别保留在 [docs/deep-tuning-validation-2026-10-23.md](docs/deep-tuning-validation-2026-10-23.md)、[docs/ponytail-validation.md](docs/ponytail-validation.md)。暂存目录中相同的固定 DLL 和模型通过硬链接共享；构建更新按文件替换，保护固定源文件，dist 仍为独立完整发布目录。

- ./test.ps1：原播放系统回归，默认使用 build/tests/runtime；可用 -RuntimeDirectory 指定其他隔离目录，拒绝 dist。
- ./build.ps1 -Target OptimizationTest 后运行 python test/run_optimization_checks.py boundary --runtime build/tests/runtime：完整双语导出、重复字幕、译文刷新及加载取消；joint 模式验证真实识别/翻译联合链路，需先准备对应测试模型。
- ./build.ps1 -Target ModelTest 与 test/run_caption_model_store.py：原生模型目录、实际传输、校验、取消续传、源回退与删除保护。
- ./build.ps1 -Target CaptionTest 与 test/prepare_caption_integration.py：隔离目录内的实际播放、日英中识别、翻译、跳转及模型释放；不修改正式用户设置。
- ./build.ps1 -Target CoreTest 及 build/tests/runtime/LumenCoreRegression.exe：新核心与原生 UI、模型集成。
- test/test_mpv_pcm_tap.py：多声道 PCM、EOF、注销与销毁并发。
- test/test_core_audio_rates.py：0.1–12× 实际时长、非静音与音高。
- test/run_core_protocol_checks.py：HTTP、DASH、RTSP、RTMP 真实播放及同连接取音。
- test/test_core_iec61937.py：压缩载体、IEC burst、原始包保留；不替代功放验收。
- test/test_backend_cancellation.py build/staging/LumenCancelProbe.exe：停滞网络连接的实际取消、重新打开与退出。

网络协议验收每次生成 `test/results/core/protocol-runs/<运行编号>/report.json` 和独立日志。`protocol-acceptance-latest.json` 仅指向最近一次播放器验收；`--probe-only` 使用独立的 probe 目录与指针。失败和超时也保留现场。`python test/finalize_core_release.py` 会解析指针、核对具体运行及日志 SHA256，并让报告引用该运行；亦可用 `--protocols <具体 report.json>` 明确选择证据。原 `protocol-acceptance.json` 保留为历史失败记录。

源、补丁与依赖哈希在 third_party/mpv/build-provenance.json、dependency-lock.json、test/mpv_pcm_tap.patch。修改后的核心源码在 third_party/mpv/mpv-6a262d11f6，许可证在 dist/licenses/playback-core。工程无 Git 元数据，升级前恢复包在 build/backups/pre-core-20261021.zip。1.8 画质文档是历史记录，不替代 2.0 新验收。
