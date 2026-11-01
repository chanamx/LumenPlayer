# M3U8 and HLS Implementation Plan

> Execute the authorized implementation and verification in this chat, using the executing-plans workflow. No Git checkout exists; use the verified pre-hls backup for recovery.

**Goal:** Play local and HTTP(S) HLS VOD/live streams and import each M3U/M3U8 list as a separate persistent playlist folder.

**Architecture:** Reuse FFmpeg for HLS, WinHTTP for bounded playlist text retrieval, and the existing native playlist UI/store. Distinguish HLS manifests from channel lists; HLS segments/variants remain one playable entry. Prepare network inputs on one cancellable background worker, commit on the UI thread, and retain the current media if preparation fails. Caption network playback from the actual PCM output to avoid a second live connection drifting from the picture.

**Tech Stack:** C++17, Win32, WinHTTP, pinned FFmpeg shared libraries, WASAPI, existing Zipformer CTC.

1. Add shared HTTP(S)/URI identity, names and relative resolution; bounded UTF-8 M3U parsing; persist folder source and entry names with backward-compatible version 1 reading and atomic version 2 writing. Test malformed lists, URL case/query preservation, relative local/network entries, HLS classification, repeated import, names and restart.
2. Permit bounded HLS/network FFmpeg I/O with a protocol whitelist, cancellation and per-operation timeout. Keep ordinary local containers isolated from external resources. Separate stream preparation from audio endpoint setup, preserve local media behavior, disable seeks for live inputs, and cancel blocking read before joins.
3. Add one background source job with latest-request cancellation, UI commit and clean shutdown. Route file picker/drop/command line and a native Ctrl+U network dialog through shared import logic. Expose URL opening from the list and open menu. Show live status and imported names without displaying URL queries.
4. Tap already-rendered network PCM for captions; test actual playback, paused output, VOD seeks, live capture and cancellation. Keep existing file caption behavior.
5. Generate local HLS TS/fMP4/master/encrypted fixtures and a loopback HTTP server with dynamic live manifests, redirects, query addresses and stalled/error responses. Run focused tests and full regressions, inspect native renders, test the exact release EXE and special installation paths, install preserving user INI/list data, clean only verified owned duplicate artifacts, and record hashes/report/backup.
