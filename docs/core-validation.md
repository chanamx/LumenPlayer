# LumenPlayer 当前发布验证索引

当前发布版本为 **2.2.5**，本轮缩短画面单击判定并处理晚到的双击消息，见 [本轮调整与验证](click-response-2.2.5-2026-11-01.md)。2.2.4 的固定长按倍率及模式切换清理继续保留。

正式发布状态及文件身份以 `build/click-response-20261101/publication.json`、`validation.json` 和根目录 `release-manifest.json` 为准。2.2.2 的实施、同版本基线、实测收益与验收范围继续保留在 [2.2.2 优化记录](tuning-results-2.2.2-2026-10-29.md)。

复核当前发布：

```powershell
python -B test/finalize_click_response_release.py --check-only
```

本次检查器绑定当前源码、正式文件和本次验收证据。历史版本的检查器和性能数字只对应其原始构建，不作为新 EXE 的实测。

保留的历史入口：

- [2.2.1 模型界面更新与验证](model-ui-2.2.1-2026-10-29.md)。
- [2.2.1 优化文档核验与候选范围](optimization-plan-verification-2026-10-29.md)。
- [2.0.0 窗口白边修复的原始验收](window-frame-validation-2026-10-24.md)，包含像素采样、非客户区重绘和文件选择框验收边界。
- [早期完整媒体与协议验证](../build/window-border-20261024/core-validation-before.md)。

实体 HDR 显示、Atmos/DTS:X 功放对象输出、真实跨显示器 DPI 切换和人工鼠标操作系统文件选择框，均需对应设备或桌面实测。本轮没有把历史断言或文件哈希核对称作这些场景的实际验收。
