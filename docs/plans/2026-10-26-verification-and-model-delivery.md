# Playback verification and model delivery investigation Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Reproduce two reported UI state problems on the current release, broaden actual playback testing, and verify optional model downloads and resource-comparable multilingual ASR candidates before changing the published product.

**Architecture:** Use an isolated copy of the exact current release for native UI work and preserve its executable identity. Keep new test programs, downloaded models, reports, and temporary files under D:/LumenPlayer/build/verification-20261026. Measure candidates serially against the current model using the same native Sherpa/ORT DLLs, CPU thread count and paced audio; distinguish transcription from translation and model-only measurements from full-player measurements.

**Tech Stack:** Win32 C++, libmpv, Sherpa ONNX C API, ONNX Runtime 1.28.2, Python orchestration, Computer Use, existing native regression tools.

---

### Task 1: Preserve baseline and prepare test evidence

**Files:** Create build/verification-20261026/baseline.json and isolated runtime; read release-manifest.json, src/main.cpp, src/captions.cpp, installer/build-installer.ps1.

1. Record hashes of production sources, distributed immutable files, settings and playlists.
2. Copy the release EXE and create isolated DLL/model hard links without user data.
3. Record machine CPU, memory, GPU, display/output capabilities and test executable identities.
4. Keep all failures, timeouts, output and cleanup records. This checkout has no Git metadata, so use file identities and project-local backups rather than a worktree or commits.

### Task 2: Reproduce UI states through actual app interaction

**Files:** Read src/main.cpp:1302 and src/main.cpp:1417; write native-ui-observations.json.

1. Launch the isolated existing release through Computer Use and select its uniquely returned window.
2. Use the real file picker to open a known video and verify output.
3. Select software decoding, inspect settings and the actual core status, and compare the displayed decoder name.
4. Enable an embedded ASS subtitle, inspect actual rendered text and the CC active appearance, then disable/re-enable it.
5. Exercise keyboard navigation, resize/fullscreen, list, pause/seek, external subtitle and model toggles where feasible.
6. Report each defect as reproduced, not reproduced, or blocked; do not equate message-driven fixtures with actual mouse tests.

### Task 3: Expand executable playback testing

**Files:** Existing test/core_regression.cpp, test/run_core_protocol_checks.py, test/test_backend_cancellation.py, test/test_mpv_pcm_tap.py, test/test_core_audio_rates.py; add a focused current-source stress harness if existing tools cannot cover duration/lifecycles.

1. Verify fresh native-core, real PCM, decoder, track, subtitle, seek and cancellation runs.
2. Exercise local formats and isolated HTTP/DASH/RTSP/RTMP servers with retained per-run records.
3. Run a sustained playback and repeated caption/seek/track lifecycle scenario; record responsiveness, working set, private bytes, CPU, output readiness and cleanup.
4. Inventory physical HDR/AVR availability and clearly bound unsupported device tests.
5. Run a model-free isolated application to verify ordinary playback and missing-model UI behavior.

### Task 4: Verify authoritative model delivery

**Files:** Read test/prepare_caption_model.py, third_party/models/*/lumen-model.json and official upstream model catalogues; create research/model-catalog.json and download-verification.json.

1. Execute Web Composite Search and Sn Search Code, then verify exact upstream files, revisions, sizes, licenses and hashes.
2. Test the current model's official GitHub and Hugging Face delivery where available; include ModelScope only with verified publisher/artifact correspondence.
3. Download into .part files, verify exact hashes, reject archive paths/links outside the target, and activate only complete models.
4. Test the unconverted official ONNX model using the current player runtime as well as the packaged ORT baseline.
5. Build and inspect an isolated installer payload without model weights, and test playback, missing model, download/reinstall and recovery expectations before considering a published package change.

### Task 5: Measure language candidates and translation feasibility

**Files:** Create test/verify_asr_candidates.cpp and a project-local orchestration/report script if needed; use existing test audio and attributed upstream test waves.

1. Select bounded candidates: lightweight English, bilingual Chinese/English, French streaming, Japanese-capable multilingual and German-capable multilingual.
2. Run fresh serial processes with one CPU inference thread, the same allocator policy and 200 ms paced streaming audio when the model is actually streaming.
3. Record initialization, model bytes, working set/private/peak memory, CPU real-time factor, block P95/max, first output in wall time, backlog and recognition text.
4. Use real reference text when available; avoid accuracy rankings from unrelated single-language clips or model-generated ground truth.
5. Label offline chunked recognition separately; measure its latency rather than presenting throughput as live subtitle delay.
6. Check Chinese-to-English and English-to-Chinese translation independently. Do not assume bilingual ASR performs translation or Whisper translation works in both directions.

### Task 6: Deliver evidence and a concrete decision

**Files:** Create docs/verification-and-model-options-2026-10-26.md and machine-readable results under build/verification-20261026.

1. Report reproduction results and materially new test coverage with exact artifact identities.
2. Recommend only candidates supported by observed performance and explicit quality limits.
3. Explain model-free installer savings and the minimal verified delivery route, including offline failure/cancel/recovery behavior.
4. Preserve source/runtime/user-data baselines and terminate only test processes created by this run.
