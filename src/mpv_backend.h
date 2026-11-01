#pragma once
#include "player.h"
#include "playback_types.h"
#include <memory>

// libmpv is owned by one worker. Public methods only queue work or read a
// snapshot, so opening a network source never blocks the window thread.
class MpvBackend final : public Backend {
public:
    using PcmSink = std::function<void(const float*, int frames, int rate,
                                       int channels, double mediaPts)>;
    explicit MpvBackend(HWND videoWnd, const CoreOptions& options = {});
    ~MpvBackend() override;
    static bool Available();
    bool Open(const std::wstring& path, bool autoplay = true) override;
    void Close() override;
    bool CancelLoading(); // Cancels the active load without blocking the window thread.
    void Play() override;
    void Pause() override;
    void Seek(double sec) override;
    void PreviewSeek(double sec) override;
    bool SeekReady() const override;
    bool SetRate(double rate) override;
    double Position() const override;
    double Duration() const override;
    bool IsPlaying() const override;
    bool IsEof() const override;
    int Width() const override;
    int Height() const override;
    bool HasAudio() const override;
    bool AudioLost() const override;
    void SetVolume(float volume) override;
    void SetMuted(bool muted) override;
    bool Failed() const override;
    bool Loading() const;
    bool IsCompatibility() const override { return true; }
    bool Live() const override;
    bool HardwareDecoding() const override;
    bool HardwareReady() const override;
    bool ConfigureVideo(const std::wstring& shaderPath, bool forceSdr) override;
    VideoRenderInfo RenderInfo() const override;
    bool RecoverVideo(bool softwareGpu) override;

    CoreStatus Status() const;
    CoreOptions Options() const;
    void SetOptions(const CoreOptions& options);
    bool SelectAudioTrack(int64_t id); // -1 = automatic; 0 = disabled.
    bool SelectSubtitleTrack(int64_t id); // -1 = automatic; 0 = disabled.
    void AddSubtitle(const std::wstring& path, bool select = true);
    void SetSubtitlePresentation(bool visible, double scale = 1);
    void SetABLoop(double a, double b); // Any negative point disables that end.
    void FrameStep(int frames = 1);
    void SetPcmSink(PcmSink sink);
    bool SupportsPcmTap() const;
    bool Screenshot(const std::wstring& path);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
