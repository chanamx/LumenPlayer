# Behavior-preserving optimization

Goal: reduce repeated allocations, unused queries, idle wakeups and duplicate build assets while preserving playback, subtitles, controls and recovery evidence.

1. Preserve the installed executable, source baseline and current user file hashes. Measure the same release/runtime/media with balanced before/after runs.
2. Separate lightweight playback state from list snapshots; remove an unused render statistics query and repeated immutable protocol discovery; let an empty PCM consumer wait for work. Keep all timing, queue limits, caches, model and decoding policies unchanged.
3. Build and run existing native/core/network/release checks. Compare actual CPU and private memory with the saved baseline, both with and without captions.
4. Deduplicate verified immutable assets in build/staging with hard links. Before enabling this, make asset updates replace the destination entry atomically so a later build cannot alter the pinned source through a shared link. Verify replacement against a disposable hard-link fixture.
5. Install the accepted build preserving current settings/list, update the existing installer, clean test copies, and regenerate exact evidence/report hashes. Record space reclaimed separately from runtime memory and report inconclusive measurements candidly.
