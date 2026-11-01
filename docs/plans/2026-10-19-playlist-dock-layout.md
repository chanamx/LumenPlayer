# Playlist dock layout (1.6.1)

Use the existing Win32/D2D UI and INI. Dock the playlist from below the shared window title bar to the bottom edge; its left boundary is also the right boundary of the video child windows, subtitles and playback strip. Hide/show and resize the dock without reopening media or changing playback state.

Reduce the default width from 348 to 320 logical pixels. Drag the left divider, persist width on release/capture loss, and provide a header reset button plus double-click reset. Keep the preferred logical width across DPI changes and temporary window constraints. Bound the sidebar to at least 240 logical pixels and reserve at least 320 for playback; a constrained window must not overwrite the preferred width.

Adapt transport controls to the remaining width, using a separate time row and a small overflow menu at narrow widths. Keep every playback function reachable and match drawing rectangles to hit targets. A docked list stays open while the video and transport are used.

Verify resize, reset, persistence, capture cancellation, playback state and actual child window dimensions. Cover narrow/wide windows, default/min/max/custom widths, full screen and 100/150/200% DPI; inspect native renders, run the full regressions and release smokes, then install with recovery backup and preserve user INI/list data. Finish release manifest and owned temporary-file cleanup.
