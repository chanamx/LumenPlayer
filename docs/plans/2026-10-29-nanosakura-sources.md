# NanoSakura and resilient model sources implementation plan

**Goal:** Offer the measured 282.5 MiB Japanese-to-Chinese NanoSakura INT8 package in the real caption manager, with native inference and more verified download routes for every existing model.

**Architecture:** Keep the existing Chinese recognizer and llama translation path. Add a single-threaded, lazy-loaded ONNX Runtime translator using the already pinned runtime. Implement only the fixed model's NFC / ByteLevel BPE tokenizer contract, using Windows normalization and the existing PCRE2 dependency; validate tokens against the upstream Hugging Face implementation before accepting inference results. Continue to publish whole models atomically after SHA-256 verification.

**Tech stack:** C++17 Win32, ONNX Runtime C API 1.28.2, PCRE2, WinHTTP; Python for provenance and isolated acceptance tooling.

The user has authorized implementation and testing. The checkout has no Git metadata; source and release snapshots replace worktree/commit steps. No delegated agents are used.

## Approach and boundaries

1. Native ONNX inference reuses the installed runtime and avoids shipping Python or .NET. A self-contained .NET companion would add a second runtime; a Python companion would enlarge and complicate deployment. The native approach is chosen subject to tokenizer conformance and real inference tests.
2. Add only the selected Japanese-to-Simplified-Chinese model. Reject other source/target pairs with a visible explanation. Do not change the default recognizer or silently select/download a model.
3. Prefer independently available, fixed upstream URLs. An extra URL is accepted only with recorded reachability and identity evidence. Community mirrors are labelled. No expired signed CDN URLs or invented mirror paths are stored.
4. Automatic downloads may move to another independent source after a restricted response, while suppressing further requests to that restricted site in the current job. Manual provider choices remain explicit. TLS and full-file hashes remain mandatory.

## Tasks and validation

1. Snapshot affected source/release files, current manifest and hashes of user data under `build/nanosakura-integration-20261029/before/`.
2. Inspect official NanoSakura, Moonshine, HF, ModelScope and GitHub metadata; save exact revisions, file hashes and source probes in that lab. Expand the catalog from those records, retaining all seven existing model identities.
3. Add `src/nanosakura.h/.cpp` and a tokenizer conformance / native inference probe. Verify Japanese, punctuation, whitespace, decomposed Unicode, special tokens, and generated Chinese against the reference tokenizer. Measure warm/cold latency, memory and cancellation with the actual pinned DLL.
4. Extend `CaptionTranslation`, source/target validation and the model dialog. Test unsupported pair handling, generation reset, stop/reload, incomplete output and bilingual export.
5. Improve `CaptionModelStore` cross-source recovery. Extend the existing local WinHTTP fault server to cover restricted sites, fallback, wrong range, corrupt files, cancellation/resume and preservation. Perform actual public downloads through the native store.
6. Run isolated actual Japanese media playback through PCM -> Moonshine -> NanoSakura -> caption display/export. Recheck baseline Chinese seek recovery and relevant playback regression tests. Record exactly what each test proves.
7. Stage a model-free 2.2.0 runtime and installer, verify file identity and installer data preservation, then atomically publish application assets. Preserve settings, playlists, downloaded models and other user files. Document verified sources, observed behavior and limitations.
