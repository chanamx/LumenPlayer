# Frameless window border repair plan

**Goal:** Keep the player's custom dark frame intact after native file dialogs and related activation/layout changes.

**Architecture:** Keep the native video and layered UI architecture. Handle both initial and recalculated client-area sizing, and prevent default nonclient painting from overlaying custom pixels. Retain normal minimized-window behavior and existing resize, maximize, fullscreen and modal ownership semantics.

**Tech Stack:** Win32 C++17, the existing native regression harness, fixed UCRT64 media core and packaged model/configuration.

1. Save the current 2.0.0 EXE, all source and release-record hashes, settings/list hashes and a recoverable source/records ZIP. Use an isolated runtime.
2. Build a native regression against the unchanged source. Record initial client/window geometry and edge pixels across nonclient activation and repaint. Exercise an owned native modal loop without controlling unrelated applications.
3. Apply the smallest common window-procedure repair. Repeat the native scenarios at 100/125/150/200 percent UI scale, media/software/system video paths, playlist dock, resize, fullscreen, maximize and minimize/restore. Check actual playback and controls.
4. Build the release and modern core regression; run affected tests with current dependencies/configuration. Preserve previous broad codec/protocol/performance evidence as historical evidence, rather than relabeling it as current measurements.
5. Replace the project distribution without changing settings/list/runtime/model, retain compression, rebuild the installer, write a patch-specific release manifest and verification report, and clean isolated runtime copies. Record the Computer Use launch-approval timeout separately from native acceptance.

The checkout has no Git metadata. The source ZIP and original EXE are the recovery checkpoint. The user's fix request authorizes implementation and validation; no additional design approval or delegated agents are needed.
