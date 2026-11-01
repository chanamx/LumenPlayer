# Lumen Player 2.2.1 模型界面微调与验证

日期：2026-10-29。以工作区实际发布的 2.2.0 为基线。主要修改模型与语言窗口，同时缩短设置页入口文字并修正版本显示。

## 界面变化

| 位置 | 调整 | 实现与性能边界 |
| --- | --- | --- |
| 模型列表 | 深灰底色、蓝色选中条、较舒适的行距、安装状态配色 | 保留 Win32 ListView 的选择、滚动和键盘行为，使用原生 custom draw |
| 信息层级 | 标题、当前识别/翻译、选中模型说明分区 | 系统字体；字体与画刷仅在打开窗口或 DPI 变化时创建 |
| 下载和使用操作 | 主要操作强调色，删除操作使用独立颜色，禁用/悬停/焦点状态统一 | 原生按钮 owner draw，无动画、图片素材或后台渲染线程 |
| 下载源、目标语言和进度 | 暗色下拉框、统一尺寸、细进度条 | 保留原生下拉选择与键盘交互 |
| 缩放和小窗口 | 按 DPI 更新字体与行高；紧凑布局；调整尺寸后确保选中行可见 | 测试覆盖 100% / 150% / 200% 的 DPI 消息和两种窗口尺寸 |
| 静止状态刷新 | 只更新发生变化的文字、进度和控件状态；空闲轮询降至每秒一次 | 下载或删除期间仍每 250 ms 检查状态，操作后立即刷新 |

模型目录、八个模型的身份和下载校验、识别/翻译实现、播放解码、音频及字幕生命周期均未修改。

下图来自最终生产窗口代码的原生离屏绘制。安装状态使用隔离测试模型，不代表用户当前配置。

![模型窗口](../build/model-ui-20261029/performance-updated-3/models-default.png)

[最小窗口](../build/model-ui-20261029/performance-updated-3/models-96-580.png) · [下拉选项](../build/model-ui-20261029/performance-updated-3/source-options.png) · [错误提示](../build/model-ui-20261029/performance-updated-3/error-state.png) · [2.2.0 对照](../build/model-ui-20261029/performance-baseline-3/models-default.png)

## 本机性能对照

两版交替启动各三次独立进程，取中位数。模型组件使用相同 862 × 623 客户区，预热后强制绘制 160 次，再采样 6 秒空闲状态。基线从更新前源码快照构建，两个探针使用相同工具链与优化级别。

| 模型窗口组件指标 | 2.2.0 | 2.2.1 |
| --- | ---: | ---: |
| 每次强制绘制 CPU 时间 | 35.55 ms | 12.50 ms |
| 每次强制绘制墙钟时间 | 44.45 ms | 17.04 ms |
| 6 秒空闲累计 CPU 时间 | 156.25 ms | 15.625 ms |
| 6 秒空闲控件文字写入 | 72 次 | 0 次 |
| 探针私有提交内存 | 2,818,048 B | 2,916,352 B |
| 连续绘制与重复 DPI 切换的 GDI 资源增长 | 无增长 | 无增长 |

本组样本的强制绘制 CPU 时间下降约 64.8%，探针私有提交内存增加 96 KiB。`PrintWindow` 强制绘制是组件比较，不等同于真实窗口刷新率或视频帧率。离屏窗口的空闲 `WM_PAINT` 次数两版均为零，不能用它证明屏幕上的实际重绘次数。

另以两版正式 EXE 各三次交替启动，在模型窗口关闭、无媒体、字幕关闭时采样 6 秒：两版累计 CPU 时间均未达到本机计时器可计出的增量；私有提交内存中位数为 19,202,048 B / 17,580,032 B，工作集为 35,639,296 B / 35,569,664 B。该短时样本未显示常驻资源回退，不据此宣称整体内存或启动性能普遍提升。

原始记录：[组件 A/B](../build/model-ui-20261029/performance.json)、[正式播放器空闲 A/B](../build/model-ui-20261029/player-idle.json)。

## 功能与发布验证

| 验证 | 最终结果 | 覆盖范围 |
| --- | --- | --- |
| 原生模型界面 | 100 项检查通过，三次最终运行均通过 | 八个模型、布局可达、Tab、下拉方向键、Space、DPI/字体、选中行可见、GDI 资源 |
| 模型管理回归 | 93 项检查通过 | 实际本地模型安装、选择、语言方向、不支持方向、删除释放、Esc 关闭 |
| 完整播放器回归 | 1,563 项断言通过 | 现有媒体格式、播放/暂停、跳转、字幕恢复等回归集合 |
| 最终正式 EXE 媒体检查 | 通过，正常退出 | 实际 `gpu-next`、WASAPI、48 kHz 双声道 PCM、硬件视频帧和 Zipformer；生成 2 条识别字幕 |
| 数据保留 | 41 个已有用户/非发行文件逐项校验 | 设置、播放列表、已下载模型及其他受保护文件保持原有字节 |
| 安装包 | 2.2.1 x64 构建成功，111 个发行白名单文件 | 不包含用户配置、播放列表、缓存或模型权重 |

媒体检查使用隔离目录 `build/model-ui-20261029/smoke-runtime`。未保存字幕只保留在内存中，退出后清理；该测试目录的配置与播放列表已还原。测试使用既有本地模型，没有新增网络下载。

正式回归和媒体检查保留默认 2 GiB 启动提交内存余量门槛与 512 MiB 运行中止底线。小型 UI 探针不载入推理引擎，启动门槛单独设为 768 MiB。首次媒体脚本因旧版 PowerShell 缺少自动载入的 `Get-FileHash` 在启动播放器前失败；改用当前 `pwsh` 后同一检查通过，失败记录保留在 `attempt-smoke-powershell5`。

界面验证通过进程内原生控件消息与离屏绘制完成，未向用户桌面发送鼠标或键盘输入。本轮没有完成物理跨显示器拖动、人工鼠标逐项验收、读屏软件验收或长时间联合负载性能测试。安装逻辑仅更新版本，本轮进行了打包及载荷核对，没有重新执行完整安装→升级→卸载矩阵。

## 交付与恢复

- 正式程序：`dist/LumenPlayer.exe`，版本 2.2.1，1,499,648 字节，比 2.2.0 增加 11,264 字节。
- 正式运行目录只有 EXE 需要替换，其余 110 个发行文件保持原哈希；继续使用原有 UCRT 依赖。
- 安装包：[LumenPlayer-2.2.1-x64-setup.exe](../build/model-ui-20261029/installer/LumenPlayer-2.2.1-x64-setup.exe)，约 65.15 MiB。
- EXE SHA-256：`fad0e2d47a4c62f9e93d9890a039276d329dd414fa326dc16eb09a9197744120`。
- 安装包 SHA-256：`3d16f21ce776e8b655dd8bf33578e06ee9575e61e7ddeac1df8b12f440d0cdde`。
- 恢复快照：[2.2.0-recovery.zip](../build/model-ui-20261029/before/2.2.0-recovery.zip)，保存更新前 166 个文件，包含项目自有源码、脚本和原 EXE；模型和未修改的第三方依赖保留在原处。
- 发布记录：[publication.json](../build/model-ui-20261029/publication.json)。最终源码、二进制、安装包及验证证据的文件绑定记录：[validation.json](../build/model-ui-20261029/validation.json)。

在项目根目录核对本版已冻结证据：

```powershell
python test/finalize_model_ui_release.py --check-only
```

该命令核对文件身份与既有证据，不重新运行媒体或界面测试，也不要求用户之后使用产生的配置和播放列表保持发行时内容。

需要重跑时，构建目标为 `ModelUiTest`、`DialogTest`、`Test` 和 `Release`，均指定本次 `build/model-ui-20261029` 下的独立输出目录。界面和模型功能入口为 `python test/run_model_ui_checks.py performance` / `dialog`，完整回归为 `regression`。重跑会更新证据，需重新生成最终绑定。

实现依据：[Microsoft ListView custom draw](https://learn.microsoft.com/en-us/windows/win32/controls/nm-customdraw-list-view)、[WM_DRAWITEM](https://learn.microsoft.com/en-us/windows/win32/controls/wm-drawitem)、[WM_DPICHANGED](https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged)。绘制结束恢复 DC 状态，DPI 改变时重建字体并采用建议窗口矩形。
