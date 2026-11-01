# LumenPlayer 空间与内存优化方案 — 2026-10-29

状态：方案文档，未修改源码、发行目录、安装包或用户数据。本文中的“预计收益”均为工程估算，落地时必须按项目现有证据流程重新测量；只有实测通过的差值才能写入发布结论。

当前基线：Lumen Player 2.2.0。已有透明文件压缩、字幕 CPU arena 关闭、模型完整性校验和发布清单校验继续保留，本文不重复计算这些既有收益。

## 1. 结论与目标

| 方向 | 当前基线 | 目标区间 | 主要手段 |
|---|---:|---:|---|
| 工作区总占用 | 约 36.5 GB | 8–10 GB | 证据去重、冷数据归档、移出无关工具链、清理临时下载 |
| 发行目录实际占用 | 约 225 MB（2.0 实测；2.2 需重测） | 170–190 MB | FFmpeg/libmpv 精简、压缩策略、打包卫生 |
| 安装包 | 65.1 MB（2.1.2，model-free） | 60–62 MB | LZMA2 最大压缩、精简运行库 |
| 普通播放内存 | 176.25 MB 工作集 / 165.29 MB 私有提交（2.0 优化后历史中位数） | 145–155 / 135–145 MB | 自适应缓存、解码队列上限、UI 缓冲按需释放 |
| 实时字幕内存 | 399.99 / 379.59 MB（同上） | 330–350 / 310–330 MB | ORT 会话选项、缓冲区复用、模型生命周期 |
| 翻译场景 | 子进程 Job 上限 3 GB；Hy-MT2 模型约 1.08 GB | 硬上限 2 GB 级，释放可验证 | 进程隔离、上下文/批量收紧、空闲卸载 |
| 关闭媒体后 | 142.53 / 123.51 MB（同上） | 先定位残留，不预设回收量 | 独立探针已显示销毁内核后仍有 74–113 MiB 残留 |

内存数字的统一口径：工作集为 `WorkingSetSize`，私有提交为 `PROCESS_MEMORY_COUNTERS_EX.PrivateUsage`；两者都不等于物理驻留或 GPU 显存。历史基线来自 2.0.0 的 1280×720 本地短片、1120×700 隐藏窗口，不能直接外推到 4K、网络直播、功放或所有兼容后端。

## 2. 当前基线

### 2.1 工作区

| 目录 | 逻辑大小 | 说明 |
|---|---:|---|
| `build/` | 32.3 GB | 测试证据、运行副本、模型、安装中间产物 |
| `.rustup/` | 2.14 GB | Rust 工具链；未发现本项目构建/测试脚本引用 |
| `third_party/` | 1.21 GB | mpv 315 MB、models 310 MB、ffmpeg 238 MB、libplacebo 128 MB、旧 Vosk 132 MB 等 |
| `dist/` | 363.8 MB | 正式运行目录，含用户设置、列表、日志和 cache |
| `.cargo/` | 0.36 GB | Cargo 缓存；同上未见引用 |
| `test/` | 0.10 GB | 测试脚本与结果 |

`build/` 中 ≥50 MB 的文件合计 26.4 GB。按“同名同大小”估算的重复冗余已达 10.1 GB，包括 4 份 1.08 GB GGUF、27 份 `avcodec-63.dll`、12 份中文 `model.int8.ort`、9 份 `model.int8.onnx`、6 份 NanoSakura encoder/decoder。该数字未逐文件哈希，实际可去重量只会更高。

### 2.2 发行目录

`dist/` 当前 363.8 MB 逻辑大小，最大文件：

| 文件 | 大小 | 备注 |
|---|---:|---|
| `sherpa-onnx-streaming-zipformer-ctc-zh-int8-2025-06-30/model.int8.ort` | 155.6 MB | 中文识别模型 |
| `avcodec-63.dll` | 86.9 MB | 解码器、编码器、工具代码 |
| `avfilter-12.dll` | 30.1 MB | 被 `libmpv-2.dll` 导入 |
| `avformat-63.dll` | 21.7 MB | 封装/解封装 |
| `onnxruntime.dll` | 17.0 MB | ASR 与 NanoSakura 共用 |
| `libshaderc_shared.dll` + `libspirv-cross-c-shared.dll` | 13.4 MB | libplacebo 运行时着色器编译 |
| `moonshine.dll` | 9.2 MB | Moonshine 识别运行时 |
| 其余 DLL、EXE、licenses、shaders | 约 30 MB | |

`dist/cache/` 中已有约 3 MB 的 mpv shader 缓存；`LumenPlayer.ini`、`LumenPlaylists.dat`、`LumenPlayer.log` 是用户数据，不应进入安装包。安装脚本已经从发布清单取文件，并拒绝 `.ort/.onnx/.gguf/.safetensors`，当前安装包本身不含模型。

### 2.3 内存

历史验证记录：

| 阶段 | 工作集 MiB（2.0 优化前 → 后） | 私有提交 MiB（2.0 优化前 → 后） |
|---|---:|---:|
| 正常播放，未开实时字幕 | 173.81 → 176.25 | 161.47 → 165.29 |
| 实时字幕工作中 | 419.03 → 399.99 | 402.16 → 379.59 |
| 字幕关闭并完成收尾 | 184.21 → 184.04 | 172.76 → 172.75 |
| 关闭媒体并丢弃本次字幕 | 143.88 → 142.53 | 123.31 → 123.51 |

补充结论：

- 关闭字幕后主要模型占用可释放，六轮探针均检测不到已加载的 `onnxruntime.dll`。
- 只创建/销毁播放内核的独立探针在销毁后仍保留约 74–113 MiB 私有提交；这支持“播放路径/图形驱动/分配器存在残留”，但不证明可以通过空闲卸载安全回收。
- 4K 软件 UI 覆盖层的 32 位整窗缓冲理论值约 31.64 MiB；这是尺寸计算，不是 4K 实测。
- 大列表导入和控件查找是 CPU 热点（1 万条约 3.75 秒导入、控件构造约 5.87 ms），不属于本文的空间/内存主目标，未列入收益表。

## 3. P0：工作区瘦身（零产品风险，优先执行）

目标：36.5 GB → 8–10 GB，且不丢失任何发布证据、恢复包或被引用的测试结果。

| 措施 | 做法 | 预计回收 | 风险/边界 |
|---|---|---:|---|
| 内容寻址去重 | 对 `build/` 证据按 SHA256 分组；相同内容只保留一份，其余用 NTFS 硬链接。只对只读证据使用硬链接，禁止对会被测试写入的模型副本使用 | 12–18 GB | 需校验哈希；测试目录若需可写副本，保留独立文件 |
| 冷证据归档 | 当前发布不引用的旧 `build/*-2026*` 用 7z/LZMA2 固实归档，生成文件清单和哈希后删除原目录 | 归档后总量降至 8–10 GB | 归档前核对 `finalize_*` 脚本引用的运行编号 |
| 移出无关工具链 | `.rustup` 2.14 GB、`.cargo` 0.36 GB；本项目构建入口未引用 | 2.5 GB | 先确认没有其他本机工作使用；确认后移到独立工具链目录或删除 |
| 清理临时下载/中间包 | `build/downloads` 154 MB、旧安装 payload、`compiler-temp`、`tmp` | 0.3–0.5 GB | 保留依赖哈希和来源记录，不保留可重新下载的压缩包 |
| 合并恢复包 | `build/backups` 三个 80.8 MB 恢复包高度重叠；保留最新完整包和必要的差异说明 | 0.15–0.25 GB | 恢复包是回滚证据，删除前核对内容 |
| 归档旧 Vosk 资产 | `third_party/v` 67 MB、`third_party/m` 65.1 MB；发行目录已不使用 | 约 132 MB | 先运行探针确认无引用；保留归档 |
| 文本证据压缩 | 对 JSON/日志/SRT 等只读证据用 XPRESS8K，不压缩模型和 DLL | 0.1–0.2 GB | 只改变物理占用，需记录实际字节 |

关键约束：

- `dist` 必须保持独立完整发布目录，不能为了省空间把 `dist` 硬链接到 `build`。
- 去重脚本应沿用 [build.ps1](../build.ps1) 的原子替换和硬链接保护思路；替换文件时先写临时文件，再替换目录项。
- 归档/删除前必须验证：当前 `release-manifest.json`、`finalize_*` 脚本、最近一次发布报告引用的文件和运行编号仍然可用。

## 4. P1：发行目录与安装包

### 4.1 FFmpeg / libmpv 精简重建

当前 FFmpeg 是宽配置的 LGPL shared 构建，见 [build-configuration.json](../third_party/ffmpeg/build-configuration.json)。播放器不需要编码、转码和大部分外部库，但需要保留截图、字幕、HDR、网络协议和兼容模式。分三档推进：

| 档位 | 内容 | 预计逻辑减少 | 风险与前置条件 |
|---|---|---:|---|
| 安全档 | 关闭 `programs`、`doc`、`avdevice`、未使用的外部库；关闭不用的 muxers/encoders，但保留截图需要的 PNG/MJPEG 编码和 image2 封装 | 20–35 MB | 中低；必须跑完整播放矩阵和截图验收 |
| 进阶档 | 只保留实际用到的滤镜；评估重建 libmpv 时关闭 `libavfilter`（`avfilter-12.dll` 当前 30.1 MB，且被 `libmpv-2.dll` 导入） | 25–30 MB | 中高；先确认固定 mpv 版本的构建选项和所有滤镜调用路径 |
| 激进档 | 只保留实际使用的 demuxer/decoder/protocol 白名单 | 再 10–20 MB | 高；直接触碰“全格式兼容”承诺，必须全矩阵通过 |

三档全部完成，发行目录逻辑大小预计减少 45–70 MB；按 XPRESS8K 估算，实际占用减少约 25–40 MB。必须保留完整 FFmpeg 产物作为回退，并在发布清单中记录构建配置。

### 4.2 压缩策略

现有 [compact_runtime.py](../test/compact_runtime.py) 对 EXE、DLL 和模型统一使用 XPRESS8K。可扩展为按文件选择算法：

- 大而冷读的 `avcodec`、`avfilter`、`avformat` 和模型试用 LZX，通常比 XPRESS8K 再省 8–15%；
- 热读的 EXE、小 DLL 保持 XPRESS8K；
- 记录冷启动、首帧、首次字幕加载时间，避免用启动延迟换 10–20 MB；
- 压缩后逐文件核对 SHA256，保留 `--restore` 路径和独立报告。

### 4.3 打包卫生

- 发布前清空 `dist/cache/`（当前约 3 MB shader 缓存），并从 manifest 排除；
- 安装包继续使用发布清单取文件，拒绝用户设置、播放列表、日志、cache 和模型权重；
- Inno Setup 可从 `lzma2/normal` 升到 `lzma2/max`，预计再省 3–5 MB；
- 安装后透明压缩继续执行，不支持的文件系统保留普通文件。

### 4.4 模型策略（产品决策）

- 安装包已经 model-free；便携 `dist` 仍含 155.6 MB 中文模型。可提供“在线安装 + 按需下载”和“离线完整包”两个产物，在线版直接少 155.6 MB。
- 模型安装/更新成功后清理 `.part`、`.decoded.tar`、`.extract`、`.previous-*`，避免同一模型长期占两份；下载前做峰值空间预检（下载 + 解包 + 旧版本保留）。
- 不建议运行时解压模型：解压峰值和启动延迟通常更差。

## 5. P2：播放与 UI 内存

| 措施 | 当前实现/位置 | 预计收益 | 风险与验证 |
|---|---|---:|---|
| 按源自适应 demuxer 缓存 | [mpv_backend.cpp](../src/mpv_backend.cpp) 固定 `demuxer-max-bytes=64MiB`、`demuxer-max-back-bytes=16MiB`；本地 16–24/8 MiB，网络保持 64/16，4K 本地 32/8 | 本地 20–40 MB | 弱网、拖动、直播可能受影响；用首帧、卡顿和拖动恢复验证 |
| 解码队列上限 | 评估 `vd-queue-max-bytes` / `vd-queue-max-secs` 等选项；先核对固定核心版本是否暴露 | 4K/软解 10–50 MB | 可能增加掉帧；需 1080p/4K、HEVC/AV1、软硬解矩阵 |
| 软件解码线程上限 | `vd-lavc-threads` 从自动改为 `min(4, 核数)`；确认 `vd-lavc-dr` | 5–20 MB | 低配机可能掉帧；逐分辨率测试 |
| 空闲卸载播放内核 | 关闭媒体后保留 mpv 会产生 142.53 MB 基线；可评估 N 分钟后 `terminate_destroy` | 未证明，先诊断 | 独立探针销毁后仍残留 74–113 MiB；不能预设可回收量，先定位残留归属 |
| 降低 libplacebo 缓存上限 | [video_bridge.cpp](../src/video_bridge.cpp) 当前 `max_total_size=16 MiB`；改为 4–8 MiB 并在关闭时清理 | 最多 8–12 MB | 自定义 GLSL 首次编译变慢；需 shader 回归 |
| PCM 队列收紧 | [mpv_backend.cpp](../src/mpv_backend.cpp) 当前 3 秒/16 MB/512 块；可降到 1.5 秒/8 MB | 字幕开启时 4–8 MB | 识别输入不能断；保持 PTS、跳转代际和上限语义 |
| 覆盖层按需释放 | [gfx.cpp](../src/gfx.cpp) 为整窗 DIB：1080p 8.3 MB、1440p 14.7 MB、4K 31.6 MiB；控制条隐藏且无字幕时销毁或缩小 surface，交互时重建 | 1440p/4K 10–30 MB | 重建有闪烁/延迟风险；需全屏、多屏、DPI、字幕同时验证 |
| 字幕文本布局缓存 | [main.cpp](../src/main.cpp) 每次重绘重建 `IDWriteTextLayout`；按文本/宽度/字号缓存 | CPU/临时分配，内存收益小 | 不能改变字形、换行和居中结果 |

普通播放目标 145–155 MB 工作集，仅适用于上面的 720p 本地基线；4K、软解、网络、字幕同时开启时必须单独列出，不能用单一数字概括。

## 6. P3：字幕与翻译内存

| 措施 | 当前实现/位置 | 预计收益 | 风险与验证 |
|---|---|---:|---|
| ORT 会话选项实验 | [caption-cpu.conf](../resources/caption-cpu.conf) 已关闭 CPU arena；再试 `EnableMemPattern=0`、顺序执行 | 字幕 5–20 MB | 先确认 sherpa 配置解析支持；失败回退默认；测 CPU 时间和识别延迟 |
| 复用 NanoSakura 零状态缓冲 | [nanosakura.cpp](../src/nanosakura.cpp) 每句临时分配 `zeroState` 约 9.4 MB、`zeroConv` 约 0.26 MB；改为成员缓冲复用 | 每句少一次约 9.7 MB 分配/释放 | 只降低分配抖动/碎片，不应改变输出；做同日中样本对照 |
| 停止时释放容器容量 | `LiveCaptions::Stop` 清空 `queue`/`partial`，但 `finals` 等容量可能保留；统一 `swap` 释放 | 数 MB，长片更明显 | 不能在暂停、拖动或仅关闭识别时丢弃仍可导出的字幕 |
| 翻译进程硬上限 | [caption_translation.cpp](../src/caption_translation.cpp) 当前 Job 上限 3 GB、`-c 1024`；Hy-MT2/Index 试 2 GB，上下文按质量测试降到 512–768 | 峰值可控 | 上限过低会被杀；需长句、批量、模型切换验证 |
| NanoSakura 子进程化 | 当前 282.5 MiB 模型在播放器进程内加载；改为独立子进程 + Job 上限 + 退出即杀 | 主进程峰值降 100 MB 级 | 需要 IPC、启动时序、取消和崩溃隔离测试 |
| 翻译空闲卸载 | 长时间无字幕/暂停时卸载翻译模型，下一句按需加载 | 空闲数百 MB | 首句延迟上升；需用户可见状态和超时策略 |
| 长会话滚动保留 | 对已识别/已翻译字幕按时间或条数滚动，导出走增量临时文件 | 单次几 MB，重在稳定性 | 不能丢导出内容；需长片和拖动验证 |

字幕开启目标 330–350 MB 工作集；开启翻译后总内存取决于模型，应承诺“有硬上限、可完全退出”，不承诺统一数值。

## 7. P4：结构性方案

- **统一模型代理进程**：ASR、NanoSakura、llama-server 都放入受 Job 限制的子进程，主进程只保留 UI 和播放。收益是内存可强制回收、崩溃隔离、上限明确；成本是 IPC、启动时序、取消和测试矩阵。
- **更小的中文 ASR 模型或可替换档位**：当前 155.6 MB INT8 是质量基线；换小模型必须做识别质量对照，不能只看体积。
- **UI 合成升级**：4K 整窗软件 DIB 约 31.6 MiB；DirectComposition、硬件合成或分区域 layered window 可进一步降内存，但跨屏、DPI、视觉和兼容性验证成本高。
- **增量构建**：减少 `build/` 反复生成完整副本和重复打包，属于开发空间/时间优化，不直接减小发行包。

## 8. 不建议做的

- **UPX/PE 压缩壳**：运行时解压到私有内存，反而增加内存，容易触发杀软，也会改变发布文件身份。
- **把所有 DLL 改成静态链接**：CRT 和运行时代码会在多个模块重复，磁盘和内存都可能变差。
- **没有完整矩阵就删解码器/滤镜**：直接破坏“全格式兼容”承诺。
- **没有质量评估就降模型精度**：省几十 MB 但可能毁掉识别/翻译质量。
- **用 `SetProcessWorkingSetSize` / `EmptyWorkingSet` 制造“内存下降”**：只是把页面换出，伤性能，不是真实释放。
- **llama.cpp 加 `--no-mmap`**：私有提交会明显上升，和优化目标相反。
- **运行时解压模型**：峰值空间和启动延迟通常更差。
- **把 `dist` 做成硬链接集合**：破坏独立完整发布目录和安全的逐文件替换。

## 9. 验收与执行顺序

### 9.1 测量口径

- 磁盘：逻辑字节、`GetCompressedFileSizeW` 实际字节、文件数、SHA256 清单、被引用证据是否仍可用。
- 内存：工作集、私有提交、峰值提交、GPU 专用/共享（可得时）、页错误；区分进程和子进程。
- 体验：首帧、掉帧、拖动恢复、ASR 首次部分结果/P95、翻译句末延迟、字幕停止耗时。
- 功能：完整编解码/协议/HDR/字幕/截图矩阵，安装升级卸载，用户设置和列表保持。

### 9.2 场景

1. 1080p 本地 H.264/AAC，未开字幕。
2. 1080p 本地 HEVC 10 bit，硬解/软解。
3. 4K 本地，硬解/软解，含 HDR 和非 HDR。
4. HLS/DASH/RTSP/RTMP 网络源，弱网与拖动。
5. 实时字幕开启/关闭/切换模型/取消启动。
6. 翻译开启，NanoSakura、Hy-MT2、Index 分别测。
7. 2 小时以上长片，含多次拖动、暂停、切轨。
8. 大播放列表（1 万条）打开、切换、导入。
9. 安装升级、卸载、重装，用户数据哨兵。

### 9.3 顺序

1. **基线补齐**：用同一 2.2 构建重测磁盘和内存，形成可对比的 before 记录。
2. **P0 工作区**：去重、归档、移工具链，先拿 25 GB 级回收，不动产品代码。
3. **P1 安全档**：清 cache、安装包最大压缩、LZX 小范围试验、FFmpeg 只关编码器/封装器/程序/doc；全矩阵通过后再考虑 avfilter 白名单。
4. **P2 播放/UI**：先做 demuxer 缓存、解码队列、线程上限；空闲卸载和覆盖层释放作为可选策略，先诊断再开关。
5. **P3 字幕/翻译**：先做缓冲区复用和容器释放，再做 ORT 选项实验，最后评估 NanoSakura 子进程化。
6. **P4 结构性改动**：只在 P1–P3 实测证明瓶颈仍在时投入。

### 9.4 回滚与证据

- 每个阶段保留旧产物和恢复包；`dist` 采用逐文件原子替换，不整目录覆盖用户设置/列表。
- FFmpeg 精简保留完整构建产物和配置记录；矩阵失败时回退完整版本。
- 压缩使用现有 `--restore` 路径并输出独立报告。
- 内存选项提供默认回退；模型策略保留已下载模型和旧目录。
- 只写实测差值，估算值单独标注，不能把“预计”写成“已实现”。

## 10. 需要确认的决策

1. 便携版是否接受“在线模型 + 离线完整包”双产物，以换取 155.6 MB 的发行目录缩减。
2. 是否允许关闭 lavfi/裁剪解码器；这会改变兼容边界，需要产品确认后再进激进档。
3. 是否接受 LZX 带来的首次读取延迟，换取额外 10–20 MB 实际占用。
4. 是否投入 mpv 空闲卸载；独立探针显示销毁后仍有残留，需先定位归属再决定。
5. 是否把 NanoSakura 改为子进程；收益明确但改动和测试成本较高。

## 附录：本次基线测量命令

以下命令只读，用于复现本文的目录大小和重复估算，不修改文件：

```powershell
# 目录逻辑大小
Get-ChildItem -Directory d:\LumenPlayer | ForEach-Object {
  $s = (Get-ChildItem -Recurse -File -Force $_.FullName -ErrorAction SilentlyContinue |
        Measure-Object -Sum Length).Sum
  [PSCustomObject]@{ Dir = $_.Name; SizeMB = [math]::Round($s / 1MB, 1) }
} | Sort-Object SizeMB -Descending

# build 中 ≥50 MB 文件的同名同大小冗余估算
$files = Get-ChildItem -Recurse -File -Force d:\LumenPlayer\build -ErrorAction SilentlyContinue |
         Where-Object { $_.Length -ge 50MB }
$files | Group-Object { $_.Name + '|' + $_.Length } | ForEach-Object {
  [PSCustomObject]@{
    Name = $_.Group[0].Name
    MB = [math]::Round($_.Group[0].Length / 1MB, 1)
    Copies = $_.Count
    ExcessMB = [math]::Round((($_.Count - 1) * $_.Group[0].Length) / 1MB, 1)
  }
} | Sort-Object ExcessMB -Descending | Select-Object -First 25

# 发行目录最大文件
Get-ChildItem -Recurse -File -Force d:\LumenPlayer\dist |
  Sort-Object Length -Descending | Select-Object -First 25 `
  @{n='MB';e={[math]::Round($_.Length/1MB,2)}}, FullName
```

参考记录：

- [footprint-validation-2026-10-24.md](footprint-validation-2026-10-24.md)：透明压缩、字幕 arena、实际播放内存。
- [optimization-audit-2026-10-27.md](optimization-audit-2026-10-27.md)：播放状态轮询、文本布局、配置写入、模型下载空间等候选。
- [performance-memory-review-2026-10-23.md](performance-memory-review-2026-10-23.md)：大列表、字幕停止耗时、独立内核残留内存。
- [optimization-results-2.1.2-2026-10-28.md](optimization-results-2.1.2-2026-10-28.md)：2.1.2 已实施项与未实施边界。
