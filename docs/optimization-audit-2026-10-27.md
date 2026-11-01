本次检查覆盖当前生产源码、开发中的模型管理与翻译模块、构建、测试、安装和工作区。使用 Ponytail audit 审查复杂度，另行审查正确性、性能和交付；涉及 JSON 与 libmpv API 时使用 Context7 并核对官方文档。只新增本报告与独立诊断文件，未修改生产源码、正式程序、用户配置或播放列表。

当前正式程序为 2.0.0，清单内 107 个分发文件的 SHA-256 全部吻合；源码资源和安装脚本已进入 2.1.0 开发。旧窗口发布校验本次运行返回 `Source set changed`，说明它不能验收当前开发树，并不表示正式目录的文件损坏。身份核验记录：[release-identity.json](D:/LumenPlayer/build/optimization-audit-20261027/release-identity.json)。

P1 表示下一次发布前优先处理，P2 表示值得安排，P3 表示低风险维护与整理。成本和改动风险均为相对估计；性能候选没有在本轮进行前后对照，因此不承诺 CPU、内存或延迟下降幅度。

| 优先级 | 层面 / 优化点 | 当前证据 | 建议 | 主要收益 | 成本 / 改动风险 |
|---|---|---|---|---|---|
| P1 | 模型目录解析生命周期 | [caption_models.cpp:100](D:/LumenPlayer/src/caption_models.cpp:100) 在 C++17 中遍历临时 JSON 的子对象引用。本轮同一目录的最小复现读到 0 个模型；保留 JSON 对象的对照读到 7 个。已有集成测试在前 6 项后报固定模型不存在。 | 让解析出的根 JSON 对象活到遍历结束；对正式目录中每个 ID 做可寻址检查，再继续整链路集成验收。 | 恢复新模型功能的基础可用性，避免未定义行为。 | 小 / 低 |
| P1 | 模型启动取消与退出等待 | [main.cpp:784](D:/LumenPlayer/src/main.cpp:784) 的完整哈希检查没有传入可选取消标记；[StopCaptions](D:/LumenPlayer/src/main.cpp:687) 会等待启动线程。 | 将取消标记传到文件校验和启动阶段，区分可取消校验与不可中断的第三方初始化，退出和切换使用明确的异步收尾状态。 | 减少大模型校验时关闭字幕、换模型和退出的长等待。 | 中 / 中；需覆盖启动中取消 |
| P1 | 译文过期及绘制状态 | [CollectTranslations](D:/LumenPlayer/src/main.cpp:658) 把译文显示时间设为返回时刻再加至少 5 秒；同一播放代际没有句龄上限。[Tick](D:/LumenPlayer/src/main.cpp:2251) 的字幕变化检测未包括译文及其到期状态。 | 按源句时间设置可显示窗口；过晚结果只用于导出。将译文出现、替换和到期统一加入重绘状态，并在跳转时取消过期推理。 | 避免旧句译文覆盖新句、静音段留下过期文字；这是源码风险，尚未做 GUI 复现。 | 中 / 中 |
| P1 | 卸载数据策略 | [lumenplayer.iss:88](D:/LumenPlayer/installer/lumenplayer.iss:88) 无条件删除设置和播放列表。 | 默认保留用户数据，提供明确的清除选项；用现有数据执行升级、卸载、重装验证。 | 降低用户设置和列表丢失的风险。 | 小至中 / 中 |
| P1 | 安装包输入范围 | [build-installer.ps1:52](D:/LumenPlayer/installer/build-installer.ps1:52) 从使用中的 dist 排除少数名称后复制其余内容。 | 改为从已验收的干净产物按发布清单取文件，核对哈希、EXE 版本和完整依赖闭包；未知文件使打包失败。 | 防止临时程序、用户导出文件和旧资源混入发行包。 | 中 / 低至中 |
| P1 | 测试与正式运行目录隔离 | [build.ps1:87](D:/LumenPlayer/build.ps1:87) 的 Test 默认输出到 dist，并向该目录同步 DLL；[test.ps1](D:/LumenPlayer/test.ps1:1) 使用此入口。 | 测试统一使用独立目录；正式安装只接受已验收产物，保留当前安全的逐文件替换方式。 | 回归测试不再改变用户正在使用的运行库组合。 | 中 / 中 |
| P1 | 新版本完整验收与证据绑定 | [最新集成结果](D:/LumenPlayer/build/caption-model-manager-20261027/integration-results/failure.json) 为失败；当前 107 个正式文件仍属于 2.0.0。旧发布脚本对当前源码集合校验失败。 | 修复目录解析后，用同一发布构建完成模型选择、下载/取消、实际识别和翻译、播放回归、无模型安装及升级卸载，生成该版本自己的清单和入口。 | 防止把 2.0 或独立模型探针的通过结果当成 2.1 发布验收。 | 中至大 / 低 |
| P2 | 识别与翻译准备解耦 | [main.cpp:786](D:/LumenPlayer/src/main.cpp:786) 启动识别后，还要串行校验翻译文件才发布识别就绪；两种翻译模型文件约 1080.59 / 1251.38 MiB。 | 识别就绪后立即启动取音并解除相应等待；翻译单独校验、加载、报告进度，继续保留完整哈希验证。 | 缩短已经有识别模型时的首播等待，让原文先可用。 | 中 / 中 |
| P2 | CC 状态反馈 | [main.cpp:1392](D:/LumenPlayer/src/main.cpp:1392) 仅统计本地和实时字幕条目，遗漏现代核心字幕轨。[10 月 26 日记录](D:/LumenPlayer/docs/verification-and-model-options-2026-10-26.md:11) 已在原生窗口复现该现象。 | 以实际选中的内嵌/外挂轨道、字幕开关和实时识别状态共同驱动指示灯。 | 指示状态与实际字幕输出一致。 | 小 / 低 |
| P2 | 播放状态轮询 | [mpv_backend.cpp:840](D:/LumenPlayer/src/mpv_backend.cpp:840) 每轮最多等待 35 ms，并每 250 ms 刷新轨道、设备及多项属性；已有唤醒回调，但没有使用属性观察。 | 用 mpv_observe_property 缓存变化，保留必要的播放进度采样和现有唤醒回调，空闲/暂停时降低周期刷新。 | 有机会减少空闲唤醒、同步属性查询和列表分配；需实测。 | 中 / 中；核对定位和恢复时序 |
| P2 | 界面重绘和文本布局 | [gfx.cpp:23](D:/LumenPlayer/src/gfx.cpp:23) 使用软件 D2D；[PaintOverlay](D:/LumenPlayer/src/main.cpp:1757) 在 dirty 时重绘整层并整窗提交，字幕每次重建文本布局。已有 dirty 跳帧，不能描述为始终全速重绘。 | 先缓存稳定布局/字幕文本，合并高频交互刷新；再评估缩小更新区域或 GPU 合成。 | 有机会降低大窗口、4K/DPI 放大下的 UI CPU 开销；GPU 合成需另做验证。 | 中至大 / 中 |
| P2 | 配置写入与报错 | [SaveConfig](D:/LumenPlayer/src/main.cpp:362) 每次逐项写全部配置，滚轮和键盘调音量也触发；写入返回值未检查。 | 记录变更字段，合并短时间连发写入，退出时收尾；检查写入失败并提供可见反馈，保留未知配置键。 | 减少 UI 线程重复 I/O，提高保存可靠性。 | 小至中 / 低至中 |
| P2 | PCM 队列与锁占用 | [captions.cpp:421](D:/LumenPlayer/src/captions.cpp:421) 在队列锁内逐采样混音、线性重采样、deque 入队；两个识别后端逐样本出队。 | 使用连续批量缓冲，缩短锁内计算；保持原有 2 秒上限、PTS 和跳转代际。采样算法变化单独做质量对照。 | 有机会减少分配与锁竞争；不能据静态代码承诺识别提速。 | 中 / 中至高 |
| P2 | 模型管理状态刷新 | [model_dialog.cpp:53](D:/LumenPlayer/src/model_dialog.cpp:53) 每 250 ms 检查所有模型文件并读取安装标记。 | 安装/删除/选择完成时使状态缓存失效；空闲时降低刷新频率，真正启用前仍完整校验。 | 减少打开管理窗口时的重复文件系统访问。 | 小至中 / 低 |
| P2 | 模型下载峰值空间 | [RunInstall](D:/LumenPlayer/src/caption_models.cpp:251) 有完整性与原子发布保护，但没有安装前可用空间检查；重装可能同时持有旧目录、新下载及解包内容。 | 计算下载、解包和保留旧版本所需峰值空间，提前提示；区分空间不足、网络中断和文件占用。 | 避免大文件下载接近完成才失败，改善错误恢复。 | 小至中 / 低 |
| P2 | 增量构建与重复打包工作 | [build.ps1:65](D:/LumenPlayer/build.ps1:65) 每次重编视频桥与全部源文件；FFmpeg DLL 又从专用目录和核心闭包进入 bundle。 | 按翻译单元和头文件依赖增量编译，固定依赖用已验证身份缓存；按目标文件名合并打包输入并验证冲突。 | 缩短开发构建及文件校验时间；不要直接增加并行度挤占本机内存。 | 中 / 中 |
| P2 | 工具链、版本和源码历史 | 当前没有 Git 元数据；[build.ps1:24](D:/LumenPlayer/build.ps1:24) 从 PATH 选择编译器；版本分散在 RC、安装脚本、README 和 manifest。 | 记录并校验编译器路径/版本，建立统一版本来源；为自有源码建立本地版本历史，大型模型、缓存和恢复资料独立保留。 | 改动、回归和发布身份更容易追踪复现。 | 中 / 低至中 |
| P2 | 多屏 DPI 与可访问性 | 主窗口有 WM_DPICHANGED；[model_dialog.cpp:99](D:/LumenPlayer/src/model_dialog.cpp:99) 仅初始化时读取 DPI，缺少对应消息处理。自绘主控件未发现 UI Automation 提供者。 | 让模型窗口动态更新字号/尺寸，验证高 DPI 小工作区和跨屏；给主要自绘控件提供名称、角色和状态。 | 改善跨屏显示与屏幕阅读器使用；键盘操作通过不等同屏幕阅读器验收。 | 中至大 / 中 |
| P2 | 联合负载性能基线 | [现有报告](D:/LumenPlayer/docs/verification-and-model-options-2026-10-26.md:33) 有 30 分钟 O0 功能循环；[新模型报告](D:/LumenPlayer/docs/priority-caption-results-2026-10-27.md:72) 多为独立探针。 | 对发布优化构建测试真实视频＋识别＋翻译，记录首帧、首字、句末译文、P95、丢帧、私有提交和取消时长，包含长片与弱网。 | 为线程数、模型档位和内存策略提供可信选择依据。 | 中至大 / 低 |
| P3 | 工作区空间分层整理 | 本次文件长度统计：build 约 11.57 GiB，.rustup 约 2.09 GiB，.cargo 约 0.35 GiB。当前产品构建入口没有 Rust/Cargo 引用。 | 核实工具链是否用于其他工作后再归档/清理；build 按当前实验、固定依赖、恢复包和可再生临时产物分类。对硬链接去重后计算实际回收量。 | 可能释放可观开发空间；不会自动减小安装包。 | 小至中 / 中；应保留恢复证据 |
| P3 | delete: 旧安装实现 | 仍有 setup.ps1、setup.cmd、sfx-config.txt、uninstall.cpp 共 167 行；当前发行入口使用 [Inno Setup 脚本](D:/LumenPlayer/installer/build-installer.ps1:16)，未发现其调用旧入口。 | 先将旧安装方式移入历史归档，确认无外部依赖后删除。 | 减少维护入口与误用旧安装器的可能。 | 小 / 低 |
| P3 | delete / shrink: 无用字段及转码别名 | [FmtTimeShort](D:/LumenPlayer/src/common.cpp:100) 没有调用；[ffIndex](D:/LumenPlayer/src/playback_types.h:38) 只赋值不读取；[Widen/Narrow](D:/LumenPlayer/src/common.cpp:22) 仅转调现有 UTF-8 函数。 | 删除未使用函数/字段，调用处统一为 Utf8ToW/WToUtf8。 | 约再减 13 行与重复命名；不声称有明显性能收益。 | 小 / 低 |

Ponytail 复杂度审查估算：旧安装器 167 行，加无用函数/字段及别名约 13 行；不减少运行依赖。`net: -180 lines, -0 deps possible.` 这只是候选删除量，本次没有实施删除。

模型目录诊断结果：[catalog-lifetime-result.json](D:/LumenPlayer/build/optimization-audit-20261027/catalog-lifetime-result.json)，复现程序：[catalog_lifetime_probe.cpp](D:/LumenPlayer/build/optimization-audit-20261027/catalog_lifetime_probe.cpp)。使用本机工具链、C++17、O2、项目当前 JSON 头文件和目录；当前表达式退出码 1、数量 0，对照表达式退出码 0、数量 7。此对照证明目录遍历问题，不替代修改后的完整播放器回归。JSON 的 at 返回元素引用，依据为 [nlohmann 官方 API](https://json.nlohmann.me/api/basic_json/at/)；属性变化事件依据为 [libmpv 官方 client.h](https://raw.githubusercontent.com/mpv-player/mpv/master/include/mpv/client.h)，本地固定版本头文件也有对应声明。

本轮没有执行新的完整播放性能矩阵，也没有安装、卸载或替换发布目录。保留现有固定 UCRT64 依赖组合、兼容播放路径、同连接 PCM 采集、模型完整性检查和用户数据保护；它们已经解决的能力与边界不能仅因简化代码而丢失。已有透明压缩和字幕 arena 优化见 [历史测量报告](D:/LumenPlayer/docs/footprint-validation-2026-10-24.md)，本次未重复计算其收益。
