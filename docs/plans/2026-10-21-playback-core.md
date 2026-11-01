# Playback Core Expansion Implementation Plan

**Goal:** Deliver broader decoding and streaming compatibility, adjustable HDR mapping, robust audio output, complete tracks/subtitles and precise playback controls while preserving real-time captions and user data.

**Architecture:** Embed a pinned libmpv core in the existing native video window, using gpu-next/D3D11 and WASAPI. Keep the previous backend as a recoverable compatibility path. Add a small versioned PCM callback to the pinned core so captions consume the selected decoded audio from the same connection, including network streams.

**Tech Stack:** C++17/Win32, libmpv, FFmpeg, libplacebo, libass, WASAPI, existing Zipformer INT8.

## 1. Reproducible core and audio tap

- Files: `test/prepare_mpv_core.py`, `test/build_mpv_core.py`, `third_party/mpv`, `test/mpv_pcm_tap.patch`.
- Pin source/packages, verify archive hashes, reject archive traversal, keep all tools/cache within this workspace.
- Build a dynamic native core with gpu-next/D3D11, WASAPI, libass and required protocols. Record exact configuration, dependency licenses and hashes.
- Add `lumen` filter with float interleaved PCM callback, no recognition or UI inside the callback, unregister before destruction.
- Validate actual callback data, pause/seek/track/close lifecycle and bounded storage.

## 2. Backend and settings contract

- Files: `src/playback_types.h`, `src/mpv_backend.h`, `src/mpv_backend.cpp`, `src/player.h`, `src/player.cpp`.
- Worker owns potentially blocking core calls; UI consumes cached snapshots and queues requests.
- Keep latest requested seek visible until it settles, discard stale PCM at all timeline discontinuities.
- Expose actual decoder/output/track state separately from preferences. Toggle hardware without reopening network input.
- Audio: device selection, shared/exclusive, IEC 61937 with PCM fallback, pitch correction over 0.1–12x. Captions and non-unit speed require PCM and explain that state.
- HDR: automatic/SDR output, tone method, peak detection/decay, target peak, reference white and gamut controls.

## 3. Native UI and source integration

- Files: `src/main.cpp`, `src/media_source.cpp`, `src/playlist.cpp`, `build.ps1`, `resources/LumenPlayer.rc`.
- Preserve current controls and add discoverable track/audio/HDR/playback menus with persisted preferences.
- Add A–B controls, paused frame stepping and full speed range. Clear per-file AB and explicit track choices on new media.
- Render file/embedded ASS using libass and embedded fonts; accept SMI. Preserve real-time caption overlay independently.
- Permit HTTP(S), DASH, RTSP(S), RTMP(S), RTP/UDP/SRT safely; keep M3U import behavior and prevent special execution URI schemes.
- Stop redundant network pre-opening when using the new core. Preserve cancellation and single-connection caption behavior.

## 4. Verification and release

- Keep existing regressions and add native core acceptance using real media: hardware/software, HDR/HLG/SDR, multiple tracks/languages, ASS effects and SMI, frame steps/AB, extreme speed audio, rapid seeks and caption resets.
- Test DASH over HTTP and RTSP/RTMP on loopback servers; record unsupported or unavailable test equipment explicitly.
- Verify PCM fallback on this stereo device, and distinguish compressed receiver transport from local object decoding. Do not claim local Atmos/DTS:X object reconstruction without a real decoder and test.
- Build isolated release, exercise actual installed application, preserve latest INI/list data, verify installed artifact hashes and licenses, remove only known obsolete/temporary artifacts.

## Recovery

The repository has no Git metadata. Pre-change source, documentation, executable and user data are preserved in `build/backups/pre-core-20261021.zip` (SHA256 `f046006a6260b76ebed7cd55ad3417693ad6b72ee5bc64f56e2b9bfc1a5cdd34`; actual archive manifest/hash is authoritative). Existing 1.8 renderer and software backend remain available until new-core acceptance passes.
