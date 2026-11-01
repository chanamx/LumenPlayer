#pragma once
#include "common.h"

struct AVFrame;
struct VideoRenderInfo {
    int inputDepth = 0, transfer = 0, primaries = 0, outputDepth = 0;
    bool hdrInput = false, hdrOutput = false, softwareGpu = false, shader = false, shaderError = false;
    unsigned renderErrors = 0;
    uint64_t presentedFrames = 0;
    char adapter[128]{};
};

// A POD C interface keeps the optional renderer and its compiler dependencies
// unloaded until video opens. AVFrame ABI is pinned to the bundled FFmpeg.
struct VideoApi {
    void* (*create)(HWND, void (*)(const char*), bool);
    void (*destroy)(void*);
    bool (*upload)(void*, const AVFrame*, int, double);
    bool (*present)(void*);
    bool (*configure)(void*, const char*, size_t, bool);
    void (*info)(void*, VideoRenderInfo*);
    bool (*readback)(void*, int, int, BYTE*);
    void* (*recover)(void*,bool);
};

class VideoRenderer {
public:
    ~VideoRenderer();
    static bool Available();
    bool Open(HWND window, bool forceSoftwareGpu = false);
    bool Upload(const AVFrame* frame, int rotation, double aspect);
    bool Present();
    bool Configure(const std::wstring& shaderPath, bool forceSdr);
    VideoRenderInfo Info() const;
    bool Readback(int width, int height, std::vector<BYTE>& rgba);
    bool Recover(bool softwareGpu);
private:
    void* state_ = nullptr;
    std::wstring shaderPath_;
    bool forceSdr_ = false;
};
