# Network acceptance reliability implementation plan

> Execute the authorized repair in this workspace; preserve the original issue and failing evidence.

**Goal:** Make network acceptance wait for actual decoded output and retain each run independently, with reports bound to its exact evidence.

**Architecture:** Keep the playback runtime unchanged. Use bounded native readiness/progress/PCM checks, one directory per acceptance or probe run, and a small latest-run pointer for discovery. Publication resolves that pointer to an immutable result and verifies each log hash.

**Tech stack:** Existing C++ native regression, Python standard library, pinned libmpv and local MediaMTX.

1. Preserve the reported failed JSON, overwritten probe logs, native regression, existing report/manifest, and affected sources in a recovery archive.
2. Update `test/core_regression.cpp`: wait for both decoded A/V outputs, verify relative position advances and PCM frames actually arrive, and print cached state on timeout.
3. Update `test/run_core_protocol_checks.py`: unique run ID/UTC times, separate acceptance/probe paths, retained timeout output and per-process backend log segment, immutable result and hashed logs, failure-aware latest pointers.
4. Update `test/finalize_core_release.py`: verify the selected run, ID, timestamps, test/core hashes and individual logs; link its immutable result in the generated report. Preserve the serial failure without attributing it exclusively to concurrency.
5. Add targeted standard-library checks for evidence preservation, pointer separation and tampering rejection; build CoreTest and run its full native regression plus repeated real four-protocol acceptance and one probe-only run. Verify an audio-only source still fails the video requirement.
6. Update the issue resolution, regenerate report/manifest, verify all recorded hashes and confirm installed EXE/DLL and user files unchanged. Remove only recreated disposable tool/build directories needed to preserve the existing cleanup record.
