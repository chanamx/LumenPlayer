# Configured hold speed

The 2.2.3 mouse path uses ffStage and UpdateFFStage to select 2/4/8 after 0/1.1/2.2 seconds. Only the keyboard arrow path reads holdRate. Unify temporary mouse and keyboard speed on the existing holdRate setting; remove the stage field, updater and hardcoded rates. Keep the keyboard switch scoped to the keyboard, while the shared multiplier remains adjustable for mouse hold.

Preserve the previous normal speed and play/pause intent on release/cancellation, the existing live/model-loading guards, the seek-based fallback, and the 2.2.3 single/double-click fix. Do not add timers, dependencies or playback backends.

Use a native regression on unchanged 2.2.3 to reproduce the wrong requested rate. Validate several fractional/integer rates past the old stage thresholds, both input sources, paused/playing states, unsupported-rate fallback, cancellation and non-1x restoration. Run the same controls and full playback checks, a real-core hold path, and an exact-release EXE smoke. Preserve source/runtime identity and user files before publishing 2.2.4.
