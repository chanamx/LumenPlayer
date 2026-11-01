# 问题报告：网络协议验收中 RTMP 项间歇失败

日期：2026-10-22
产品版本：Lumen Player 2.0.0
问题类型：验收脚本时序缺陷、报告与证据不一致
严重级别：中（验收可信度），非已确认的运行时播放故障

修复状态：**2026-10-22 已完成修复并复核**。以下第 1–9 节保留发现时的现场与分析；新增验收证据见第 10 节，原失败 JSON 继续保留，不被成功记录覆盖。

## 1. 摘要

保留的 `test/results/core/protocol-acceptance.json` 中，RTMP 项为 `exitCode: 1`，其余 HTTP、DASH、RTSP 为 0。失败进程的实际日志显示：RTMP 已完成连接、媒体加载和播放时间推进，失败发生在随后的一次性“音视频就绪”断言上。

根因是验收脚本在 `position > 0.2` 通过后立即检查 `HasAudio() && HasVideo()`，没有等待；而播放核心的视频尺寸 `dwidth/dheight` 通过 250 ms 周期的慢刷新发布。RTMP 作为直播式输入，音频轨通常先可用，视频要等关键帧解码和输出初始化，检查恰好落在尺寸尚未进入缓存的窗口内。

同一份 EXE 与核心 DLL 在隔离回环环境复跑时，四项协议全部通过，RTMP 用时 3.13 秒。因此本问题不构成“RTMP 协议不支持”或“RTMP 无法播放”的结论；需要修正的是验收断言与证据保留方式。

## 2. 基本信息

| 项目 | 值 |
|---|---|
| 产品版本 | 2.0.0 |
| 正式程序 SHA256 | `ee8ee900607bc08a6fb0a0f517f03639b7b82b801831a440b48612d81afe25e2` |
| 核心 DLL SHA256 | `f49b928b7e7411c714729ac082517f7e4c408f2d66c56789f8802804143951fa` |
| 固定 mpv 提交 | `6a262d11f63733d37e3ee6d5fafcb1be1ef83952` |
| 验收程序 | `build/staging/LumenCoreRegression.exe` |
| 验收程序 SHA256 | `65adb4db703d1a6218260cb1a024e339817c9b752f1d37d47edb12b4770bab00` |
| 平台 | Windows 10 19045；Intel Core i5-7200U，2 核 / 4 线程 |
| 复现脚本 | `test/run_core_protocol_checks.py` |
| 回环服务器 | MediaMTX 1.21.2，仅本机 |
| 媒体样例 | `test/results/core/network-source.mp4`，H.264 640×360 + AAC 48 kHz 单声道 |

## 3. 现象与证据

### 3.1 保留的失败结果

[protocol-acceptance.json](../test/results/core/protocol-acceptance.json)：

```json
{
  "protocol": "rtmp",
  "exitCode": 1,
  "wallSeconds": 2.7345736999996006
}
```

同一文件中 HTTP、DASH、RTSP 的 `exitCode` 均为 0，`sourceGetRequests` 为 1。

### 3.2 历史同签名失败

2026-10-21 的并发验收失败日志保存了完整的播放器输出
（[protocol-rtmp-concurrent-failure.log](../test/results/core/protocol-rtmp-concurrent-failure.log)）：

```
PASS asynchronous source reaches a terminal load state
PASS real playback core loads the media
OUTPUT mpv / software / gpu-next
PASS network playback advances on real input
FAIL after 9 modern checks: network source provides real audio and video
```

可以看到，连接、加载和播放时间推进都已通过；失败只落在 `network source provides real audio and video` 这一条。

### 3.3 失败进程时间线

保留的 `test/results/core/backend.log` 中，失败进程的最后记录为：

| 时刻 | 日志 | 说明 |
|---|---|---|
| 15:52:56.154 | `software UI renderer ready` | 进程启动 |
| 15:52:58.395 | `mpv audio recovery 1: default shared PCM` | 音频输出超过 1.2 秒未就绪，触发回退 |
| 15:52:58.568 | `audio tap ready mpv selected PCM; same playback connection` | `StartTap` 成功 |
| 约 15:52:58.7 | 进程退出，`exitCode 1` | 距 tap 建立约 0.13 秒 |

该进程没有后续的 `mpv ready` 行，说明退出时音频输出仍未完成；但失败发生得比 `mpv ready` 的判定更早。

### 3.4 通过进程的对照

隔离复跑中，通过的 RTMP / RTSP 进程耗时分别为 3.13、3.2、4.04、4.06、4.12 秒；失败的保留记录仅为 2.73 秒。失败进程提前退出，符合“断言没有等待、在视频就绪前就结束”的特征。

## 4. 根因分析

### 4.1 失败断言

[core_regression.cpp](../test/core_regression.cpp#L177-L178)：

```cpp
Check(Until([]{return g_player.Position()>0.2;},7),"network playback advances on real input");
Check(g_player.HasAudio()&&g_player.HasVideo(),"network source provides real audio and video");
```

第一条允许最多 7 秒；第二条只检查一次，没有 `Until` 重试。

`HasAudio()` 来自 mpv 的 `aid`；`HasVideo()` 要求 `Width() > 0 && Height() > 0`
（[player.h](../src/player.h#L320-L321)）。宽度和高度取自核心慢刷新中的 `dwidth` / `dheight`
（[mpv_backend.cpp](../src/mpv_backend.cpp#L620-L633)），而慢刷新周期为 250 ms
（[mpv_backend.cpp](../src/mpv_backend.cpp#L849-L851)）。

### 4.2 失败落在视频侧

[player.cpp](../src/player.cpp#L1717-L1734) 的 `StartTap` 在建立 mpv PCM tap 前先要求 `core->HasAudio()` 为真，并随后写入：

```
audio tap ready mpv selected PCM; same playback connection
```

失败进程在退出前 0.13 秒刚产生这条日志，说明当时音频轨已识别。断言在 tap 之后执行，因此音频侧不满足的可能性较低；失败的一侧应是 `HasVideo()`，即视频尺寸尚未进入 250 ms 的状态缓存。

该判断基于代码路径与日志顺序，属于高置信推断；本轮未在测试程序中直接打印 `HasAudio` 与 `HasVideo` 的数值。若要完全确证，可在断言前输出这两个布尔值与 `Width()/Height()`。

### 4.3 RTMP 的时序特征

- RTMP 是直播式输入，没有 MP4/DASH 的索引和预读；客户端从流中间接入。
- 音频轨通常先可用，视频需要等待关键帧、解码和 `gpu-next` 输出初始化。
- 验收顺序为 HTTP、DASH、RTSP、RTMP，四个独立进程连续启动；RTMP 是最后一项，音频端点重建或视频预滚稍慢就会落入“音频已就绪、视频尺寸未缓存”的窗口。
- `position > 0.2` 可以由音频时钟或直播流的非零时间戳快速满足，因此不能代表视频已经出帧。

### 4.4 音频回退不是直接原因

`mpv audio recovery` 由“有音轨、文件已加载、但 `current-ao` 持续为空超过 1.2 秒”触发
（[mpv_backend.cpp](../src/mpv_backend.cpp#L658-L661)）。它说明音频输出初始化偏慢，会增加整体就绪时间，但不是断言的失败条件。

复跑中，至少一次 RTMP 进程同样出现 `mpv audio recovery`，随后仍通过全部断言；因此不能把该日志单独当作失败根因。

### 4.5 根因结论

失败由验收断言的时序竞争触发，不是 RTMP 传输失败，也不是 H.264/AAC 解码失败。播放器侧确实存在“视频尺寸状态最多滞后约 250 ms”的窗口，但正常播放会自行越过该窗口；测试没有给它时间。

## 5. 复现与验证

1. 使用与失败记录相同的 `LumenCoreRegression.exe` 和 `libmpv-2.dll`，在隔离回环环境重跑原始四项脚本：HTTP、DASH、RTSP、RTMP 全部 `exitCode: 0`；RTMP 用时 3.13 秒，播放器日志为 12 项断言全部通过。
2. 另外独立运行 3 次 RTMP：全部通过；其中一次出现 `mpv audio recovery`，仍通过。
3. 同一份二进制既产生过失败记录，也产生过通过记录，符合间歇性时序问题的特征。

复跑仅用于诊断，未替换项目保留的失败 JSON 与原始日志；当前 `protocol-acceptance.json` 仍保留 15:52:59 的失败版本。

## 6. 关联问题

### 6.1 报告与证据未绑定同一次运行

[core-validation.md](core-validation.md) 生成于 2026-10-22 15:22，将四项串行验收列为通过；当前落盘的 `protocol-acceptance.json` 写于 15:52:59，是失败版本。报告与最终证据来自不同运行，且没有运行编号或时间戳把两者绑定。

### 6.2 失败日志被后续探针覆盖

`test/run_core_protocol_checks.py` 每次运行都会写 `protocol-<name>.log`。失败发生后执行的 `--probe-only` 运行把 `protocol-rtmp.log` 覆盖为 ffprobe 的流信息，导致失败时播放器自己打印的断言信息只剩历史并发日志可参考。

### 6.3 “仅并发失败”的判断不成立

文档把 2026-10-21 的 RTMP 失败归为并发场景问题，并称其不作为通过依据；但 2026-10-22 的串行运行出现了完全相同的失败签名。该问题并非并发专属。

### 6.4 音频回退放大竞态窗口

`audio recovery` 不是失败条件，但它把 RTMP 的就绪时间推后约 1.2 秒，使一次性断言更容易落在视频尺寸发布之前。它属于放大器，不是根因。

## 7. 影响与严重级别

| 维度 | 影响 | 级别 |
|---|---|---|
| 运行时播放 | 本机回环样例未发现可重复的 RTMP 播放故障；复跑均通过 | 低 |
| 发布验收 | 四项协议验收存在假阴性，可能阻断发布或造成错误结论 | 中 |
| 证据完整性 | 报告结论与保留 JSON 不一致；失败日志已被覆盖 | 中 |

综合定级为**中**：不需要按 RTMP 功能故障处理，但需要修正验收方法和证据管理，否则同类失败会在后续版本重复出现。

## 8. 建议

### 8.1 修正断言

把一次性 A/V 检查改为带期限的等待：

```cpp
Check(Until([]{return g_player.HasAudio() && g_player.HasVideo();}, 5),
      "network source provides real audio and video");
```

也可以改为等待 `CoreInfo().tracks` 同时包含 audio 与 video，或等待
`CoreInfo().width > 0 && CoreInfo().height > 0`。这样仍能验证真实音视频，不会掩盖加载失败。

### 8.2 保留每次运行的独立日志

`protocol-<name>.log` 改为带时间戳或运行编号的文件，例如
`protocol-rtmp-20261022-155259.log`；`protocol-acceptance.json` 也按运行保存，不再原地覆盖。这样失败现场不会被后续探针覆盖。

### 8.3 报告绑定运行标识

`core-validation.md` 的协议段应引用同一次运行的 JSON 文件名、时间戳和 EXE/DLL 散列；如果重跑结果不同，应保留两份记录并如实标注间歇性，而不是让后一次运行覆盖前一次。

### 8.4 重新生成验收记录

修正断言后重跑四项协议，重新生成协议验收 JSON，并更新 `core-validation.md` 的对应段落。若仍出现间歇失败，应保留失败日志与复跑日志，按“flaky”记录，而不是直接写成协议不支持。

### 8.5 不把本问题混同为功能缺陷

本次不需要修改 RTMP 协议处理或解码路径。若要进一步降低就绪状态滞后，可单独评估 250 ms 状态刷新与 1.2 秒音频回退阈值；但这属于播放器状态发布策略，不是本问题的必要条件。

## 9. 附录：关键位置与证据

| 内容 | 位置 |
|---|---|
| 网络验收断言 | `test/core_regression.cpp` 第 175–180 行 |
| `HasVideo` 判定 | `src/player.h` 第 320–321 行 |
| 视频尺寸与音频轨读取 | `src/mpv_backend.cpp` 第 620–633 行 |
| 250 ms 慢刷新 | `src/mpv_backend.cpp` 第 849–851 行 |
| 音频回退条件 | `src/mpv_backend.cpp` 第 658–661 行 |
| `StartTap` 与日志 | `src/player.cpp` 第 1717–1734 行 |
| 保留失败 JSON | `test/results/core/protocol-acceptance.json` |
| 历史失败日志 | `test/results/core/protocol-rtmp-concurrent-failure.log` |
| 失败进程日志 | `test/results/core/backend.log` |
| 协议验收脚本 | `test/run_core_protocol_checks.py` |
| 验收报告 | `docs/core-validation.md` |

## 10. 修复与验证结果

- 网络验收改为有期限地等待实际音视频输出、尺寸及音频输出参数；就绪后验证相对起点的播放推进，随后确认收到真实 PCM 帧。超时会打印 `ready/failed/audio/video/width/height/position/vo/ao/sample_rate/channels/tap/pcm_frames`，不以直播非零时间戳代替出帧证明。
- 每次播放器验收或探测使用独立运行编号、UTC 起止、JSON 及日志。失败和超时同样保留；`--probe-only` 使用单独指针，不覆盖播放器验收。原 `protocol-acceptance.json` 为历史串行失败，保留不变。
- 报告生成器解析最近运行指针后，引用具体运行的 `report.json`，核对运行编号、测试 EXE / 核心 DLL / 测试源码散列，以及逐协议断言与后台日志的大小和 SHA256。历史串行与并发失败均保留，不再称为“仅并发失败”。
- 修复后连续 **5 轮四协议验收全部通过**：20 个场景、260 项网络断言；RTMP 每轮约 **3.28–3.43 秒**。同一新测试二进制的原有 **136 项核心回归**通过。
- **4 项证据保护检查**通过：探测与验收分离、失败现场保留及拒绝发布、日志篡改拒绝、超时保留部分输出。实际 probe-only 运行进一步确认验收指针及已保存记录不变。
- 真实纯音频负例在约 7 秒期限后正确拒绝，诊断为 `audio=1 video=0 width=0 height=0`；新增等待没有放宽必须具备视频的条件。
- 正式 `dist/LumenPlayer.exe` 和核心 DLL 哈希保持不变；本次开始时的用户设置、列表亦保持不变。历史安装时的用户数据证明与本次状态分开记录。

修复汇总：[network-acceptance-fix.json](../test/results/network-acceptance-fix.json)。其中包含 5 次具体运行、探测运行、负例日志、保护检查和修复前恢复归档的散列。新版发布报告：[core-validation.md](core-validation.md)；最近一次播放器验收索引：[protocol-acceptance-latest.json](../test/results/core/protocol-acceptance-latest.json)。索引可更新，报告引用的具体运行不随索引更新。
