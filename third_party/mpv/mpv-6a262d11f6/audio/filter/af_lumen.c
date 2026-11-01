/* LumenPlayer same-connection PCM tap. SPDX-License-Identifier: LGPL-2.1-or-later */
#include <math.h>

#include "audio/aframe.h"
#include "audio/format.h"
#include "common/common.h"
#include "common/global.h"
#include "filters/f_autoconvert.h"
#include "filters/filter_internal.h"
#include "filters/user_filters.h"
#include "player/client.h"

struct priv {
    struct mp_pin *input;
    bool eof_sent;
};

static void af_lumen_process(struct mp_filter *f)
{
    struct priv *p = f->priv;
    if (!mp_pin_in_needs_data(f->ppins[1]))
        return;

    struct mp_frame frame = mp_pin_out_read(p->input);
    if (frame.type == MP_FRAME_AUDIO) {
        p->eof_sent = false;
        struct mp_aframe *af = frame.data;
        int samples = mp_aframe_get_size(af);
        if (samples > 0) {
            double pts = mp_aframe_get_pts(af);
            uint8_t **data = mp_aframe_get_data_ro(af);
            mp_client_lumen_audio(f->global->client_api, (const float *)data[0],
                samples, mp_aframe_get_rate(af), mp_aframe_get_channels(af),
                pts == MP_NOPTS_VALUE ? NAN : pts);
        }
    } else if (frame.type == MP_FRAME_EOF && !p->eof_sent) {
        p->eof_sent = true;
        mp_client_lumen_audio(f->global->client_api, NULL, 0, 0, 0, NAN);
    }
    mp_pin_in_write(f->ppins[1], frame);
}

static void af_lumen_reset(struct mp_filter *f)
{
    struct priv *p = f->priv;
    p->eof_sent = false;
}

static const struct mp_filter_info af_lumen_filter = {
    .name = "lumen",
    .priv_size = sizeof(struct priv),
    .process = af_lumen_process,
    .reset = af_lumen_reset,
};

static struct mp_filter *af_lumen_create(struct mp_filter *parent, void *options)
{
    struct mp_filter *f = mp_filter_create(parent, &af_lumen_filter);
    talloc_free(options);
    if (!f)
        return NULL;

    mp_filter_add_pin(f, MP_PIN_IN, "in");
    mp_filter_add_pin(f, MP_PIN_OUT, "out");

    struct mp_autoconvert *conv = mp_autoconvert_create(f);
    if (!conv) {
        talloc_free(f);
        return NULL;
    }
    // Format only: preserve the source rate, layout, PTS and frame attributes.
    mp_autoconvert_add_afmt(conv, AF_FORMAT_FLOAT);
    mp_pin_connect(conv->f->pins[0], f->ppins[0]);
    struct priv *p = f->priv;
    p->input = conv->f->pins[1];
    return f;
}

const struct mp_user_filter_entry af_lumen = {
    .desc = {
        .description = "LumenPlayer same-connection PCM audio callback",
        .name = "lumen",
    },
    .create = af_lumen_create,
};
