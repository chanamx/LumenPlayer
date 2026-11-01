# Evidence-led deep tuning implementation plan

**Goal:** Remove the measured large-playlist and UI allocation bottlenecks, release discarded caption storage, and make the normal caption-off action responsive while retaining its final utterance.

**Architecture:** Reuse the existing path comparator and native containers. Validate any current-item position hint against the actual folder entry before use. Keep controls local to each operation/frame, and retain synchronous caption teardown for media destruction and application exit; the off action may finish in one background management thread, with UI-owned results and session checks.

**Tech Stack:** C++17, Win32/Direct2D, existing UCRT64 libmpv and Zipformer runtime, native regression harnesses and isolated paired diagnostic processes.

1. Preserve source, published EXE, static runtime and user-file hashes. Reuse the supplied diagnostic harness with fresh paired baseline/candidate executions.
2. Replace playlist import's nested title search with one comparator-consistent index. Verify local case folding, exact network URLs, duplicate title overwrite, caps and persistence. Reduce repeated navigation lookup and static control-label allocations; share each frame's controls without retaining stale UI snapshots.
3. Extend the subtitle prefix index for strictly increasing appends, rebuilding after out-of-order/duplicate updates. Release cue/index capacity on explicit discard, preserving stopped-but-exportable text.
4. Separate the caption-off request from final teardown. Stop capture first, preserve full tail decoding, publish completion to the UI and gate restart on the same completion. Bind pending results to a media session and verify rapid off/on, startup cancellation, seek, new media, export and exit. Keep the synchronous path as the shutdown boundary.
5. Measure the same microbenchmarks and real caption lifecycle before/after. Run focused invariants, modern and compatibility regressions, formal UI/media/network acceptance, installed smoke and installer identity checks. Preserve settings/list, update release records and report supported gains plus inconclusive memory results.

No new dependencies, quality reduction, decode-buffer changes, global working-set trimming or speculative render-pipeline replacement are planned. This checkout has no Git metadata; the verified source ZIP and binary checkpoints provide recovery.
