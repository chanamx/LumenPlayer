# Lumen Player optimization implementation plan

**Goal:** Improve playback stability, coherent controls, responsive UI and resource use while retaining the native player and both caption engines.

**Architecture:** Keep Media Foundation / WASAPI / Direct2D and the existing source layout. Fix shared lifecycle and timing errors before changing presentation. Use the existing native APIs without adding runtime dependencies.

**Tech Stack:** C++17, LLVM-MinGW, Win32, Media Foundation, WASAPI, Direct2D, DirectWrite, Vosk.

## 1. Preserve and reproduce

- Preserve source, executable and settings in `build/backups/before-optimization-20261018.zip`.
- Add `build.ps1` with release and regression builds; never rewrite the user's configuration.
- Establish process CPU/memory and executable size baselines.

## 2. Playback and lifecycle (`src/player.h`, `src/player.cpp`, `src/soda.*`)

- Correct partial audio consumption, bounded decoding, pause/seek/EOF and replay behavior.
- Serialize audio endpoint operations and cancel stale decode work after seeking.
- Stop the caption audio tap before replacing or stopping its consumers.
- Track rate changes consistently; preserve pause/volume during fallback.
- Validate buffers and finite inputs; release endpoint/frame/model resources after use.
- Make caption discontinuities and shutdown safe; retain completed captions for export.

## 3. Interaction and presentation (`src/main.cpp`, `src/gfx.*`)

- Correct fullscreen layout and minimum-window controls; scroll short settings panels.
- Expand slider hit areas, preview drag position and commit one seek on release.
- Restore previous mute volume; persist keyboard changes; cancel holds on capture/focus loss.
- Separate panel dismissal, title/window commands, video clicks and double clicks.
- Add consistent hover/focus/disabled states, tooltip text, keyboard navigation and shortcut help.
- Keep captions/toasts inside available viewport; clip long filenames and subtitle labels.
- Repaint only when dirty or visible content changes; skip minimized rendering and avoid unbounded brush/text caches.

## 4. Verification and delivery (`test/`, `README.md`, `docs/validation.md`)

- Run native assertions against subtitle parsing, UI layout/input and real media decoding.
- Exercise short/normal windows, DPI, fullscreen, seek, rate, pause, EOF, replay, caption toggles and invalid input.
- Render welcome/settings/playback views to image artifacts and inspect them.
- Measure idle CPU/memory and playback resources; report exact evidence and limits.
- Rebuild `dist/LumenPlayer.exe`, preserve user assets/configuration, document supported formats and environment-dependent behavior.

No destructive asset/model/toolchain cleanup. Keep archived original files separate from the runtime folder. Desktop screenshot helper timed out twice; interactive desktop acceptance remains a separate boundary unless the helper becomes available.
