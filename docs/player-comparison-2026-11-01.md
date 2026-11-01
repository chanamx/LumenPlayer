# Lumen Player 2.2.4 与 VLC 3.0.24、PotPlayer 261021、mpv 0.41.0 对比

- 文档日期：2026-11-01
- Lumen 基线：2.2.4，2026-10-31 发布；播放核心为定制 libmpv `6a262d11f6`（2026-10-19）
- VLC 基线：3.0.24 "Vetinari"，2026-09-22 发布，当前稳定版
- PotPlayer 基线：261021，2026-10-21；Windows 7–11，x86／x64／ARM64
- mpv 基线：0.41.0，2025-12-21 稳定版；2026 年 master 构建持续更新
- 说明：VLC 4.0 桌面版尚未发布；Vega OS 上已有基于 libvlc 4 的 beta。PotPlayer 261021 的发布日期按 VideoHelp 记录，官方版本历史页当前可读到 260819 等条目。

## 结论

**Lumen 在“播放画质 + 中文离线字幕／翻译 + 原生轻量 UI”这条线上有明确优势；在“通用播放器产品面”上弱于 VLC、PotPlayer，在“可配置性／可脚本化／跨平台”上弱于 mpv。**

| 对比对象 | 一句话判断 |
| --- | --- |
| VLC | 通用多媒体平台；Lumen 的画质调校、ASS 字幕、本地 AI 字幕更强，VLC 的格式／光盘／采集／录制／转码／投屏／跨平台／安全更新更强 |
| PotPlayer | 最接近的 Windows 消费级对手；PotPlayer 的格式面、3D、DVD／蓝光／TV、Dolby Vision／RTX HDR、EQ／ReplayGain、皮肤／多语言更全，Lumen 的集成式离线字幕／翻译、libplacebo 参数、libass、轻量原生 UI 更突出 |
| mpv | Lumen 的上游核心；mpv 更可配置、可脚本、跨平台，OpenGL／Vulkan、光盘菜单、截图／流录制更全；Lumen 提供 Windows 原生 UI、模型管理、ASR／翻译与直通状态 |
| 综合 | 没有单一“最强”：VLC 最全，PotPlayer 最像 Windows 全能消费播放器，mpv 最可折腾，Lumen 最专精 |

一句话：Lumen 是“Windows 高画质播放 + 离线字幕／翻译工具”，不是通用播放器的替代品。

## 一、基线与关键数据

### 1.1 Lumen 与 VLC

| 项目 | Lumen Player 2.2.4 | VLC 3.0.24 |
| --- | --- | --- |
| 版本／日期 | 2.2.4 / 2026-10-31 | 3.0.24 / 2026-09-22 |
| 平台 | Windows 10／11 x64 | Windows XP–11、macOS、iOS／tvOS、Android、Linux／BSD |
| 界面 | 原生 Win32，简体中文 | Qt，多语言 |
| 播放核心 | 定制 LGPL libmpv `6a262d11f6` + FFmpeg n9.0 + libplacebo 377 + libass 9 | libvlc + FFmpeg 8.1.2 + 49 个第三方库 |
| 渲染路径 | gpu-next／Direct3D 11 | Direct3D11、OpenGL 等平台输出 |
| 发布体积 | EXE 1,505,792 B；dist 顶层 41 个文件约 204 MiB；安装包 66,508,410 B（约 63.4 MiB），不含模型 | 官方安装包；本文不做体积对跑 |
| 可选模型 | 8 个 ASR／翻译模型，30.8–1,251.4 MiB／个 | 无本地 ASR／翻译模型 |
| 自有代码规模 | 35 个源文件，11,256 行 | 跨平台大型社区项目，本文未量化行数 |
| 更新／安全 | 发布检查器 + SHA-256；README 未描述在线自动更新或公开安全公告流程 | RSA-4096 更新校验；3.0.24 含 130+ 安全修复 |
| 本文证据 | 项目 README、docs、build-provenance、test/results | VLC 3.0.24 官方发布说明 |

### 1.2 PotPlayer 与 mpv

| 项目 | PotPlayer 261021 | mpv 0.41.0 |
| --- | --- | --- |
| 版本／日期 | 261021 / 2026-10-21 | 0.41.0 / 2025-12-21（master 2026 构建活跃） |
| 平台 | Windows 7–11，x86／x64／ARM64 | Windows、macOS、Linux、BSD |
| 界面 | 原生 Win32，皮肤，多语言 | 无完整 GUI，只有 OSC；配置／CLI；可选第三方前端 |
| 核心 | 闭源内置解码器 + OpenCodec；DXVA、CUDA、QuickSync | FFmpeg + libplacebo + libass；gpu-next 默认 |
| 视频输出 | D3D11 默认（261021），另有 D3D9 Ex Flip／Overlay | OpenGL、Vulkan、D3D11 |
| AI 字幕 | Whisper 实时字幕；在线或本地 LLM 翻译，需引擎／模型／服务 | 无内置 ASR／翻译 |
| 光盘／设备 | DVD／蓝光／TV／HDTV、3D、摄像头／采集、录制、截图 | DVD／蓝光 discnav（视构建）、截图、stream-record |
| 许可 | 闭源免费软件（非商业使用免费） | 开源；默认 GPL，libmpv 可 LGPL 构建 |
| 发布形态 | 官方 Windows 安装包 | 命令行／库，跨平台包 |
| 本文证据 | 官方版本历史、potplayer.tv、功能页、VideoHelp | mpv.io、mpv manual、0.41.0 release notes |

Lumen 2026-10-30 的独立播放内核文档仍是 Proposed，尚未替换 libmpv；本文按实际发布内核比较，不把“自研内核”计入现有能力。

## 二、2.2.4 相对上一版对比更新了什么

| 版本／项 | 变化 | 对比含义 |
| --- | --- | --- |
| 2.1.x | 模型管理器、完整双语 SRT 导出 | 本地字幕从“能识别”扩展到“可安装、可切换、可导出” |
| 2.2.0 | NanoSakura 日中翻译、多个固定下载源 | 新增离线翻译能力，VLC／mpv 无对应功能 |
| 2.2.0／2.2.1 | 日语、英语、德语 Moonshine 识别；模型与语言界面；DPI 支持 | 实时字幕从中文扩展到四语种 |
| 2.2.2 | 模型安装空间预检、成功回退后的缓存清理 | 工程可靠性提升，不改变强弱定位 |
| 2.2.3 | 单击／双击互斥 | 交互修复 |
| 2.2.4 | 长按倍率统一、卸载核心后 FFmpeg 日志回调修复 | 交互与稳定性修复 |
| 播放核心 | 定制 LGPL libmpv 2,753,536 B（约 2.63 MiB）、FFmpeg n9.0、libplacebo 377、libass 9 | 解码库版本比 VLC 3.0.24 的 FFmpeg 8.1.2 更新；核心来自 2026-10 mpv master |
| 独立内核 | 仅可行性提案 | 当前仍是 mpv 内核，不能按“已自研”计算 |

## 三、Lumen 与 VLC 对比

### 3.1 播放与画质

| 维度 | Lumen 2.2.4 | VLC 3.0.24 | 判定 |
| --- | --- | --- | --- |
| 硬件解码 | D3D11／gpu-next，硬件优先，失败软件回退，状态来自实际解码器 | D3D11、DXVA2 等，硬件解码默认开启 | 相当 |
| HDR／色调映射 | 自动／SDR／HDR 输出；Spline、BT.2390、BT.2446A、ST2094-40、Reinhard、自动共 6 种方法；动态峰值分析、响应速度、显示峰值、参考白可调 | 支持 HDR、HDR 色调映射、12-bit 与扩展色域 | Lumen 可调深度明显更强 |
| 色域映射 | 自动、感知、相对色度、饱和度 4 种 | 有色彩管理，但可调项少于 Lumen | Lumen 更强 |
| 外部着色器 | 支持外部 mpv GLSL／hook 着色器，可加载、更换、移除 | 内置视频滤镜与部分 GPU 滤镜，无同等的 mpv hook 生态 | Lumen 更适合深度调校 |
| 360°／3D／Ambisonics | 未提供 | 支持 360° 视频、3D 音频、最高三阶 Ambisonics | VLC 明显更强 |
| 视频滤镜种类 | 以 mpv 核心、HDR／GLSL 调校为主，界面未单列去隔行／降噪等滤镜 | 内置去隔行、调整、锐化、色彩、帧率转换等较多滤镜 | VLC 种类更多 |
| 变速／逐帧／A–B 循环 | 0.1–12×，默认保持音高；逐帧；A–B 循环 | 支持变速与逐帧，具体范围和入口随界面／版本 | Lumen 参数范围更宽 |

### 3.2 字幕与语言

| 维度 | Lumen 2.2.4 | VLC 3.0.24 | 判定 |
| --- | --- | --- | --- |
| ASS／SSA | 直接使用 libass，保留定位、颜色、动画、卡拉 OK 与容器内嵌字体 | 支持 ASS／SSA 与字幕大小调整 | Lumen 更贴近 libass 原始排版 |
| 常见字幕格式 | SRT、VTT、SMI／SAMI、部分图形字幕 | SRT、VTT、TTML／EBU-TT、CEA-708、蓝光文本字幕、图形字幕等 | VLC 格式更广 |
| 轨道语言偏好 | 中文／英文／日文及自定义代码，按容器默认／强制标记重新选择 | 支持轨道与字幕语言选择 | 相当 |
| 离线实时字幕 | 中文 Zipformer CTC；日语、英语、德语 Moonshine；CPU 单线程；从选中音轨取 PCM，同连接不另开识别连接 | 无本地语音识别 | Lumen 独有 |
| 本地翻译 | NanoSakura 日→中；Hy-MT2、Index-Translate 支持中／英／日／德／法目标 | 无本地翻译 | Lumen 独有 |
| 双语字幕导出 | 可保存识别原文或完整双语 SRT，未翻译句保留原文 | 无对应 ASR／翻译导出链路 | Lumen 独有 |
| 字幕延迟／同步微调 | 界面与 README 未提供字幕延迟调节 | 提供音画／字幕同步微调 | VLC 更强 |
| 字幕大小／位置 | 支持字幕缩放与显示控制 | 支持字幕大小调整 | 相当 |

### 3.3 音频与功放

| 维度 | Lumen 2.2.4 | VLC 3.0.24 | 判定 |
| --- | --- | --- | --- |
| 输出设备／共享／独占 | 可选设备、系统共享 PCM、独占 PCM | WASAPI 等平台输出，可选设备 | 相当 |
| IEC 61937 直通 | AC3、EAC3、DTS、DTS-HD、TrueHD；失败回退共享 PCM，并显示实际输出与原因 | AC3、DTS、DTS-HD、EAC3、TrueHD 的 HDMI／S／PDIF 直通 | 相当，Lumen 状态更透明 |
| 均衡器／音效 | 未提供 | 内置均衡器、音频滤镜、重采样、音高变换等 | VLC 明显更强 |
| 音画延迟调节 | 界面未提供 | 提供音频／字幕延迟微调 | VLC 更强 |
| 多声道／3D 音频 | 直通与 PCM 输出；不本地重建 Atmos／DTS:X 对象 | 支持 Ambisonics、3D 音频与多声道 | VLC 更强 |
| Atmos／DTS:X | 通过兼容压缩载体交给 HDMI 功放解码；不宣称本地对象重建 | 通过 HDMI 直通交给功放 | 相当，均依赖功放 |

### 3.4 网络与媒体来源

| 维度 | Lumen 2.2.4 | VLC 3.0.24 | 判定 |
| --- | --- | --- | --- |
| HTTP(S)、RTSP(S)、RTMP(S)、RTP、UDP、SRT | 支持 | 支持，另有 SRT listener 等模式 | 相当 |
| HLS、DASH | 支持；HLS 清单保持单条目，MPD 作为单个媒体源 | 支持 HLS、DASH，覆盖范围更广 | 相当，VLC 更成熟 |
| M3U／M3U8 频道清单 | 作为独立列表文件夹，保留节目名、相对地址、签名参数，重复导入合并 | 播放列表／网络列表支持 | 相当 |
| SMB／FTP／SFTP／NFS／HTTP2／SAT>IP／RIST | 未提供这些入口 | 支持局域网文件系统浏览、HTTP/2、SAT>IP、RIST 等 | VLC 明显更强 |
| 投屏／远程渲染器 | 未提供 | Chromecast 投屏 | VLC 明显更强 |

### 3.5 光盘、采集、录制与转换

| 维度 | Lumen 2.2.4 | VLC 3.0.24 | 判定 |
| --- | --- | --- | --- |
| DVD／蓝光菜单 | 不支持；定制 libmpv 构建关闭 DVDNAV 与 libbluray | 支持 DVD 菜单、蓝光 BD-J 菜单与蓝光 ISO | VLC 明显更强 |
| 摄像头／采集设备 | 未提供 | 支持 DirectShow 采集，3.0.24 增加 NV12 支持 | VLC 明显更强 |
| 录制／快照 | 底层有截图命令，界面未提供入口；无录制 | 支持录制与快照 | VLC 明显更强 |
| 转码／推流／流媒体服务 | 未提供 | 支持转码、流输出与推流 | VLC 明显更强 |
| 章节导航 | README／菜单未提供 | 支持章节导航 | VLC 更强 |
| 媒体库 | 播放列表为主，无媒体库／元数据管理 | 3.0.24 以播放列表为主；VLC 4 预览引入媒体库 | 两者均非完整媒体库 |

### 3.6 平台、生态与维护

| 维度 | Lumen 2.2.4 | VLC 3.0.24 | 判定 |
| --- | --- | --- | --- |
| 跨平台 | 仅 Windows x64 | Windows、macOS、Linux、BSD、Android、iOS／tvOS 等 | VLC 明显更强 |
| 界面语言 | 简体中文 | 多语言 | VLC 明显更强 |
| 插件／SDK | 无插件体系，无公开播放 SDK | 模块／扩展体系，libVLC 可供第三方嵌入 | VLC 明显更强 |
| 在线更新／安全响应 | README 未描述自动更新；以本地发布检查器与 SHA-256 为主 | RSA-4096 更新校验，公开安全公告 | VLC 明显更强 |
| 体积与 UI | EXE 约 1.44 MiB，原生 Win32，常驻控件开销低 | 功能面大，Qt 界面与依赖更重 | Lumen 更轻 |
| 构建可审计性 | 固定 libmpv 源码、依赖锁、文件哈希、编译器身份可复核 | 官方构建与发行版打包；公开源码 | Lumen 在自有发布链上更细 |
| 验证规模 | 2.2.4 专项 408／51／628 项、完整回归 2,018 项；2.2.0 另有 92／74／106／1,563／1,840／41／22 项 | OSS-Fuzz、公开安全公告与全球长期使用 | 量级不同，VLC 更广 |

### 3.7 VLC 的相对短板

1. **HDR／画质可调深度不如 Lumen。** VLC 3.0.24 支持 HDR 与色调映射，但不提供 Lumen 这种逐项参数和外部 mpv GLSL／hook 生态；VLC 4 桌面版尚未发布。
2. **没有本地实时字幕／翻译。** VLC 不提供离线 ASR、双语字幕生成或本地翻译模型。
3. **界面与包体更重。** Qt 界面、模块体系和跨平台能力带来更大的功能面，也带来更高的体积与复杂度；这不是缺陷，但和 Lumen 的轻量定位不同。
4. **不是专业画质调校工具。** VLC 的视频滤镜更偏通用，Lumen 的 libplacebo 参数和 shader hook 更适合逐项调校。

## 四、Lumen 与 PotPlayer 对比

### 4.1 定位与路线

PotPlayer 是 Windows 消费级全能播放器：内置解码器、硬件加速、3D、DVD／蓝光／TV、截图／录制／采集、书签、皮肤、多语言，以及 Whisper 实时字幕和翻译。Lumen 是 Windows 专用的高画质播放与字幕工具，强调 libplacebo 画质参数、libass、离线固定模型、双语 SRT 和可审计发布。

两者的 AI 字幕路线不同：

- **Lumen**：固定 ONNX 模型（Zipformer／Moonshine／NanoSakura／Hy-MT2／Index），内置模型管理与 SHA-256 校验，离线 CPU 推理，4 种识别语言、5 种翻译目标。
- **PotPlayer**：Whisper 引擎（含 Faster-Whisper-XXL），引擎与模型需另行配置；语言覆盖取决于所选 Whisper 模型；翻译可接在线服务或本地 LLM。Whisper 大模型可能更准、语言更多，但依赖外部组件或网络。

### 4.2 功能矩阵

| 维度 | Lumen 2.2.4 | PotPlayer 261021 | 判定 |
| --- | --- | --- | --- |
| 平台与界面 | Windows x64，原生 Win32，简体中文，轻量 | Windows 7–11，x86／x64／ARM64，原生 Win32，皮肤，多语言 | PotPlayer 平台与本地化更广；Lumen 更轻、中文更集中 |
| 解码与格式 | 定制 libmpv + FFmpeg n9.0；软件／硬件解码 | 闭源内置解码器 + OpenCodec；DXVA／CUDA／QuickSync；4K／8K、3D、DVD／蓝光／TV／HDTV | PotPlayer 消费格式与设备面更广；Lumen 解码库版本更新 |
| HDR | libplacebo：6 种色调映射、4 种色域映射、动态峰值、响应速度、显示峰值、参考白、外部 GLSL／hook | HDR 色调映射类型选择；Dolby Vision HDR；NVIDIA RTX Video HDR／VSR；D3D11 默认 | 各有所长：Lumen 参数更细；PotPlayer 格式与 GPU 增强功能更广 |
| 3D／光盘／电视 | 未提供 | 3D 眼镜与输出格式；DVD／蓝光／TV／HDTV 设备入口 | PotPlayer 明显更强 |
| 字幕渲染 | libass，完整 ASS／SSA 排版、动画、卡拉 OK、内嵌字体；SRT／VTT／SAMI／图形字幕 | libass 输出；SMI／SRT／ASS／SSA、Ruby、DVD VobSub、蓝光字幕 | PotPlayer 传统字幕格式更广；Lumen 双语 ASR 导出更完整 |
| 离线实时字幕 | 中文、日语、英语、德语；固定小模型；集成模型管理；离线 CPU | Whisper 实时字幕；引擎／模型需另行配置；语言取决于 Whisper 模型 | Lumen 开箱即用、模型可管理；PotPlayer 大模型语言与准确率上限更高 |
| 翻译 | NanoSakura 日→中；Hy-MT2／Index 中英日德法；固定 SHA-256；离线 | 在线翻译服务或本地 LLM；语言与服务取决于配置 | Lumen 离线可控；PotPlayer 选择更多但依赖网络／外部模型 |
| 双语 SRT | 支持原文／完整双语导出 | 有字幕保存能力，但没有 Lumen 这种 ASR＋翻译＋双语合并的固定流程 | Lumen 更完整 |
| 音频 | 设备选择、共享／独占 PCM、IEC 61937 直通、失败原因显示 | 均衡器、ReplayGain、音频滤镜、音画延迟、多声卡选择 | Lumen 直通透明；PotPlayer 音效与调校更全 |
| 网络／直播 | HTTP(S)、RTSP(S)、RTMP(S)、RTP、UDP、SRT、HLS、DASH | HTTP 流、HLS／DASH 内置模块、直播列表；其他协议随版本／模块 | Lumen 协议清单更明确；PotPlayer 消费级直播入口更成熟 |
| 截图／录制／采集 | 底层截图命令，界面无入口；无录制、无采集 | 截图、视频录制、摄像头／采集设备 | PotPlayer 明显更强 |
| 书签／章节／播放列表 | 播放列表；无书签／章节界面 | 播放列表、书签／章节、快照预览 | PotPlayer 更强 |
| 皮肤／多语言 | 固定中文原生界面 | 皮肤／主题、多语言 | PotPlayer 更强 |
| 更新／可审计性 | 自有源码与固定依赖、SHA-256、发布检查器可复核 | 闭源厂商更新，内部实现不可审计 | Lumen 更可审计；PotPlayer 闭源但产品成熟 |
| 体积 | EXE 约 1.44 MiB，基础运行文件约 204 MiB（不含模型） | 商业播放器体量，随安装组件变化 | Lumen 更轻 |

### 4.3 判定

**PotPlayer 作为 Windows 全能消费播放器，整体产品面强于 Lumen；Lumen 在“离线字幕／翻译的集成度 + 画质参数 + 轻量原生 UI”上更强。**

PotPlayer 的相对优势：

- 格式与设备面：4K／8K、3D、DVD／蓝光／TV／HDTV、摄像头／采集、录制。
- HDR 功能面：Dolby Vision、NVIDIA RTX Video HDR／VSR，以及 HDR 色调映射类型选择。
- 消费级功能：EQ、ReplayGain、音频滤镜、音画延迟、书签／章节、皮肤、多语言。
- ASR 上限：Whisper 大模型语言覆盖更广，配合本地 LLM 可做更灵活的翻译。

Lumen 的相对优势：

- 集成式离线字幕：模型管理、SHA-256、固定版本、离线 CPU、识别与翻译队列、双语 SRT 导出，不需要用户自己拼 Whisper 引擎。
- libplacebo 画质参数更细，外部 GLSL／hook 更适合逐项调校。
- libass 排版链路更直接，字幕还原更贴近原始 ASS 行为。
- 原生轻量 UI、透明 IEC 61937 直通状态、可复现构建与发布证据。

PotPlayer 的相对短板：

- 闭源，内部实现与依赖不可审计；Windows 专用，无跨平台。
- Whisper 引擎／模型需额外下载配置；翻译可能依赖在线服务或本地 LLM，离线开箱体验不如 Lumen。
- 功能面复杂，HDR 参数粒度不如 Lumen 的 libplacebo 菜单；没有 Lumen 这种固定模型哈希／发布检查器。

Lumen 的相对短板：

- 没有 3D、DVD／蓝光／TV／HDTV、Dolby Vision、RTX HDR／VSR、EQ／ReplayGain、皮肤／多语言 UI。
- ASR 仅 4 种语言，模型小于 Whisper large；翻译目标仅 5 种，语言覆盖不如 PotPlayer 灵活。
- 没有摄像头／采集、录制、书签／章节等消费级入口。

## 五、Lumen 与 mpv 对比

### 5.1 关系

Lumen 的播放核心就是定制 libmpv。两者的解码、渲染、字幕基本盘高度重合；Lumen 不是 mpv 的竞品，而是 **mpv 的 Windows 原生前端 + 字幕／翻译扩展 + 定制 LGPL 构建**。因此谈“Lumen 的画质比 mpv 强”并不成立；Lumen 的优势主要在界面、集成和 AI 字幕，而不是播放内核本身。

### 5.2 功能矩阵

| 维度 | Lumen 2.2.4 | mpv 0.41.0 | 判定 |
| --- | --- | --- | --- |
| 核心与版本 | 定制 libmpv `6a262d11f6`（2026-10-19），LGPL 构建；gpu-next／D3D11 | 0.41.0 稳定版（2025-12-21）；gpu-next 默认；2026 master 构建活跃 | 核心同源；Lumen 是较新的 master 快照，但构建能力被裁剪 |
| 平台 | Windows x64 | Windows、macOS、Linux、BSD | mpv 明显更强 |
| 界面 | 原生 Win32，中文，播放列表、设置、模型管理 | 无完整 GUI，只有 OSC；配置／CLI；可选第三方前端 | Lumen 开箱即用；mpv 更自由 |
| 渲染／GPU API | gpu-next／D3D11；libplacebo、libass | gpu-next 默认；OpenGL、Vulkan、D3D11；硬件解码 API 更广 | mpv 更广；Lumen 更聚焦 Windows |
| HDR／画质 | UI 暴露 6 种色调映射、4 种色域、动态峰值、响应、显示峰值、参考白、外部 GLSL／hook | 同一套 mpv/libplacebo 能力，可经配置／CLI 暴露更多选项 | 核心同级；mpv 更可配置，Lumen 更易用 |
| 脚本／扩展 | 无 Lua／JS 脚本、无 IPC、无公开 SDK | Lua／JS 脚本、JSON IPC、libmpv C API、用户脚本生态 | mpv 明显更强 |
| 光盘菜单 | 定制构建关闭 DVDNAV 与 libbluray | 常见构建可通过 discnav 支持 DVD／蓝光菜单 | mpv 更强 |
| 截图／流录制 | 底层截图命令，界面无入口；无录制 | 核心提供截图、stream-record 等能力 | mpv 更强 |
| 章节导航 | 界面未提供 | 核心支持章节 | mpv 更强 |
| 网络／协议 | README 明确列出 HTTP(S)、RTSP(S)、RTMP(S)、RTP、UDP、SRT、HLS、DASH | 通过 FFmpeg／配置支持多种协议，取决于构建 | Lumen 清单更明确；mpv 更灵活 |
| 音频直通 | 设备选择、共享／独占、IEC 61937、失败原因显示 | 可通过配置实现直通，状态需自行观察／脚本 | Lumen 更透明；mpv 更可配置 |
| 离线 ASR／翻译 | 4 种识别语言、5 种翻译目标、模型管理、双语 SRT | 无内置 ASR／翻译 | Lumen 独有 |
| 中文 UX／模型管理 | 原生中文界面、模型下载／切换／删除、发布检查器 | 无 | Lumen 独有 |
| 体积 | EXE 约 1.44 MiB；完整运行文件约 204 MiB（不含模型） | 小型 CLI／库；官方构建较小 | 两者都轻；Lumen 多了原生 UI 与 AI 运行库 |
| 许可／嵌入 | 自有 Windows 应用；无公开播放 SDK | 默认 GPL；libmpv 可 LGPL 构建；公开 C API | mpv 更适合嵌入与二次开发 |

### 5.3 判定

**mpv 是更强大、更自由的上游核心；Lumen 是更易用、更中文化的 Windows 封装。**

mpv 的相对优势：

- 可配置性：配置文件、命令行、属性／命令、Lua／JS 脚本、JSON IPC。
- 跨平台与 GPU API：Windows、macOS、Linux、BSD；OpenGL、Vulkan、D3D11。
- 上游能力：DVD／蓝光 discnav、截图、stream-record、章节、更完整的协议／滤镜组合。
- 可嵌入：libmpv C API，适合第三方应用、自动化和研究。

Lumen 的相对优势：

- 开箱即用的 Windows 原生 UI、播放列表、设置、模型管理。
- 中文离线实时字幕、翻译、双语 SRT。
- 透明的 IEC 61937 直通状态、设备／格式回退提示。
- 固定依赖、SHA-256、发布检查器，版本证据更集中。

mpv 的相对短板：

- 没有完整 GUI；OSC 只提供基础控制，播放列表、设置、模型管理需要配置或第三方前端。
- 没有内置 ASR／翻译／模型下载；要实现 Lumen 的字幕链路需要脚本或外部工具。
- 对普通用户门槛更高；默认 GPL 构建与 Lumen 的 LGPL 定制构建取向不同。

Lumen 的相对短板：

- 定制构建关闭了 Lua、OpenGL、Vulkan、DVDNAV、libbluray、libcurl 等能力；不是完整 mpv。
- 没有脚本／IPC／公开 C API，不能像 mpv 一样嵌入或自动化。
- 没有截图／流录制／章节界面；这些能力在 mpv 核心中存在，但 Lumen 未暴露。
- 仅 Windows、仅中文界面，平台与本地化远窄于 mpv。

## 六、Lumen 的明确优势

1. **HDR／画质调校参数最细。** libplacebo 的 6 种色调映射、4 种色域映射、动态峰值分析、响应速度、显示峰值与参考白均可调，并支持外部 GLSL／hook。相对 VLC、mpv 的 UI 可调性更强；相对 PotPlayer，Lumen 参数更细，但 PotPlayer 的 Dolby Vision／RTX HDR 功能面更广。
2. **字幕排版更接近 libass 原生行为。** ASS／SSA 的定位、颜色、动画、卡拉 OK 和容器内嵌字体保留完整。
3. **离线实时字幕与本地翻译。** 相对 VLC、mpv 独有；相对 PotPlayer，Lumen 的差异是集成式固定模型、内置模型管理、SHA-256 校验与双语 SRT 导出，PotPlayer 则依赖外部 Whisper 引擎／模型或在线翻译服务。
4. **双语字幕可交付。** 识别原文与完整双语 SRT 可保存，未翻译句保留原文。
5. **音频直通状态透明。** IEC 61937 失败时回退共享 PCM，并显示实际输出与原因；设备菜单是偏好，运行状态才是协商结果。
6. **轻量原生 UI 与可复现发布。** EXE 约 1.44 MiB，固定 libmpv／FFmpeg／libplacebo／libass 版本与哈希，发布检查器绑定源码、运行文件和证据。

## 七、Lumen 的明确短板

1. **相对 VLC／PotPlayer，内容获取与外设面窄。** 没有 DVD／蓝光菜单、3D、TV／HDTV、摄像头／采集、录制、转码推流、投屏、SMB／FTP／SFTP／NFS 浏览。
2. **相对 PotPlayer，消费级功能不全。** 没有 Dolby Vision、RTX HDR／VSR、EQ／ReplayGain、音画／字幕延迟、书签／章节、皮肤／多语言 UI；ASR 语言覆盖不如 Whisper 大模型，翻译目标仅 5 种。
3. **相对 mpv，核心能力被裁剪。** 定制构建关闭 Lua、OpenGL、Vulkan、DVDNAV、libbluray、libcurl；没有脚本／IPC／公开 C API、截图／流录制／章节界面。
4. **格式与异常文件鲁棒性未大规模验证。** 解码库版本新，但验证集中在本机样本与有限协议矩阵；VLC、PotPlayer 有长期、全球范围的损坏文件与冷门容器积累。
5. **生态位窄。** 仅 Windows、仅中文界面、无插件体系、无公开 SDK，不能嵌入第三方应用。
6. **更新与安全流程弱。** README 未描述在线自动更新或公开安全公告；VLC 3.0.24 一次发布即含 130+ 安全修复。
7. **AI 功能有真实成本。** 模型不随安装包分发；中文模型约 154.8 MiB，日语／德语 Tiny 约 30.8 MiB，英语 Small 约 135.7 MiB，NanoSakura 约 282.5 MiB，Hy-MT2 约 1,080.6 MiB，Index 约 1,251.4 MiB。2.2.0 实测连续日中识别＋翻译峰值工作集约 721.7 MiB，120.38 秒片段累计约 97.17 CPU 秒；NanoSakura 译文相对句末中位延迟约 1.19 秒、P95 约 2.23 秒、最大约 2.87 秒。Hy-MT2／Index 在测试机上通常每句还需数秒。
8. **验证边界清楚但窄。** 现有证据主要来自 i5-7200U／Windows 10 19045 单机；实体 HDR 显示、Atmos／DTS:X 功放对象输出、跨显示器 DPI、长时间压力测试未完成。
9. **内核仍是 mpv。** 2.2.4 仍依赖定制 libmpv；独立内核只是 2026-10-30 的可行性提案，尚未实施。

## 八、场景建议

| 需求 | 更合适的选择 |
| --- | --- |
| Windows 全能本地播放、3D、DVD／蓝光／TV、Dolby Vision／RTX HDR、EQ／ReplayGain、皮肤／多语言 | PotPlayer |
| Windows 高画质 HDR 调校、ASS 字幕、中文离线字幕／翻译、双语 SRT、轻量原生 UI | Lumen |
| 跨平台、脚本、配置、嵌入、上游 mpv 能力 | mpv |
| 网络、光盘、采集、录制、转码、推流、投屏、长期安全更新、跨平台 | VLC |
| 通用 Windows 消费播放器 | PotPlayer／VLC |
| 开箱即用中文离线字幕 | Lumen |
| 多者并存 | Lumen 做高画质与离线字幕，PotPlayer 做全能本地播放，VLC 做网络／转码，mpv 做折腾与嵌入 |

## 九、证据与边界

- 本文是数据汇总与能力对比，没有重新运行 Lumen、VLC、PotPlayer 或 mpv 的同机同片性能对比测试。
- Lumen 的体积、内存、CPU、延迟与断言数量来自其版本发布记录和本机测量，主要环境为 Intel i5-7200U、8 GiB、Windows 10 19045；这些数字不能外推为所有机器或所有片源。
- VLC 的功能与版本信息来自 VLC 3.0.24 官方发布说明和 VideoLAN 公开页面；VLC 4.0 桌面版尚未发布，Vega OS beta 不作为桌面稳定版能力。
- PotPlayer 的功能与版本信息来自官方版本历史、potplayer.tv、功能页和 VideoHelp；PotPlayer 闭源，内部实现与依赖不可独立审计。Whisper 引擎／模型、在线翻译服务和本地 LLM 的实际效果取决于所选组件，本文没有做同片对比。
- mpv 的功能与版本信息来自 mpv.io、mpv manual 和 0.41.0 发布说明；Lumen 的定制构建开关来自 `third_party/mpv/build-provenance.json`。
- Lumen 的实体 HDR 输出、Atmos／DTS:X 功放对象输出、跨显示器 DPI、真实桌面鼠标流程和长时间压力测试仍有未覆盖边界；本文不把历史断言或文件哈希当作这些场景的实际验收。
- 2.2.4 是交互修复版本，未重新跑完整的旧硬件、协议和长片矩阵；历史数字归其原始版本。

## 十、来源

Lumen：

- [README.md](../README.md)
- [当前发布验证索引](core-validation.md)
- [2.2.4 长按倍率修复](hold-fix-2.2.4-2026-10-31.md)
- [2.2.0 日中翻译与模型下载源](models-2.2.0-2026-10-29.md)
- [2.0.0 磁盘与字幕内存优化验证](footprint-validation-2026-10-24.md)
- [独立播放内核可行性与架构提案](native-playback-core-feasibility-2026-10-30.md)
- [mpv 构建来源与能力开关](../third_party/mpv/build-provenance.json)
- [模型目录与文件哈希](../resources/caption-models.json)

VLC：

- [VLC 3.0.24 发布说明](https://www.videolan.org/vlc/releases/3.0.24.html)
- [VLC 官方网站](https://www.videolan.org/)
- [VLC 4.0 预览状态](https://vlc.cc/news/vlc-4-0-preview/)
- [VLC for Vega OS：首个 VLC 4.0 beta](https://jbkempf.com/blog/2026/vlc-vegaos/)

PotPlayer：

- [PotPlayer 官方网站](https://potplayer.tv/)
- [PotPlayer 官方版本历史](https://t1.daumcdn.net/potplayer/PotPlayer/v4/Update2/UpdateEng.html)
- [PotPlayer 下载与版本记录](https://potplayer.multimediaplayer.net/download/)
- [PotPlayer 功能页](https://potplayer.info/features/)
- [PotPlayer NVIDIA RTX Video HDR](https://potplayer.multimediaplayer.net/tutorials/potplayer-nvidia-rtx-video-hdr/)
- [PotPlayer Dolby Vision HDR](https://potplayer.multimediaplayer.net/tutorials/potplayer240509-dolbyvision/)
- [PotPlayer 字幕翻译](https://potplayer.multimediaplayer.net/tutorials/subtitle-translation/)
- [VideoHelp：PotPlayer 261021](https://www.videohelp.com/software/PotPlayer)

mpv：

- [mpv 官方网站](https://mpv.io/)
- [mpv manual](https://mpv.io/manual/master/)
- [mpv 0.41.0 发布说明](https://github.com/mpv-player/mpv/releases/tag/v0.41.0)
- [MPV 0.41 切换 gpu-next 报道](https://linuxiac.com/mpv-0-41-media-player-switches-to-gpu-next-renderer/)
- [mpv 源码仓库](https://github.com/mpv-player/mpv)
