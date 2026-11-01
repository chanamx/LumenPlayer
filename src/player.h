#pragma once
#include "common.h"
#include "ffmpeg.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfplay.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <condition_variable>
#include <map>
#include <memory>
#include "video.h"
#include "playback_types.h"

class Player;

class Backend {
public:
    virtual ~Backend() {}
    virtual bool Open(const std::wstring& path, bool autoplay = true) = 0;
    virtual void Close() = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Seek(double sec) = 0;
    virtual void PreviewSeek(double sec) { Seek(sec); }
    virtual bool SeekReady() const { return true; }
    virtual bool SetRate(double r) = 0;
    virtual double Position() const = 0;
    virtual double Duration() const = 0;
    virtual bool IsPlaying() const = 0;
    virtual bool IsEof() const = 0;
    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual bool HasAudio() const = 0;
    virtual bool AudioLost() const { return false; }
    virtual void SetVolume(float v) = 0;
    virtual void SetMuted(bool m) = 0;
    virtual bool Failed() const { return false; }
    virtual bool IsCompatibility() const { return false; }
    virtual bool Live() const { return false; }
    virtual bool HardwareDecoding() const { return false; }
    virtual bool HardwareReady() const { return HardwareDecoding(); }
    virtual bool ConfigureVideo(const std::wstring&, bool) { return false; }
    virtual VideoRenderInfo RenderInfo() const { return {}; }
    virtual bool RecoverVideo(bool) { return false; }
    virtual bool RenderFailed() const { return false; }
    virtual void Repaint() {}
};

// Classic Windows path: MFPlay -> Media Session -> EVR (DXVA2 hardware decoding).
class HwBackend : public Backend {
public:
    explicit HwBackend(HWND videoWnd);
    ~HwBackend() override;

    bool Init();
    void Shutdown();
    bool Open(const std::wstring& path, bool autoplay = true) override;
    void Close() override;
    void Play() override;
    void Pause() override;
    void Seek(double sec) override;
    bool SeekReady() const override { return !seekPending_.load(); }
    bool SetRate(double r) override;
    double Position() const override;
    double Duration() const override;
    bool IsPlaying() const override;
    bool IsEof() const override;
    int Width() const override { return vw_; }
    int Height() const override { return vh_; }
    bool HasAudio() const override { return hasAudio_; }
    bool HardwareDecoding() const override { return true; }
    void SetVolume(float v) override;
    void SetMuted(bool m) override;
    bool Failed() const override { return failed_.load(); }
    void Repaint() override;

    void OnEvent(int type, HRESULT hr);

private:
    struct Callback;
    void SubmitSeekLocked();
    HWND wnd_ = nullptr;
    Com<IMFPMediaPlayer> player_;
    Com<IMFPMediaItem> item_;
    Callback* cb_ = nullptr;
    bool initialized_ = false;
    std::atomic<bool> failed_{false};
    std::atomic<bool> eof_{false};
    std::atomic<bool> seekPending_{false};
    std::mutex seekM_;
    std::atomic<double> seekTarget_{0};
    uint64_t seekRequest_ = 0, seekSubmitted_ = 0;
    bool seekInFlight_ = false;
    std::mutex eventM_;
    std::condition_variable eventCv_;
    bool itemReady_ = false, hasAudio_ = false;
    std::atomic<bool> wantPlaying_{false};
    int vw_ = 0, vh_ = 0;
    double duration_ = 0;
    float volume_ = 1.0f;
    bool muted_ = false;
};

// Unified software / D3D11VA decoding and WASAPI, with optional GPU rendering.
class SwBackend : public Backend {
public:
    explicit SwBackend(HWND videoWnd, bool compatibility = false, bool preferHardware = false, bool softwareGpu = false);
    ~SwBackend() override;

    bool Open(const std::wstring& path, bool autoplay = true) override;
    bool OpenPrepared(const std::wstring& path, std::unique_ptr<FfmpegReader> prepared, bool autoplay);
    void SetRenderedTap(std::function<void(const float*,int,int,int,double)> sink);
    void Close() override;
    void Play() override;
    void Pause() override;
    void Seek(double sec) override;
    void PreviewSeek(double sec) override;
    bool SeekReady() const override { return previewApplied_.load() == previewRequest_.load() && !preview_.load(); }
    bool SetRate(double r) override;
    double Position() const override;
    double Duration() const override { std::lock_guard<std::mutex> lock(clockM_); return duration_; }
    bool IsPlaying() const override { return playing_.load(); }
    bool IsEof() const override { return eof_.load(); }
    int Width() const override { std::lock_guard<std::mutex> lock(frameM_); return frame_.w; }
    int Height() const override { std::lock_guard<std::mutex> lock(frameM_); return frame_.h; }
    bool HasAudio() const override { return hasAudio_; }
    bool AudioLost() const override { return audioLost_.load(); }
    void SetVolume(float v) override;
    void SetMuted(bool m) override;
    void Repaint() override { PaintFrame(); }

    // True once the WASAPI render endpoint has been initialized (we can emit sound).
    bool AudioReady() const { return audioOk_ && (selectedStreams_ & 2) && !audioLost_.load(); }
    bool AudioDeviceUnavailable() const { return audioDeviceUnavailable_; }
    bool Failed() const override { return failed_.load(); }
    bool IsCompatibility() const override { return ffmpeg_ != nullptr; }
    bool Live() const override { return ffmpeg_ && ffmpeg_->Live(); }
    bool HardwareDecoding() const override { return ffmpeg_ && ffmpeg_->HardwareDecoding(); }
    bool HardwareReady() const override { return hardwareReady_; }
    bool ConfigureVideo(const std::wstring& shaderPath, bool forceSdr) override;
    VideoRenderInfo RenderInfo() const override;
    bool RecoverVideo(bool softwareGpu) override;
    bool RenderFailed() const override {return rendererFailed_.load();}

    // Called from the decode thread when a frame has been decoded (for repaint).
    void OnFrameReady();

private:
    friend class Player;
#ifdef LUMEN_TEST
    friend struct PlaybackTestAccess;
#endif
    bool EnsureAudio();
    bool SetupReader(bool advancedProcessing = false, bool audioOnly = false);
    bool SetupCompatibility(std::unique_ptr<FfmpegReader> prepared = {});
    void InitializeRenderer();
    std::unique_ptr<VideoRenderer> ReleaseRenderer();
    void AdoptRenderer(std::unique_ptr<VideoRenderer> renderer);
    bool UpdateCompatibilityVideo();
    bool UpdateVideoFormat();
    bool SetAudioFormat();
    void StartThreads();
    void StopThreads();
    void ApplySeek(double sec, uint64_t request);
    void VideoLoop();
    void AudioLoop();
    void HandleVideo(IMFSample* sample, LONGLONG ts, uint64_t generation);
    bool HandleAudio(IMFSample* sample, LONGLONG ts, uint64_t generation);
    int KindOf(DWORD idx);
    double PosLocked() const;
    bool EnsureFrame(int w, int h);
    void PaintFrame();

    struct AudioChunk { std::vector<BYTE> data; UINT32 frames = 0, offset = 0; double pts = 0; uint64_t generation = 0; };
    struct BgraFrame { std::vector<BYTE> bits; int w = 0, h = 0, stride = 0; double pts = 0, aspect = 1; bool valid = false; };

    HWND wnd_ = nullptr;
    std::wstring path_;
    Com<IMFSourceReader> reader_;
    std::unique_ptr<FfmpegReader> ffmpeg_;
    bool forceCompatibility_ = false;
    bool preferHardware_ = false;
    bool hardwareReady_ = false;
    bool softwareGpu_ = false;
    bool deferRenderer_ = false;
    std::atomic<bool> rendererFailed_{false};
    std::unique_ptr<VideoRenderer> renderer_;
    std::wstring shaderPath_;
    bool forceSdr_ = false;
    std::atomic<bool> playing_{false};
    std::atomic<bool> closing_{false};
    std::atomic<bool> eof_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> preview_{false};
    std::atomic<double> discardBefore_{0};
    std::atomic<double> rate_{1.0};
    std::atomic<uint64_t> seekGen_{0};
    std::mutex seekM_;
    std::atomic<double> previewTarget_{-1};
    std::atomic<uint64_t> previewRequest_{0};
    std::atomic<uint64_t> previewApplied_{0};
    std::atomic<double> seekTarget_{0};
    float volume_ = 1.0f;
    bool muted_ = false;

    mutable std::mutex clockM_;
    mutable double base_ = 0;
    mutable double qpc_ = 0;

    std::mutex ioM_;
    std::mutex audioM_;
    unsigned selectedStreams_ = 0;
    std::map<DWORD, int> kindMap_;

    mutable std::mutex frameM_;
    BgraFrame frame_;

    std::mutex aqM_;
    std::condition_variable aqCv_;
    std::deque<AudioChunk> aq_;

    Com<IAudioClient> ac_;
    Com<IAudioRenderClient> arc_;
    Com<ISimpleAudioVolume> avol_;
    HANDLE audioEvent_ = nullptr;
    bool audioEventDriven_ = false;
    WAVEFORMATEX* mix_ = nullptr;    // endpoint mix format (informational / fallback)
    WAVEFORMATEX have_ = {};         // format actually negotiated with the engine
    DWORD channelMask_ = 0;
    UINT32 mixFrames_ = 0;
    int mixRate_ = 48000;
    int mixChannels_ = 2;
    int mixBits_ = 32;
    bool mixFloat_ = true;
    bool audioOk_ = false;
    bool audioDeviceUnavailable_ = false;
    bool hasAudio_ = false;
    std::atomic<bool> audioLost_{false};

    std::atomic<double> audioEnd_{0};
    std::atomic<double> audioPos_{0};

    int vw_ = 0, vh_ = 0;
    double fps_ = 30.0, duration_ = 0;
    bool rgbOut_ = true;
    int codedWidth_ = 0, codedHeight_ = 0, cropX_ = 0, cropY_ = 0;
    LONG videoStride_ = 0;
    bool bt709_ = true, fullRange_ = false;
    double displayAspect_ = 1;

    std::thread vthread_, athread_;
    std::mutex renderedTapM_;
    std::function<void(const float*,int,int,int,double)> renderedTap_;
    std::vector<float> renderedPcm_;
    bool renderedFinished_=false;
};

class Player {
public:
    ~Player();

    void SetWindows(HWND hwVideo, HWND swVideo);
    bool Open(const std::wstring& path, bool preferHardware, bool autoplay = true, std::unique_ptr<FfmpegReader> prepared = {});
    void Close();
    bool SwitchToSoftware();
    bool SwitchDecoder(bool preferHardware);
    bool NeedsFallback();
    bool NeedsAudioRecovery();
    static bool ModernCoreAvailable();
    void UseModernCore(bool enabled) { modernCore_ = enabled; }
    bool ModernCoreEnabled() const { return modernCore_; }
    bool CoreActive() const;
    bool Loading() const;
    bool CancelLoading();
    CoreStatus CoreInfo() const;
    const CoreOptions& PlaybackOptions() const { return coreOptions_; }
    void ConfigurePlayback(const CoreOptions& options);
    bool SelectAudioTrack(int64_t id);
    bool SelectSubtitleTrack(int64_t id);
    bool AddSubtitle(const std::wstring& path);
    void SubtitlePresentation(bool visible, double scale);
    bool SetABLoop(double a, double b);
    bool FrameStep(int frames);
    double Rate() const { return rate_.load(); }

    void Play();
    void Pause();
    void HoldPlayback(bool hold);
    bool PlaybackHeld() const { return playbackHeld_; }
    bool PlaybackRequested() const { return backend_ && playRequested_; }
    bool WantsToPlay() const { return backend_ && playRequested_ && (!backend_->IsEof() || !backend_->SeekReady()); }
    void TogglePlay();
    void Seek(double sec);
    void PreviewSeek(double sec);
    bool SeekReady() const { return backend_ && backend_->SeekReady(); }
    void SetScrubbing(bool enabled);
    void SeekRelative(double d);
    void SetRate(double r) { SetRateEx(r); }
    bool SetRateEx(double r) { if (!backend_ || !std::isfinite(r) || r<0.1 || r>12 || (Live() && r!=1) || !backend_->SetRate(r)) return false; rate_.store(r); return true; }
    double Position() const { return backend_ ? backend_->Position() : 0; }
    double Duration() const { return backend_ ? backend_->Duration() : 0; }
    bool IsPlaying() const { return backend_ ? backend_->IsPlaying() : false; }
    bool IsEof() const { return backend_ ? backend_->IsEof() : false; }
    bool IsHardware() const { return backend_ && backend_->HardwareDecoding(); }
    bool UsesSystemWindow() const { return useHw_; }
    bool HardwarePreferred() const { return preferHardware_; }
    bool ConfigureVideo(const std::wstring& shaderPath, bool forceSdr) {
        if(backend_ && HasVideo() && !backend_->ConfigureVideo(shaderPath,forceSdr))return false;
        shaderPath_=shaderPath;forceSdr_=forceSdr;
        coreOptions_.shaderPath=shaderPath;coreOptions_.forceSdr=forceSdr;return true;
    }
    VideoRenderInfo RenderInfo() const {return backend_?backend_->RenderInfo():VideoRenderInfo{};}
    bool IsCompatibility() const { return backend_ && backend_->IsCompatibility(); }
    bool Live() const { return backend_ && backend_->Live(); }
    bool CanSeek() const { return backend_ && !Live() && Duration()>0; }
    bool HwAvailable() const { return hwAvailable_; }
    bool HasVideo() const { return CoreActive() ? Width()>0 && Height()>0 : hasVideo_; }
    bool HasAudio() const { return backend_ && backend_->HasAudio(); }
    bool IsOpen() const { return backend_ != nullptr; }
    bool Failed() const { return backend_ && backend_->Failed(); }
    // Software backend could not create an audio stream (e.g. the OS audio engine
    // is unavailable). Video still plays, just silently.
    bool AudioUnavailable() const { return audioFailed_ || (backend_ && backend_->AudioLost()); }
    int Width() const { return backend_ ? backend_->Width() : 0; }
    int Height() const { return backend_ ? backend_->Height() : 0; }
    void SetVolume(float v);
    void SetMuted(bool m);
    void Repaint() { if (backend_) backend_->Repaint(); }

    // Caption audio tap (independent audio-only SourceReader). Set before StartTap.
    void SetAudioSink(std::function<void(const float*, int, int, int, double)> f,
                      std::function<void(double)> finished = {}, std::function<void(double)> reset = {}) {
        audioSink_ = std::move(f); audioFinished_ = std::move(finished); audioReset_ = std::move(reset);
    }
    void StartTap();
    void StopTap();
    bool TapRunning() const { return tapRunning_.load(); }
    bool TapFailed() const { return tapFailed_.load(); }

private:
#ifdef LUMEN_TEST
    friend struct PlaybackTestAccess;
#endif
    void TapLoop();
    bool ReplaceBackend(bool hardware, bool recovery);
    bool EnsureTap();
    bool EnsureNativeTap();
    bool EnsureCompatibilityTap();

    HWND hwWnd_ = nullptr, swWnd_ = nullptr;
    std::unique_ptr<Backend> backend_;
    CoreOptions coreOptions_;
#ifdef LUMEN_TEST
    bool modernCore_ = false; // Legacy regression coverage is kept explicit.
#else
    bool modernCore_ = true;
#endif
    bool useHw_ = false;
    bool preferHardware_ = false;
    std::wstring shaderPath_;
    bool forceSdr_ = false;
    bool hwAvailable_ = false;
    bool hwFailed_ = false;
    unsigned videoRecoveries_ = 0;
    bool hasVideo_ = true;
    bool audioFailed_ = false;
    bool playbackHeld_ = false, playRequested_ = false;
    std::wstring audioDeviceId_;
    double nextAudioCheck_ = 0;
    double nextAudioRetry_ = 0;
    unsigned audioRetries_ = 0;
    std::atomic<double> rate_{1.0};
    float volume_ = 1.0f;
    bool muted_ = false;
    std::wstring path_;

    std::function<void(const float*, int, int, int, double)> audioSink_;
    std::function<void(double)> audioFinished_, audioReset_;
    std::atomic<bool> tapRunning_{false};
    std::atomic<bool> tapFailed_{false};
    std::atomic<bool> scrubbing_{false};
    std::thread tapThread_;
    std::mutex tapMutex_;
    Com<IMFSourceReader> tapReader_;
    std::unique_ptr<FfmpegReader> tapCompatibility_;
    int tapRate_ = 16000, tapChannels_ = 1;
    std::atomic<double> tapPos_{0};
    std::atomic<uint64_t> tapSeekGen_{0};
};
