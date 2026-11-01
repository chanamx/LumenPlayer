// Compile upstream's FFmpeg helpers as C, as required by libplacebo.
#include <libplacebo/utils/libav.h>

// Keep upstream helpers static so unused decoder-allocation helpers do not
// retain libavcodec. Export only the three operations used by this bridge.
bool LumenMapAvFrame(pl_gpu gpu,struct pl_frame* out,const struct pl_avframe_params* params) {
    return pl_map_avframe_ex(gpu,out,params);
}
void LumenUnmapAvFrame(pl_gpu gpu,struct pl_frame* frame) {pl_unmap_avframe(gpu,frame);}
AVFrame* LumenMappedAvFrame(const struct pl_frame* frame) {return pl_get_mapped_avframe(frame);}
