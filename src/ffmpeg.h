#pragma once
#include "common.h"
#include <mfidl.h>
#include <mmreg.h>
#include <memory>
struct AVFrame;

// Local-file software decoding, sharing the player's existing PCM/BGRA output.
// FFmpeg DLLs load on demand; native playback works without this optional path.
class FfmpegReader {
public:
    struct VideoFormat { int width = 0, height = 0, rotation = 0; double fps = 30, aspect = 1; };
    FfmpegReader();
    ~FfmpegReader();
    bool Open(const std::wstring& path, const WAVEFORMATEX* audio, bool audioOnly = false,
        const std::shared_ptr<std::atomic<bool>>& cancel = {});
    bool ConfigureAudio(const WAVEFORMATEX* audio);
    void Interrupt(bool cancel = true);
    bool Live() const;
    bool Seek(double seconds);
    HRESULT Read(DWORD* stream, DWORD* flags, LONGLONG* time, IMFSample** sample);
    bool HasVideo() const;
    bool HasAudio() const;
    bool DecodesAudio() const;
    double Duration() const;
    VideoFormat Video() const;
    void SetNativeVideo(bool enabled);
    const AVFrame* NativeVideo() const;
    bool SetHardwareDecoding(bool enabled);
    bool HardwareDecoding() const;
    bool SetOutputChannelMask(uint64_t mask);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
