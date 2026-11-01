# Lumen Player 1.8.0 画质、GLSL 与格式验收

2026-10-20。正式 EXE `db5204c5bc8c681b942a2af828b8be5b0f321f5c16ea29137c4c9963c504e26c`；原生主回归 1560 项，HLS 专项 60 项，渲染/音频专项 1840 项；10 份正式程序烟测使用同一个 EXE 和渲染桥接。安装并清理后，又直接从 dist 启动 AV1/HDR 视频、实时字幕并正常退出，确认未保存字幕清理和原始用户数据恢复。此报告由证据文件生成。

## 已交付

- FFmpeg 软件 / D3D11VA 硬件解码共用原始位深的 libplacebo D3D11 渲染链路。状态来自实际接收并成功传回的硬件帧；AV1 在本机自动使用软件解码。
- BT.601/709/2020、范围与色度重建、PQ/HLG、HDR10 元数据、SDR 色调映射及当前显示输出的 HDR 协商。保留用户的 Windows HDR 设置。
- mpv 风格 GLSL hook 的读取、移除、错误回退、Unicode/emoji 路径及重启保存。默认不加载外部着色器，附带轻度锐化示例。
- Lanczos 放大/色度重建、Hermite 缩小、sigmoid、峰值检测、自动色调/色域映射及蓝噪声抖动。默认关闭额外去色带。
- DXGI 呈现位于窗口线程，解码线程只上传帧并发出重绘请求；最多两次渲染器重建，第二次 WARP，保持时间、暂停和字幕采集。缓存限 16 MiB，关闭媒体后释放 GPU 帧并卸载 libavcodec/libavformat。
- 根据端点声道掩码保留多声道 PCM；立体声混音检查包含人声、前置、侧环绕和后环绕频率。

## 画质对照

独立 mpv Windows 构建来自 [shinchiro 20261019](https://github.com/shinchiro/mpv-winbuild-cmake/releases/tag/20261019)，版本及库版本在 `test/results/render/mpv-version.txt`，包 SHA256 在 `test/results/render-research/mpv-build.json`。使用 `--no-config --vo=gpu-next --gpu-api=d3d11 --hwdec=no`，除截图、窗口尺寸、音频空输出与测试脚本外不覆盖画质配置。对照工具及缓存仅用于开发测试，未进入 dist。

9 类源包括 SDR BT.601/709、完整/有限范围、10 位 SDR、PQ、HLG、10 位 AV1 和 BT.709/BT.2020 彩色色块；分别原尺寸、2 倍放大和 0.75 倍缩小，共 27 组。处理后的 8 位图像最大单组平均绝对差：SDR 0.0111/255、HDR 0.5159/255。逐像素结果在 `test/results/mpv-comparison.json`，并排图在 `test/results/renderer-comparison.png`。

这是这些片源与当前输出上的对照，不能扩展为所有显示器/片源逐像素等同。渲染库固定在 mpv 参考构建对应的上游提交 `ccb74c6c13b1676e3af368cbe9281bfe649c17c6`（7.377.0），包括新版 HDR 场景亮度自适应；不同编译参数和输出路径仍可能产生差异。当前实现采用 mpv 默认渲染算法基线；没有把相同底层库当作自动证明画质相同，也没有声称完成 MPC-HC/MPC-BE 各种默认渲染器的逐像素实测。MPC Video Renderer 的 HDR 转换、10 位处理与抖动用于功能核对。

先期稳定版 libplacebo 的中间纹理格式回退在本机曾关闭高级缩放；最终使用的上游提交已包含修复，无额外源码补丁。完整源码、提交、DLL 摘要与构建脚本在 `third_party/libplacebo/build-provenance.json`、`test/build_libplacebo.py`。渲染检查要求内部 FBO/缩放错误为零。软硬解切换复用同一个 GPU 渲染器，避免第二个交换链触发较慢的 bitblt 呈现，并实际比对切换前后的 10 位 HDR 图像。旋转夹具改用真正写入显示矩阵的参数，校验 90/180/270 度后的尺寸与色块。

## 格式与本机边界

| 项目 | 验证结果 | 边界 |
|---|---|---|
| AV1 8/10 位 | 实际解码、GPU 图像读回、完整播放及正式 EXE 验收 | 本机 D3D11 配置未提供 AV1 Profile 0，使用 CPU；不保证弱 CPU 的任意 4K/8K帧率 |
| HDR10 PQ / HLG | 10 位输入、亮度层次、BT.2020 彩色块、SDR 映射、真实 DXGI 呈现 | 本机显示器当前 SDR，物理 HDR 输出未验收 |
| AC-3 / E-AC-3 / TrueHD / DTS | 5.1 PCM 的每个声道与立体声混音均验证 | TrueHD 合成编码器实际输出 5.1，未把它误记为 7.1 |
| FLAC 7.1 | 8 个声道均有信号，全部主声道进入立体声混音 | 本机端点双声道，真实 7.1 扬声器未验收 |
| Dolby Atmos (E-AC-3 JOC) | Apple 官方 16/JOC 片段识别为 Atmos，实际产生 96768 帧非静音立体声 PCM；正式 EXE 日志识别 `profile=30` | 当前没有 Atmos 对象/高度渲染或 IEC 61937 直通；TrueHD Atmos、DTS-HD/DTS:X 扩展未实测 |
| HLS/直播与字幕 | 原有本地/HTTP TS、fMP4、AES-128、清单文件夹、取消、实时字幕和关闭清理通过 | 网络/HLS 保留原有播放中解码切换限制；GPU 重建可保留同一媒体连接 |

主格式矩阵仍为 68 个组合：66 个有效输入解码，2 个损坏输入安全拒绝。`media-capabilities.json` 记录了 D3D11 配置、显示输出和音频端点只读探测；当前默认音频端点为 48000 Hz、2 声道，查询的 IEC 61937 格式未获支持。未更改系统 HDR、空间音效、默认设备或全局路径。

真实 JOC 片段来自 [Apple 官方流媒体示例](https://developer.apple.com/streaming/examples/advanced-stream-dv-atmos.html)，原清单、分片 URL、摘要及独立探测保存在 `atmos-source.json`。只在开发结果目录保留短片段，不包含在运行包中。

## 正式包、恢复与复验

正式程序 EXE 798,208 字节；渲染桥接及 6 个依赖合计 18.03 MiB。FFmpeg、ASR 运行库、字幕模型和词表摘要与 1.7.0 相同。未宣称新版比旧版启动更快；各次输入空闲、字幕准备与内存测量保存在 `renderer-release-*.json`，不能从原始帧预览时间推断可见首帧速度。

原 1.7.0 备份 `build/backups/pre-renderer-20261020.zip`（`8888a89d54d3c6ba1677f34e8c73d901a7428f9798482ef82208246bbad482cf`）。安装时的最新 INI / 播放列表快照为 `build/backups/pre-renderer-install-20261020.*`，安装记录证明原数据保持。清理移除 792.78 MiB 的重复发布副本、构建工具、对象和临时对照程序；固定依赖、源码、已核验归档、备份与验证证据保留。

完整回归：`./test.ps1`。渲染/环绕声专项：先 `./build.ps1 -Target Test`、`python test/make_render_fixtures.py`，确保 `python test/prepare_atmos_sample.py` 的官方短样本可用，再 `dist/LumenRegression.exe --render`。独立 mpv 对照：`python test/prepare_renderer_runtime.py reference`、`python test/compare_mpv_render.py`。正式包验收：`./test/accept_renderer_release.ps1`。

参考：[mpv 手册](https://mpv.io/manual/stable/)、[libplacebo](https://libplacebo.org/)、[MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer)、[Windows Spatial Sound](https://learn.microsoft.com/en-us/windows/win32/coreaudio/spatial-sound)、[IEC 61937 格式](https://learn.microsoft.com/en-us/windows/win32/coreaudio/representing-formats-for-iec-61937-transmissions)。
