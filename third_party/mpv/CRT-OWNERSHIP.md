# Subtitle heap crash diagnosis

The historical native core used LLVM-MinGW UCRT with a MINGW64/MSVCRT libass
dependency. Although the exported C functions and public structure layouts
matched, ASS allocation ownership crosses the application/library boundary:

- `sub/sd_ass.c:mp_ass_add_default_styles` assigns core `strdup` output to
  `ASS_Style.Name` and `mp_ass_set_style` assigns `FontName`.
- `sub/sd_ass.c:fill_plaintext` and `sub/osd_libass.c` assign core allocations to
  `ASS_Event.Text`.
- libass later releases these fields in `ass_free_track`, `ass_free_event` or
  `ass_flush_events`; `sub/ass_mp.c:mp_ass_set_style` can also free a library
  allocated `FontName` in the core.

An isolated C probe (`build/libass_allocator_probe.c`) creates styles/events and
frees the track without loading video, a GPU, fonts, subtitle parsing or PNG
encoding. With historical MINGW64 libass, MSVCRT-owned strings pass 20 iterations;
UCRT-owned strings crash during the first `ass_free_track`. With UCRT64 libass,
the same UCRT-owned strings pass all 20 iterations. Evidence is recorded in
`build/libass-allocator-ucrt-evidence.json`.

The repair uses the same library versions from the official MSYS2 UCRT64
repository. It migrates libass, the entire font/text dependency chain, shaderc,
SPIRV-Cross, libgcc, libstdc++ and winpthread together. Moving the shader/compiler
chain avoids two CRT families competing for the same GCC runtime DLL names.
The existing libplacebo 7.377 DLL is itself built with LLVM UCRT and is retained
unchanged. The original FFmpeg SDK is also retained unchanged. The resulting
30-DLL closure has no MSVCRT imports, no unresolved non-system imports and no
extra DLLs; staging now enforces the CRT invariant.

The old core SHA `072ef3be91f53554133c3013b1b26f90df5331c20d2e80124f88900030ba3e72`
and its prior null-audio or single-screenshot successes are historical evidence
only. They do not establish stable external subtitle switching. The UCRT64
candidate SHA is
`f49b928b7e7411c714729ac082517f7e4c408f2d66c56789f8802804143951fa`;
application playback, repeated track switching, screenshots, graphics and audio
acceptance must be run against that exact dependency closure.

The first real application integration run against this candidate passed the
ASS/SAMI switch and produced valid rendered PNGs (ASS 575,170 bytes; SAMI
571,012 bytes), along with hardware/software switching, frame stepping, rapid
seeks, speeds, A-B loop, selected-track PCM and callback teardown checks. It
reached 74 checks without a native crash; that run stopped on a later HDR test
which read dimensions before the first asynchronous frame became ready. This
partial integration run is recorded in `test/results/core/core-ucrt-first.log`.
It is evidence that the former subtitle crash is repaired, and is not a claim
that the complete application regression or physical HDR/receiver acceptance
has passed. The stable runtime and manifest were promoted to this UCRT64
closure for the subsequent full application rebuild and acceptance.
