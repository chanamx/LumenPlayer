# Lumen Player 1.5.0 implementation plan

**Goal:** Preserve playback compatibility while removing the player-side browser speech bridge and improving caption toggling, right-button speed control, executable identity, and seek feedback.

**Architecture:** Keep the native streaming Zipformer recognizer and existing playback backends. Settings activation pauses once without holding playback; media opening retains its existing caption gate. Software seek previews are coalesced on the existing decode thread, with short audio feedback and the original playback state restored on release.

**Tech stack:** C++17, Win32, Direct2D, Media Foundation, WASAPI, FFmpeg, sherpa-onnx/ORT.

1. Back up the current source, installed executable/configuration, release evidence, and player-owned browser bridge. Retain original model weights and all browser data.
2. Inspect remaining startup costs. Keep the existing verified ORT model unless direct measurements demonstrate a repeatable improvement without quality/compatibility loss.
3. Replace the two-engine caption wrapper with the existing local implementation; remove browser discovery, injection, pipes, bridge runtime, and engine selection UI. Retain the legacy caption-enabled INI key for compatibility.
4. Make settings activation pause once and allow immediate manual playback during background loading. Test manual resume, remaining paused, load failure, opening with captions enabled, and cancellation.
5. Add persistent right-button speed enable/value controls (1.25–8x, 0.25x increments). Retain left-button staged fast forward and backend fallback. Restore state on button release/capture loss.
6. Coalesce software seek preview requests on the decoder thread. Preview loaded picture/audio, suspend speech collection while dragging, preserve volume/mute and playback state, and handle slow decoding, release, cancellation, and EOF.
7. Embed a multi-size icon matching the existing blue play logo; use it for the native main window and verify the executable resources.
8. Run native regression, video compatibility, caption recognition, keyboard/mouse/capture/DPI/short-window checks, and release/path smoke. Inspect native UI renders, preserve user settings, publish the tested executable and release hashes, and clean only task-generated files.

Validation: `powershell -File test.ps1`, `python test/render_previews.py`, `powershell -File build.ps1 -Install`, and the release smoke scripts. Record actual metrics and untested boundaries in a new release report; keep earlier reports historical.
