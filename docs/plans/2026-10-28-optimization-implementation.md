# Lumen Player 2.1.2 Optimization Implementation Plan

**Goal:** Implement the confirmed caption correctness fixes and the highest-priority lifecycle/data-preservation improvements from the 2.1.1 audit, then ship a verified local build.

**Architecture:** Reuse SubtitleTrack and the existing background stop state. The same function supplies rendered subtitle text and dirty detection. ASR startup has cooperative hash cancellation; translation owns its verification/loading worker so recognition can start independently. Export merges successful bilingual results onto the complete original track. Keep the verified UCRT64 dependencies and same playback core.

**Tech Stack:** C++17, native Win32/Direct2D, libmpv, current ASR and llama runtimes, PowerShell, Python, Inno Setup. There is no Git metadata; retain a recovery ZIP and hash snapshots instead of commits/worktrees.

## Steps and checks

1. Save owned source/scripts, manifest and current EXE under `build/optimization-20261028/before/`; record all current dist files by hash. Reuse the 2.1.1 boundary results as failing before evidence.
2. Update `src/subtitle.cpp` / `.h` with symmetric duplicate checks and complete bilingual merge. Check shifted replay, genuine repeated speech, sorted lookup, missing/late/changed-source translations, and actual SRT save/readback.
3. Update `src/main.cpp`: shared visible-text selection; expiry bounded by source end; late translations still export; central final-result collection for Tick and export. Use controlled clock plus real native rendering and save function for expiry/export regressions.
4. Update `src/main.cpp` / `src/caption_translation.cpp` / `.h`: wire cancellation through startup SHA verification, cancel on disabling captions, stop safely through existing background state, and perform translation SHA/loading on its own worker. Check cancellation during hashing/loading, off/on, file/model changes, and real ASR + translation output.
5. Update `installer/lumenplayer.iss` to preserve settings/playlists on normal and silent uninstall. Update provenance/docs. Build and exercise isolated installer upgrade/uninstall with sentinel user data.
6. Update `build.ps1` / `test.ps1`: default test runtime under build, refuse test output into dist, pass the isolated executable to network checks; never copy test binaries into formal runtime. Reuse existing fixtures and result paths where practical.
7. Remove verified unused FmtTimeShort/ffIndex and replace Widen/Narrow aliases with existing UTF-8 helpers. Keep legacy installer scripts archived in recovery; no broad media-engine rewrite.
8. Build optimized 2.1.2 in an isolated directory; run targeted native regressions and the relevant existing playback/caption suites serially with host memory/time guards. Measure before/after cancellation and recognition-versus-translation readiness without claiming unmeasured global speedups.
9. Validate the optimized EXE itself in an isolated release runtime. Publish by atomic file replacement after checks; preserve settings/playlists/models, create the 2.1.2 manifest and installer, and save a repeatable version-specific validation entry plus evidence. Recheck user-data hashes and report remaining limits.

Full physical HDR/AVR, true cross-monitor DPI and hours-long joint performance measurements are outside the narrow fixes; retain those audit items instead of reporting them as completed.
