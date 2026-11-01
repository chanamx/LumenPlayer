# mpv PCM Tap Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Preserve same-connection network caption audio while using the pinned libmpv playback backend.

**Architecture:** Add a built-in `lumen` audio filter that requests interleaved float PCM only, keeps the incoming rate and layout, and forwards every frame unchanged after the format conversion. Register one callback per core through a small public C extension; core synchronization and a dedicated callback mutex make unregister wait for in-flight delivery, and destroying the owner handle automatically unregisters it.

**Tech Stack:** mpv 6a262d11f6 C sources, existing mpv autoconverter and client locking, Meson, native C/C++ smoke test.

---

### Task 1: Stable callback contract

Create `include/mpv/lumen.h`, extend `player/client.c` and `player/client.h`. Define sample count per channel, callback-only data lifetime, unknown PTS as NaN, zero samples as EOF, no callback reentry into mpv, one owner per core, and synchronous unregister. Protect callback state with its own mutex in addition to the core lock used by the setter. Automatically unregister before client destruction.

### Task 2: Minimal format-only filter

Create `audio/filter/af_lumen.c`; register in `filters/user_filters.c`, `filters/user_filters.h` and `meson.build`, and install the extension header. Request `AF_FORMAT_FLOAT` without adding a sample-rate or channel-layout constraint. Tap nonempty audio and EOF, preserve all frame metadata and ownership, and avoid allocations or blocking work in the callback path beyond existing format conversion.

### Task 3: Reproducible patch and verification

Create `test/mpv_pcm_tap.patch` against the pinned pristine source and concise integration notes. Verify patch application on pristine copies, C and C++ header compatibility, syntax with the real SDK/build configuration, exported DLL symbol, and live delivery with a WAV fixture. Check one-owner enforcement, unregister during a blocked callback, automatic cleanup when an owning secondary client is destroyed, PCM sample/rate/channel/PTS validity, EOF and seek cancellation integration. Root backend owns bounded queuing, seek generations and audible-time gating. The build agent owns all dependency downloads and the DLL build.
