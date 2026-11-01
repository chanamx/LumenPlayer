#include "video.h"
#include <dxgi1_6.h>
#include <libplacebo/d3d11.h>
#include <libplacebo/renderer.h>
#include <libplacebo/shaders/custom.h>
#include <libplacebo/filters.h>
#include <libplacebo/cache.h>
#define PL_LIBAV_IMPLEMENTATION 0
#include <libplacebo/utils/libav.h>
extern "C" bool LumenMapAvFrame(pl_gpu,struct pl_frame*,const struct pl_avframe_params*);
extern "C" void LumenUnmapAvFrame(pl_gpu,struct pl_frame*);
extern "C" AVFrame* LumenMappedAvFrame(const struct pl_frame*);
extern "C" {
#include <libavutil/pixdesc.h>
}
#include <new>

namespace {
struct SharedShaders {
    pl_cache cache=nullptr;
    SharedShaders() {pl_cache_params params{};params.max_total_size=16*1024*1024;cache=pl_cache_create(&params);}
    ~SharedShaders(){pl_cache_destroy(&cache);}
};
pl_cache ShaderCache() {static SharedShaders shared;return shared.cache;}
struct Render {
    HWND window = nullptr;
    pl_log log = nullptr;
    pl_d3d11 d3d = nullptr;
    pl_swapchain swap = nullptr;
    pl_renderer renderer = nullptr;
    pl_cache cache = nullptr;
    pl_tex textures[4]{};
    pl_frame image{};
    pl_render_params params{};
    pl_color_map_params colorMap{};
    const pl_hook* hook = nullptr;
    VideoRenderInfo info{};
    std::mutex mutex;
    void (*logger)(const char*) = nullptr;
    bool mapped = false, forceSdr = false, hdrScreen = false;
    float displayPeak = 0, displayBlack = 0;
    double aspect = 1;
    DWORD displayCheck = 0;
    bool debug = false;
    ~Render() {
        if (d3d) {
            if (mapped) LumenUnmapAvFrame(d3d->gpu,&image);
            pl_mpv_user_shader_destroy(&hook);
            pl_renderer_destroy(&renderer);
            for (auto& texture : textures) pl_tex_destroy(d3d->gpu,&texture);
            pl_swapchain_destroy(&swap);
            pl_d3d11_destroy(&d3d);
        }
        pl_log_destroy(&log);
    }
    static void Log(void* data,pl_log_level level,const char* message) {
        auto* self = static_cast<Render*>(data);
        if (self->logger && (level <= PL_LOG_WARN || self->debug)) self->logger(message);
        if (level <= PL_LOG_ERR && self->hook) self->info.shaderError = true;
    }
    void Display() {
        const DWORD now = GetTickCount(); if(displayCheck && now-displayCheck<1000)return; displayCheck=now;
        hdrScreen=false;displayPeak=displayBlack=0;
        IDXGIDevice* device=nullptr; IDXGIAdapter* adapter=nullptr; IDXGIFactory1* factory=nullptr;
        if (SUCCEEDED(d3d->device->QueryInterface(__uuidof(IDXGIDevice),(void**)&device))) {
            device->GetAdapter(&adapter); device->Release();
        }
        if (adapter) { adapter->GetParent(__uuidof(IDXGIFactory1),(void**)&factory);adapter->Release(); }
        const HMONITOR monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
        if(factory) {
            for(UINT a=0;;++a) {
                IDXGIAdapter1* item=nullptr; if(factory->EnumAdapters1(a,&item)==DXGI_ERROR_NOT_FOUND)break;
                if(!item)break;
                for(UINT n=0;;++n) {
                    IDXGIOutput* output=nullptr; if(item->EnumOutputs(n,&output)==DXGI_ERROR_NOT_FOUND)break;
                    if(!output)break;
                    DXGI_OUTPUT_DESC desc{};output->GetDesc(&desc);
                    if(desc.Monitor==monitor) {
                        IDXGIOutput6* output6=nullptr;
                        if(SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6),(void**)&output6))) {
                            DXGI_OUTPUT_DESC1 details{};
                            if(SUCCEEDED(output6->GetDesc1(&details))) {
                                hdrScreen=details.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                                displayPeak=details.MaxLuminance;displayBlack=details.MinLuminance;
                            }
                            output6->Release();
                        }
                    }
                    output->Release();
                }
                item->Release();
            }
            factory->Release();
        }
        pl_color_space hint{};
        if(!forceSdr && hdrScreen && mapped) {
            hint=image.color;
            if(pl_color_transfer_is_hdr(hint.transfer) && displayPeak>0) {
                hint.hdr.max_luma=displayPeak;hint.hdr.min_luma=displayBlack;
            }
        }
        else {hint.primaries=PL_COLOR_PRIM_BT_709;hint.transfer=PL_COLOR_TRC_SRGB;}
        pl_swapchain_colorspace_hint(swap,&hint);
    }
    void Fit(pl_frame& target,int w,int h) const {
        const double ar=std::max(0.001,aspect);
        const float dw=(float)std::min<double>(w,h*ar), dh=(float)std::min<double>(h,w/ar);
        target.crop={(w-dw)/2,(h-dh)/2,(w+dw)/2,(h+dh)/2};
    }
    void OutputColor(pl_frame& target,bool fileEncoding=false) const {
        if(pl_color_transfer_is_hdr(target.color.transfer))return;
        // mpv's default SDR policy preserves ordinary source gamma, while
        // converting HDR to the SDR reference display's pure 2.2 curve.
        if(!info.hdrInput && (image.color.transfer==PL_COLOR_TRC_BT_1886 || image.color.transfer==PL_COLOR_TRC_GAMMA22 || image.color.transfer==PL_COLOR_TRC_SRGB))
            target.color.transfer=image.color.transfer;
        if(target.color.transfer==PL_COLOR_TRC_SRGB && (!fileEncoding || image.color.transfer==PL_COLOR_TRC_SRGB))
            target.color.transfer=PL_COLOR_TRC_GAMMA22;
    }
    bool Draw(pl_frame& target) {
        params.hooks = hook ? &hook : nullptr; params.num_hooks = hook ? 1 : 0;
        const bool ok=pl_render_image(renderer,&image,&target,&params);
        info.renderErrors=pl_renderer_get_errors(renderer).errors;
        if(!ok || info.shaderError) {
            if(hook) {
                info.shader=false;info.shaderError=true;pl_mpv_user_shader_destroy(&hook);
                params.hooks=nullptr;params.num_hooks=0;
                return pl_render_image(renderer,&image,&target,&params);
            }
        }
        return ok;
    }
};
void* Create(HWND window,void (*logger)(const char*),bool software) {
    auto self=std::unique_ptr<Render>(new(std::nothrow) Render);
    if(!self)return nullptr;self->window=window;self->logger=logger;self->debug=GetEnvironmentVariableW(L"LUMEN_VIDEO_DEBUG",nullptr,0)>0;
    pl_log_params log{};log.log_cb=Render::Log;log.log_priv=self.get();log.log_level=self->debug?PL_LOG_DEBUG:PL_LOG_WARN;
    self->log=pl_log_create(PL_API_VER,&log);
    auto device=pl_d3d11_default_params;device.force_software=software;device.min_feature_level=D3D_FEATURE_LEVEL_10_0;device.max_frame_latency=2;
    self->d3d=pl_d3d11_create(self->log,&device);if(!self->d3d)return nullptr;
    self->info.softwareGpu=self->d3d->software;
    IDXGIDevice* dxgi=nullptr;IDXGIAdapter* adapter=nullptr;
    if(SUCCEEDED(self->d3d->device->QueryInterface(__uuidof(IDXGIDevice),(void**)&dxgi))) {
        if(SUCCEEDED(dxgi->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC desc{};adapter->GetDesc(&desc);
            WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,self->info.adapter,sizeof(self->info.adapter),nullptr,nullptr);adapter->Release();
        }
        dxgi->Release();
    }
    pl_d3d11_swapchain_params swap{};swap.window=window;swap.color_bits=10;
    self->swap=pl_d3d11_create_swapchain(self->d3d,&swap);if(!self->swap)return nullptr;
    self->renderer=pl_renderer_create(self->log,self->d3d->gpu);if(!self->renderer)return nullptr;
    self->cache=ShaderCache();pl_gpu_set_cache(self->d3d->gpu,self->cache);
    self->params=pl_render_default_params;
    // Match mpv gpu-next defaults: Lanczos enlargement/chroma, Hermite
    // reduction, sigmoid upscaling, peak detection, automatic tone mapping.
    self->colorMap=pl_color_map_default_params;
    self->colorMap.tone_mapping_function=&pl_tone_map_auto;
    self->params.color_map_params=&self->colorMap;
    self->params.upscaler=&pl_filter_lanczos;
    self->params.downscaler=&pl_filter_hermite;
    self->params.plane_upscaler=&pl_filter_lanczos;
    self->params.background=PL_CLEAR_COLOR;self->params.border=PL_CLEAR_COLOR;
    self->params.background_transparency=0;
    return self.release();
}
void Destroy(void* handle) { delete static_cast<Render*>(handle); }
bool Upload(void* handle,const AVFrame* frame,int rotation,double aspect) {
    auto& self=*static_cast<Render*>(handle);std::lock_guard<std::mutex> lock(self.mutex);
    if(self.mapped) {LumenUnmapAvFrame(self.d3d->gpu,&self.image);self.mapped=false;}
    self.image={};pl_avframe_params params{};params.frame=frame;params.tex=self.textures;params.map_dovi=true;
    if(!LumenMapAvFrame(self.d3d->gpu,&self.image,&params))return false;
    self.mapped=true;self.aspect=aspect;self.image.rotation=(pl_rotation)rotation;
    self.info.transfer=frame->color_trc;self.info.primaries=frame->color_primaries;
    const auto* desc=av_pix_fmt_desc_get((AVPixelFormat)frame->format);
    self.info.inputDepth=desc?desc->comp[0].depth:0;
    self.info.hdrInput=pl_color_transfer_is_hdr(self.image.color.transfer);
    return true;
}
bool Present(void* handle) {
    auto& self=*static_cast<Render*>(handle);std::lock_guard<std::mutex> lock(self.mutex);
    if(!self.mapped)return true;
    RECT rect{};GetClientRect(self.window,&rect);int w=rect.right,h=rect.bottom;
    if(w<1 || h<1 || !IsWindowVisible(self.window))return true;
    self.Display();if(!pl_swapchain_resize(self.swap,&w,&h))return false;
    pl_swapchain_frame swap{};if(!pl_swapchain_start_frame(self.swap,&swap))return SUCCEEDED(self.d3d->device->GetDeviceRemovedReason());
    pl_frame target{};pl_frame_from_swapchain(&target,&swap);self.Fit(target,w,h);self.OutputColor(target);
    self.info.outputDepth=swap.color_repr.bits.color_depth;
    self.info.hdrOutput=pl_color_transfer_is_hdr(target.color.transfer);
    const bool ok=self.Draw(target);const bool submitted=pl_swapchain_submit_frame(self.swap);
    if(submitted)pl_swapchain_swap_buffers(self.swap);
    if(ok&&submitted) {
        if(!self.info.presentedFrames && self.logger) {
            char message[256]{};snprintf(message,sizeof(message),"D3D11 presented input=%d-bit hdr=%d output=%d-bit hdr=%d gpu=%s",self.info.inputDepth,self.info.hdrInput,self.info.outputDepth,self.info.hdrOutput,self.info.adapter);
            self.logger(message);
        }
        ++self.info.presentedFrames;
    }
    return ok&&submitted;
}
bool Configure(void* handle,const char* text,size_t length,bool forceSdr) {
    auto& self=*static_cast<Render*>(handle);std::lock_guard<std::mutex> lock(self.mutex);
    const pl_hook* candidate=length?pl_mpv_user_shader_parse(self.d3d->gpu,text,length):nullptr;
    if(length&&!candidate)return false;
    pl_mpv_user_shader_destroy(&self.hook);self.hook=candidate;self.forceSdr=forceSdr;self.displayCheck=0;
    pl_renderer_flush_cache(self.renderer);
    self.info.shader=candidate!=nullptr;self.info.shaderError=false;
    return true;
}
void Info(void* handle,VideoRenderInfo* info) {auto& self=*static_cast<Render*>(handle);std::lock_guard<std::mutex> lock(self.mutex);*info=self.info;}
bool Readback(void* handle,int w,int h,BYTE* rgba) {
    auto& self=*static_cast<Render*>(handle);std::lock_guard<std::mutex> lock(self.mutex);if(!self.mapped)return false;
    pl_tex_params params{};params.w=w;params.h=h;params.format=pl_find_named_fmt(self.d3d->gpu,"rgba8");
    params.renderable=true;params.host_readable=true;params.blit_dst=true;
    if(!params.format)return false;pl_tex tex=pl_tex_create(self.d3d->gpu,&params);if(!tex)return false;
    pl_frame target{};target.num_planes=1;target.planes[0].texture=tex;target.planes[0].components=4;
    for(int i=0;i<4;++i)target.planes[0].component_mapping[i]=i;
    target.color.primaries=PL_COLOR_PRIM_BT_709;target.color.transfer=PL_COLOR_TRC_SRGB;
    target.repr.sys=PL_COLOR_SYSTEM_RGB;target.repr.levels=PL_COLOR_LEVELS_FULL;target.repr.bits.color_depth=8;target.repr.bits.sample_depth=8;
    self.Fit(target,w,h);self.OutputColor(target,true);bool ok=self.Draw(target);
    pl_tex_transfer_params download{};download.tex=tex;download.ptr=rgba;download.row_pitch=(size_t)w*4;
    if(ok)ok=pl_tex_download(self.d3d->gpu,&download);pl_tex_destroy(self.d3d->gpu,&tex);return ok;
}
void* Recover(void* handle,bool softwareGpu) {
    auto* old=static_cast<Render*>(handle);
    AVFrame* frame=nullptr;HWND window=old->window;auto logger=old->logger;
    const double aspect=old->aspect;const int rotation=old->image.rotation;
    if(old->mapped)frame=av_frame_clone(LumenMappedAvFrame(&old->image));
    delete old;
    void* next=Create(window,logger,softwareGpu);
    if(next && frame && !Upload(next,frame,rotation,aspect)) {Destroy(next);next=nullptr;}
    av_frame_free(&frame);return next;
}
}
extern "C" __declspec(dllexport) bool LumenVideoApi(unsigned version,VideoApi* api) {
    if(version!=1 || !api)return false;*api={Create,Destroy,Upload,Present,Configure,Info,Readback,Recover};return true;
}
