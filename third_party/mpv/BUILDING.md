# LumenPlayer native playback core

The shipped core is `runtime/libmpv-2.dll`, built from mpv commit
`6a262d11f63733d37e3ee6d5fafcb1be1ef83952` plus `test/mpv_pcm_tap.patch`.
The 121 MB DLL directly under `third_party/mpv` is an upstream comparison SDK;
it has no Lumen PCM callback and must not be packaged as the playback core.

The native build uses Python 3.13 x64, LLVM-MinGW UCRT 20251104 (Clang 21.1.5),
Git and Ninja. Python build tools are installed only in `build/mpv-tools`, using
the wheel hashes in `build-tools-requirements.txt`. Downloads, pip cache and
temporary files remain under the project build directory.

From the project root, run `python test/build_mpv_core.py`. Missing mpv source
is recovered from the fixed archive hash, then the PCM patch is applied with
`git -c core.autocrlf=false apply` so Windows Git settings cannot change source
line endings. A source directory already present is preserved. Individual
preparation steps are `python test/prepare_mpv_core.py --source` and
`python test/prepare_mpv_core.py --dependencies`.

The FFmpeg input is the existing LGPL shared 9.0.2 SDK and its retained archive
`third_party/ffmpeg/packages/ffmpeg-n9.0-latest-win64-lgpl-shared-9.0.zip`, SHA256
`11afb4cc03bd4c5ab123dfe396b31aa432bb227cfae0047a56ed632c17c59e0f`.
`avfilter-12.dll` is restored from that exact package; libraries from a second
FFmpeg build are never mixed in. The D3D11 libplacebo 7.377 SDK is reproduced by
`test/build_libplacebo.py`; its source and DLL hashes have separate provenance.
Fixed MSYS2 UCRT64 subtitle/font/compiler dependencies are listed in
`dependency-lock.json`. The full font, shaderc, SPIRV-Cross and GCC runtime
closure uses UCRT64; the native core rejects MSVCRT DLLs during closure staging.
The original package archives, package build records and license texts remain
available in the project. Source links and build instructions do not constitute
a claim that every external library's complete corresponding source is bundled
inside the application folder.

The core enables D3D11, D3D11VA/DXVA2 integration, WASAPI, libass, iconv, zlib,
SIMD and the built-in scaletempo2 filter, with `gpl=false`. The existing FFmpeg
shared SDK provides its enabled demuxers, decoders and network protocols. Lua,
JavaScript, external scripts and unnecessary graphics/platform backends are
excluded. Runtime playback settings still decide hardware decoding and encoded
audio passthrough; compiling a decoder does not establish hardware or receiver
support on a particular machine.

The build collects only the DLL import closure into `runtime/` and fails on
missing non-system imports or obsolete extra DLLs. `build-provenance.json`
records the core commit, PCM patch hash, exact options, compiler, tools, build
script hashes, dependency lock hash and each runtime file's size and SHA256.
The media libraries remain separate DLLs. Freetype uses its FTL license choice;
GCC runtime libraries retain their runtime exception. License texts are included
for libass, LLVM runtime, the MSYS2 dependencies and the existing renderer.

The CRT family is an ownership requirement for libass, beyond matching its API
version. mpv assigns `strdup`/`malloc` results directly to public ASS styles and
events, which libass later frees. Historical MINGW64/MSVCRT libass mixed with the
LLVM UCRT core crashes on external SRT/SAMI track rendering or disposal. A
minimal allocator probe reproduces this without video or font rendering; the
same UCRT allocations pass after moving libass and the complete font closure to
UCRT64. The old dependency lock is retained only as
`dependency-lock-mingw64-historical.json`; it is not an accepted build input.

Validation artifacts include the source patch replay audit, DLL import closure
audit and the real PCM callback/rate tests under the project `test/results` and
`build` directories. A successful null-output test does not validate a physical
audio endpoint, HDMI receiver or HDR display.

After release validation, the Meson object directory `build/mpv-core-build`,
tool cache `build/mpv-tools`, generated `build/mpv-pkgconfig`, temporary
`build/windows-interop` and the two `build/mpv-replay-audit*` directories may be
removed. Retain the fixed source/package archives, source patch, dependency
lock, license texts, generated SDKs and `runtime/`. For an offline rebuild also
retain `build/mpv-tools`; deleting it requires fetching the locked wheels again.
Source replay has been checked byte for byte. The current PE image retains its
link timestamp, so this recipe does not promise identical binary hashes across
two independent clean builds.
