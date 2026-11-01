# Verified player optimization implementation plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement the verified low-risk optimization candidates while preserving media compatibility, caption latency, model quality and user data.

**Architecture:** Keep the existing Win32/libmpv/ORT architecture. Reuse native disk-space APIs and existing model ownership/atomic activation checks. Build, measure and package in `D:/LumenPlayer/build/tuning-20261029`; preserve the 2.2.1 baseline before editing. This checkout has no Git metadata, so use recovery archives and hash bindings.

**Tech Stack:** C++17, Win32, fixed UCRT toolchain, ONNX Runtime 1.28.2, Python standard library, PowerShell and Inno Setup.

---

User authorization: the verified audit was accepted with “好，实际进行优化”. Execute serially in this chat, report progress between phases, and complete the approved work without another design approval. No decoder/protocol removal, model precision changes, cache reductions or automatic idle unloading.

### Task 1: Freeze identity and baseline

- Check `test/finalize_model_ui_release.py --check-only` before edits.
- Archive project-owned sources/scripts, current EXE/manifest/settings; hash all runtime and user files.
- Build the baseline NanoTest and relevant native probes into isolated output directories before production edits. Use existing memory guards for real-model runs.

### Task 2: Model-free compaction support

Files: `test/compact_runtime.py`, `test/test_compact_runtime.py`.

- Add one runnable regression for model-free and legacy-model directories, symlink/junction escape refusal, hard-link skip, restore and user-file exclusion.
- Observe the model-free failure; include the optional legacy model only when its directory exists, retaining validation of malformed present assets.
- Keep XPRESS8K as the default. Do not touch managed user model-store contents.
- Run the regression and a real compress/restore on independent fixture files with byte/hash checks.

### Task 3: Installation peak-space preflight and owned cache cleanup

Files: `src/caption_models.cpp`, `test/caption_model_store.cpp`, `test/run_caption_model_store.py`.

- Add regressions for low space before network I/O, preservation of the installed version and user notes, partial-download credit, archive/decode headroom and integer overflow.
- In the existing worker, query available bytes for the destination caller; calculate remaining downloads plus extraction/decode peak and a small safety reserve. The old live model is already allocated and must not be counted twice.
- Reuse catalog-owned cache paths for successful fallback cleanup. Preserve unknown files, resumable cancellation and the existing atomic activation/rollback path.
- Run the actual native implementation against the local fault-injection HTTP server; retain all logs and verify request/Range behavior.

### Task 4: Local allocation experiments

Files: `src/nanosakura.cpp`, `test/nanosakura_native.cpp`, optionally `src/main.cpp` and a scoped layout probe.

- Measure unchanged baseline and candidate using identical Japanese inputs and fixed models in fresh processes.
- Release the first decoder step's no-longer-used initial state only after its OrtValue references are released; preserve cancellation and output generation. Retain only if output/latency/memory validation passes.
- Evaluate bounded caption/Toast layout reuse only with correct font/DPI/width invalidation and an equivalence/performance probe; avoid a new UI cache framework.

### Task 5: Storage and package experiments

- Use the existing storage audit's file IDs and SHA-256 groups. Deduplicate only demonstrably frozen, independently allocated copies; exclude current release bindings, active/writable models and user data. Do not perform a broad deletion or archive an actively referenced path.
- Measure actual stored-byte changes, separating hard links and logical file totals. If no safe subset is established, preserve it and record the concrete boundary.
- Compare normal/max installer compression with exactly the same payload, and keep a change only if size and resource measurements justify it. Preserve warm shader caches.

### Task 6: Validate and release

- Run appropriate native store, caption/translation lifecycle, full playback and rendering checks from an isolated directory, plus a real release-EXE smoke using the existing guard.
- Build and package a scoped patch release. Keep the fixed runtime DLL/CRT family and model catalog identities.
- Publish only after passing checks, with atomic per-file replacement and unchanged settings/playlists/models/user-file hashes. If the user's active EXE is locked, finish all independent validation before requesting closure.
- Write `docs/tuning-results-2.2.2-2026-10-29.md`, scoped machine-readable bindings and a matching check-only finalizer. Mark rejected experiments and unmeasured outcomes explicitly.
