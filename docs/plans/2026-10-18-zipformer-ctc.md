# Zipformer CTC 实时字幕替换实施计划

**Goal:** 将默认本地 Vosk 替换成已经过本机门槛测试的 Zipformer CTC INT8，并交付可直接运行的播放器。

**Architecture:** 保留现有后台加载、音频采集和字幕展示接口，用匹配版本的 sherpa-onnx C API 动态加载 CPU 单线程模型。识别线程负责所有流操作，通过代次使跳转前的结果失效；结束输入和停止时排空短队列并提交末句。配置 engine=0 保持本地引擎含义，浏览器 SODA=1 保留。

**Tech Stack:** C++17 / Win32 / Media Foundation / sherpa-onnx 1.13.8 / Zipformer CTC Chinese INT8 2025-06-30。

## 1. 依赖与备份

- 备份当前源码、构建脚本、EXE 与用户 INI；旧 Vosk 原始运行库/模型继续保存在 third_party。
- 下载官方匹配版本 Windows x64 shared 原生运行库和指定模型；核验模型 SHA-256 与前轮 manifest 一致。
- 保留上游头文件与许可/来源说明；不引入 Python 运行依赖。

## 2. 原生识别器替换

- 修改 src/soda.h、src/soda.cpp：本地枚举/默认值、依赖检测、DLL 导出、创建配置、float 音频队列、分块解码、partial/final 字幕与时间戳。
- 使用固定 CPU 单线程和 greedy；安全加载随应用的 DLL，配对销毁流/结果/识别器，停止后卸载运行库。
- 跳转重建流并丢弃旧代次，保持已生成字幕；片尾/停止补齐末尾，队列设置实时积压上限。

## 3. 音频与用户交互

- 修改 src/player.h、src/player.cpp：采集结束和定位通知同一个字幕消费者，防止定位后提交旧采集样本。
- 修改 src/main.cpp：CTC 引擎文案、默认映射与结束/跳转回调；沿用后台模型加载。
- 修改 build.ps1 和资源版本：同版本原生库、模型及说明随发布包安装；安装时保留用户 INI。

## 4. 验证和交付

- 扩展 test/regression.cpp：真实语音的 partial、完整末句、时间范围、暂停/跳转/重播、反复开关、停止释放与导出回读。
- 运行 .\test.ps1，再构建/安装 Release，对正式 EXE 做启动和真实识别烟测。
- 检查设置页渲染、版本/依赖/模型散列，确认发布目录不再依赖 Vosk；旧运行资源只从 dist 删除，原件保留。
- 更新 README、验收记录与 release-manifest.json，记录播放器内延迟、内存及实际测试边界。

## 完成记录（2026-10-19）

- 已完成本地 CTC 引擎、音频结束/定位通知、配置映射、UI 标签及 1.2.0 正式包安装。
- CTC 专项 43 项通过；完整回归连续两轮各 139 项通过，其中最终一轮在 dist 不含 Vosk 资源的情况下执行。
- 已核验非零起点与连续分段时间戳、无静音尾部片段、48k 双声道输入、重复启停与 DLL 卸载；正式 EXE 烟测加载新引擎并正常退出。
- 原旧资源仅移除 dist 副本，third_party 原件和替换前 ZIP 保留。用户运行期间的最新 INI 以安装前快照为准保留。
- 测量与完整证据由 test/finalize_ctc_report.py 汇总至 docs/zipformer-validation.md 和 release-manifest.json。
