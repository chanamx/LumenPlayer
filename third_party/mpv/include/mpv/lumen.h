/* LumenPlayer extension to the pinned libmpv build. SPDX-License-Identifier: ISC */
#ifndef MPV_CLIENT_API_LUMEN_H_
#define MPV_CLIENT_API_LUMEN_H_

#include "client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Receive audio from the built-in "lumen" filter. Samples are interleaved
 * float PCM; samples is the frame count per channel, not the float count.
 * rate and channels describe this block. pts is its media timestamp in seconds,
 * or NaN when unknown. The buffer is valid only until the callback returns.
 * A NULL buffer with samples == 0 signals EOF; rate/channels are then zero and
 * pts is NaN. Reset/seek is not EOF and does not produce this signal.
 *
 * This runs synchronously on a playback/filter thread and MUST return promptly.
 * Only copy/enqueue bounded data here. Do not call any mpv API, wait for another
 * thread that uses mpv, or destroy a handle from inside this callback.
 *
 * A filter can prefetch audio while paused and ahead of audible playback. The
 * host must gate delivery using its pause/seek generation and media clock.
 * Encoded passthrough has no PCM; disable passthrough while using this filter.
 */
typedef void (*mpv_lumen_audio_callback)(void *opaque, const float *pcm,
                                       int samples, int rate, int channels,
                                       double pts);

/**
 * Register the callback for the core used by ctx. Insert "lumen" in --af to
 * enable delivery. This is a Lumen extension, not an upstream libmpv API.
 * Only one handle can own a registration per core. A different handle receives
 * MPV_ERROR_INVALID_PARAMETER until that registration is cleared.
 *
 * Pass callback == NULL to unregister this handle's callback; opaque is then
 * ignored. On return no callback from that registration is in flight. Clearing
 * a different handle's registration is a successful no-op. Destroying the
 * owning handle also unregisters synchronously. As with other libmpv APIs, the
 * caller must not race a call against destruction of the same handle.
 *
 * Thread-safe through core synchronization. Returns 0 on success or a negative
 * mpv_error. A NULL ctx is MPV_ERROR_INVALID_PARAMETER.
 */
MPV_EXPORT int mpv_lumen_set_audio_callback(mpv_handle *ctx,
                                           mpv_lumen_audio_callback callback,
                                           void *opaque);

#ifdef __cplusplus
}
#endif
#endif
