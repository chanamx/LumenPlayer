#include "video.h"

namespace {
VideoApi api{};
std::once_flag once;
HMODULE rendererModule = nullptr;
void RendererLog(const char* message) { LogMsg("Video: %s", message); }
}

bool VideoRenderer::Available() {
    std::call_once(once, [] {
        rendererModule = LoadLibraryExW(PathJoin(ExeDir(), L"LumenVideo.dll").c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto get = rendererModule ? reinterpret_cast<bool (*)(unsigned, VideoApi*)>(GetProcAddress(rendererModule,"LumenVideoApi")) : nullptr;
        if (!get || !get(1, &api)) {
            LogMsg("GPU renderer unavailable error=%lu", GetLastError());
            if (rendererModule) FreeLibrary(rendererModule);
            rendererModule = nullptr; api = {};
        }
    });
    return rendererModule != nullptr;
}
VideoRenderer::~VideoRenderer() { if (state_) api.destroy(state_); }
bool VideoRenderer::Open(HWND window, bool forceSoftwareGpu) {
    if (!Available()) return false;
    if (state_) api.destroy(state_);
    state_ = api.create(window, RendererLog, forceSoftwareGpu);
    return state_ != nullptr;
}
bool VideoRenderer::Upload(const AVFrame* frame, int rotation, double aspect) { return state_ && api.upload(state_, frame, rotation, aspect); }
bool VideoRenderer::Present() { return state_ && api.present(state_); }
bool VideoRenderer::Configure(const std::wstring& path, bool sdr) {
    if (!state_) return false;
    std::string text;
    if (!path.empty()) {
        FILE* file = _wfopen(path.c_str(), L"rb");
        if (!file) return false;
        _fseeki64(file, 0, SEEK_END); const auto size = _ftelli64(file); _fseeki64(file, 0, SEEK_SET);
        if (size <= 0 || size > 4*1024*1024) { fclose(file); return false; }
        text.resize((size_t)size); const bool read = fread(text.data(),1,text.size(),file) == text.size(); fclose(file);
        if (!read || text.find('\0') != std::string::npos) return false;
        if (text.compare(0,3,"\xef\xbb\xbf") == 0) text.erase(0,3);
        if (text.find("//!HOOK ") == std::string::npos) return false;
    }
    if(!api.configure(state_,text.data(),text.size(),sdr))return false;
    shaderPath_=path;forceSdr_=sdr;return true;
}
VideoRenderInfo VideoRenderer::Info() const { VideoRenderInfo info; if(state_)api.info(state_,&info); return info; }
bool VideoRenderer::Readback(int w,int h,std::vector<BYTE>& pixels) {
    if (!state_ || w<1 || h<1 || w>16384 || h>16384 || (uint64_t)w*h>67108864) return false;
    pixels.resize((size_t)w*h*4); return api.readback(state_,w,h,pixels.data());
}
bool VideoRenderer::Recover(bool softwareGpu) {
    if(!state_)return false;
    state_=api.recover(state_,softwareGpu);
    if(!state_)return false;
    if(!Configure(shaderPath_,forceSdr_))Configure(L"",forceSdr_);
    return true;
}
