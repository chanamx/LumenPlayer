# Pinned mpv PCM tap

This patch is only for `third_party/mpv/mpv-6a262d11f6` (upstream commit
`6a262d11f6`). It adds `mpv/lumen.h` and the exported symbol
`mpv_lumen_set_audio_callback` without changing the upstream client API version.
Detect that symbol explicitly when loading a DLL; an ordinary libmpv build does
not have it.

Callback order is `(opaque, pcm, frames_per_channel, sample_rate, channels, pts)`.
Data is float interleaved, valid only during the callback. Unknown PTS is NaN.
`pcm == NULL && frames_per_channel == 0` is EOF (zero rate/channels, NaN PTS).
The filter emits EOF once per uninterrupted stream; resetting does not emit EOF.
It requests only `AF_FORMAT_FLOAT`, preserving rate, channel layout and frame
metadata. Place `lumen` before `scaletempo2` so caption PCM remains at original
media speed. The host must disable encoded passthrough when enabling this tap.

There is one registration owner per playback core. Registration from a different
handle fails with `MPV_ERROR_INVALID_PARAMETER`; a non-owner clear is a no-op.
The setter takes the core dispatch lock and the callback mutex. Delivery holds
only the callback mutex; unregister cannot return until any in-flight callback
has finished. Owner destruction clears under that same mutex before freeing the
handle. This destruction path deliberately avoids the dispatch lock because mpv
also destroys script clients from the core thread.

Do not call any mpv API from a callback or wait on a thread that needs mpv. Copy
or enqueue bounded work only. There is no new thread or growing queue in the
filter. The callback is pre-output and can run while paused or ahead of audible
playback. The host queue must track its own seek/file/track generation, reset
before issuing a seek, and gate PCM using pause, seek readiness and media PTS.
Drain queued blocks before processing EOF, and reject stale generations. ASR
runtime work and resampling to mono 16 kHz belong to the existing worker.

Build integration is limited to the new audio source, filter registration, the
public header install list, and client callback state. No rate or channel layout
constraints are added. No application playback/UI files are changed here.

Validation: compile both C and C++ users of the public header; build the pinned
DLL; run `python test/test_mpv_pcm_tap.py <absolute-custom-dll-path>`. The real DLL
smoke test uses null output and generated mono/stereo/5.1 WAV files. It checks
the original sample rate/channel count, frame count, finite non-silent PCM,
monotonic PTS, one EOF, conflicting owners, synchronous unregister and owner
destruction while a callback is deliberately blocked. These tests intentionally
block only their test callback to exercise the lifetime guarantee. They do not
validate a physical endpoint, AVR, live network or application seek queue logic.
