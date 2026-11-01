#include "player.h"
#include "playlist.h"
#include "mpv_backend.h"
#include <shlwapi.h>
#include <mferror.h>
#include <cstring>
#include <mmreg.h>
#include <exception>

extern "C" HRESULT WINAPI MFCreateMFByteStreamOnStream(IStream* pStream, IMFByteStream** ppByteStream);

// Microsoft mfapi.h attribute, missing from the bundled MinGW SDK.
static const GUID kVideoRotation = {0xc380465d, 0x2271, 0x428c, {0x9b, 0x83, 0xec, 0xea, 0x3b, 0x4a, 0x85, 0xc1}};

static bool MakeByteStream(const std::wstring& path, IMFByteStream** out) {
    Com<IStream> stream;
    HRESULT hr = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
        FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, stream.put());
    if (FAILED(hr)) { LogMsg("SHCreateStreamOnFileEx 0x%08X", hr); return false; }
    hr = MFCreateMFByteStreamOnStream(stream.get(), out);
    if (FAILED(hr)) { LogMsg("MFCreateMFByteStreamOnStream 0x%08X", hr); return false; }
    Com<IMFAttributes> attributes;
    if (SUCCEEDED((*out)->QueryInterface(__uuidof(IMFAttributes), (void**)attributes.put())))
        attributes->SetString(MF_BYTESTREAM_ORIGIN_NAME, path.c_str());
    return true;
}

static bool SetReaderTime(IMFSourceReader* reader, double sec) {
    if (!reader) return false;
    PROPVARIANT pv = {};
    pv.vt = VT_I8;
    pv.hVal.QuadPart = (LONGLONG)((sec < 0 ? 0 : sec) * 10000000.0);
    return SUCCEEDED(reader->SetCurrentPosition(GUID_NULL, pv));
}

static HRESULT SetPcmType(IMFSourceReader* reader, const WAVEFORMATEX& format, DWORD channelMask = 0) {
    Com<IMFMediaType> type;
    HRESULT hr = MFCreateMediaType(type.put());
    if (SUCCEEDED(hr)) hr = MFInitMediaTypeFromWaveFormatEx(type.get(), &format, sizeof(format));
    if(SUCCEEDED(hr) && channelMask)hr=type->SetUINT32(MF_MT_AUDIO_CHANNEL_MASK,channelMask);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type.get());
    return hr;
}

static bool NativeAudioContainer(const std::wstring& path) {
    // Native sources can silently omit unknown video and expose only audio.
    // Keep the fast path for unambiguous audio containers; independently
    // inspect all tracks in other containers through the compatibility reader.
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return false;
    char header[12]{};
    const size_t bytes = fread(header, 1, sizeof(header), file);
    fclose(file);
    return bytes >= 12 && ((!memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WAVE", 4)) ||
        !memcmp(header, "fLaC", 4) || (!memcmp(header, "FORM", 4) &&
        (!memcmp(header + 8, "AIFF", 4) || !memcmp(header + 8, "AIFC", 4))));
}

static void Nv12ToBgra(const BYTE* yp, int ys, const BYTE* uvp, int uvs,
    BYTE* dst, int dstStride, int w, int h, bool bt709, bool fullRange) {
    const int scale = fullRange ? 256 : 298;
    const int red = fullRange ? (bt709 ? 403 : 359) : (bt709 ? 459 : 409);
    const int greenU = fullRange ? (bt709 ? 48 : 88) : (bt709 ? 55 : 100);
    const int greenV = fullRange ? (bt709 ? 120 : 183) : (bt709 ? 136 : 208);
    const int blue = fullRange ? (bt709 ? 475 : 454) : (bt709 ? 541 : 516);
    auto byte = [](int v) { return (BYTE)std::max(0, std::min(255, (v + 128) >> 8)); };
    for (int y = 0; y < h; ++y) {
        const BYTE* yr = yp + (size_t)y * ys;
        const BYTE* ur = uvp + (size_t)(y / 2) * uvs;
        BYTE* dr = dst + (size_t)y * dstStride;
        for (int x = 0; x < w; ++x) {
            const int Y = (yr[x] - (fullRange ? 0 : 16)) * scale;
            const int U = ur[(x / 2) * 2] - 128;
            const int V = ur[(x / 2) * 2 + 1] - 128;
            dr[x * 4 + 0] = byte(Y + blue * U);
            dr[x * 4 + 1] = byte(Y - greenU * U - greenV * V);
            dr[x * 4 + 2] = byte(Y + red * V);
            dr[x * 4 + 3] = 255;
        }
    }
}

// ===========================================================================
// HwBackend (MFPlay + EVR, the classic Windows pipeline)
// ===========================================================================
struct HwBackend::Callback final : IMFPMediaPlayerCallback {
    HwBackend* owner = nullptr;
    std::mutex mutex;
    LONG ref = 1;
    STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&ref); }
    STDMETHOD_(ULONG, Release)() override {
        LONG r = InterlockedDecrement(&ref);
        if (r == 0) delete this;
        return r;
    }
    STDMETHOD(QueryInterface)(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFPMediaPlayerCallback)) {
            *ppv = static_cast<IMFPMediaPlayerCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(void, OnMediaPlayerEvent)(MFP_EVENT_HEADER* h) override {
        std::lock_guard<std::mutex> lock(mutex);
        if (owner && h) owner->OnEvent((int)h->eEventType, h->hrEvent);
    }
};

HwBackend::HwBackend(HWND wnd) : wnd_(wnd) {}

HwBackend::~HwBackend() {
    Shutdown();
}

bool HwBackend::Init() {
    if (initialized_) return player_.get() != nullptr;
    initialized_ = true;
    cb_ = new Callback();
    cb_->owner = this;
    HRESULT hr = MFPCreateMediaPlayer(nullptr, FALSE, MFP_OPTION_FREE_THREADED_CALLBACK, cb_, wnd_, player_.put());
    LogMsg("MFPCreateMediaPlayer hr=0x%08X", hr);
    if (FAILED(hr) || !player_) {
        if (cb_) { cb_->owner = nullptr; cb_->Release(); cb_ = nullptr; }
        return false;
    }
    return true;
}

void HwBackend::Shutdown() {
    if (cb_) { std::lock_guard<std::mutex> lock(cb_->mutex); cb_->owner = nullptr; }
    if (player_) {
        player_->Shutdown();
        player_.reset();
    }
    item_.reset();
    if (cb_) { cb_->Release(); cb_ = nullptr; }
    initialized_ = false;
}

bool HwBackend::Open(const std::wstring& path, bool autoplay) {
    if (!Init()) return false;
    failed_ = false;
    wantPlaying_.store(false);
    eof_.store(false);
    duration_ = 0;
    vw_ = vh_ = 0;
    player_->Stop();
    player_->ClearMediaItem();
    item_.reset();

    Com<IMFByteStream> bs;
    if (!MakeByteStream(path, bs.put())) return false;
    HRESULT hr = player_->CreateMediaItemFromObject(bs.get(), TRUE, 0, item_.put());
    if (FAILED(hr) || !item_) { LogMsg("MFPlay CreateMediaItem 0x%08X", hr); failed_ = true; return false; }
    BOOL video = FALSE, selected = FALSE, audio = FALSE;
    item_->HasVideo(&video, &selected);
    if (!video || !selected) return false;
    item_->HasAudio(&audio, &selected);
    hasAudio_ = audio && selected;
    DWORD streamCount = 0;
    item_->GetNumberOfStreams(&streamCount);
    for (DWORD i = 0; i < std::min<DWORD>(streamCount, 64); ++i) {
        BOOL enabled = FALSE;
        if (FAILED(item_->GetStreamSelection(i, &enabled)) || !enabled) continue;
        PROPVARIANT subtype{}, profile{};
        const bool h264 = SUCCEEDED(item_->GetStreamAttribute(i, MF_MT_SUBTYPE, &subtype)) &&
            subtype.vt == VT_CLSID && subtype.puuid &&
            (*subtype.puuid == MFVideoFormat_H264 || *subtype.puuid == MFVideoFormat_H264_ES);
        PropVariantClear(&subtype);
        if (!h264) continue;
        const HRESULT queried = item_->GetStreamAttribute(i, MF_MT_MPEG2_PROFILE, &profile);
        const ULONG value = SUCCEEDED(queried) && profile.vt == VT_UI4 ? profile.ulVal : 0;
        PropVariantClear(&profile);
        // The Windows H.264 decoder supports Baseline/Main/High (4:2:0).
        // High 10/4:2:2/4:4:4 can prepare a topology yet never drain, and its
        // subsequent Shutdown can block inside msmpeg2vdec. Avoid activating it.
        LogMsg("MFPlay H264 profile=%lu query=0x%08X", value, queried);
        if (value && value != 66 && value != 77 && value != 100) {
            LogMsg("H264 profile requires software compatibility decoding");
            return false;
        }
    }
    { std::lock_guard<std::mutex> lock(eventM_); itemReady_ = false; }
    hr = player_->SetMediaItem(item_.get());
    if (FAILED(hr)) { LogMsg("MFPlay SetMediaItem 0x%08X", hr); failed_ = true; return false; }

    PROPVARIANT pv = {};
    if (SUCCEEDED(item_->GetDuration(GUID_NULL, &pv))) {
        if (pv.vt == VT_I8) duration_ = (double)pv.hVal.QuadPart / 10000000.0;
        else if (pv.vt == VT_UI8) duration_ = (double)pv.uhVal.QuadPart / 10000000.0;
    }
    // Topology construction is asynchronous. Preparing it must not emit audio
    // or advance the timeline just to discover the video's dimensions.
    {
        std::unique_lock<std::mutex> lock(eventM_);
        if (!eventCv_.wait_for(lock, std::chrono::seconds(3), [&] { return itemReady_ || failed_.load(); }) || failed_.load()) {
            LogMsg("MFPlay topology preparation failed or timed out");
            return false;
        }
    }
    SIZE native = {}, ar = {};
    if (SUCCEEDED(player_->GetNativeVideoSize(&native, &ar))) { vw_ = native.cx; vh_ = native.cy; }
    if (duration_ <= 0) {
        PROPVARIANT pd = {};
        if (SUCCEEDED(player_->GetDuration(GUID_NULL, &pd))) {
            if (pd.vt == VT_I8) duration_ = (double)pd.hVal.QuadPart / 10000000.0;
            else if (pd.vt == VT_UI8) duration_ = (double)pd.uhVal.QuadPart / 10000000.0;
        }
    }
    player_->SetVolume(muted_ ? 0.0f : volume_);
    player_->SetMute(muted_ ? TRUE : FALSE);
    if (vw_ <= 0 || vh_ <= 0) {
        LogMsg("MFPlay never reported a video size, handing over to software");
        failed_ = true;
        return false;
    }
    float slow = 1, fast = 1;
    player_->GetSupportedRates(TRUE, &slow, &fast);
    LogMsg("MFPlay open: %dx%d %.2fs fastRate=%.1f", vw_, vh_, duration_, fast);
    if (autoplay) Play();
    else Pause();
    return true;
}

void HwBackend::Close() {
    wantPlaying_.store(false);
    { std::lock_guard<std::mutex> lock(seekM_); seekInFlight_ = false; seekPending_.store(false); }
    if (player_) {
        player_->Stop();
        player_->ClearMediaItem();
    }
    item_.reset();
    eof_.store(false);
    failed_ = false;
    vw_ = vh_ = 0;
    duration_ = 0;
    hasAudio_ = false;
}

void HwBackend::Play() {
    if (!player_) return;
    wantPlaying_.store(true);
    if (eof_.load() && !seekPending_.load()) { Seek(0); return; }
    if (!seekPending_.load()) player_->Play();
}

void HwBackend::Pause() {
    wantPlaying_.store(false);
    if (!player_ || seekPending_.load()) return;
    MFP_MEDIAPLAYER_STATE state = MFP_MEDIAPLAYER_STATE_EMPTY;
    if (SUCCEEDED(player_->GetState(&state)) && state == MFP_MEDIAPLAYER_STATE_PLAYING) player_->Pause();
}

void HwBackend::Seek(double sec) {
    if (!player_ || !std::isfinite(sec)) return;
    sec = Clampd(sec, 0, std::max(0.0, duration_ - 0.01));
    std::lock_guard<std::mutex> lock(seekM_);
    seekTarget_.store(sec);
    ++seekRequest_;
    seekPending_.store(true);
    eof_.store(false);
    if (!seekInFlight_) SubmitSeekLocked();
}

void HwBackend::SubmitSeekLocked() {
    PROPVARIANT pv = {};
    pv.vt = VT_I8;
    pv.hVal.QuadPart = (LONGLONG)(seekTarget_.load() * 10000000.0);
    seekSubmitted_ = seekRequest_;
    seekInFlight_ = true;
    const HRESULT hr = player_->SetPosition(GUID_NULL, &pv);
    if (FAILED(hr)) {
        LogMsg("MFPlay seek failed 0x%08X", hr);
        seekInFlight_ = false;
        failed_.store(true); // Retain the requested position for software recovery.
    }
}

bool HwBackend::SetRate(double r) {
    if (!player_ || !std::isfinite(r) || r < 1 || r > 8) return false;
    if (r <= 1.001) return SUCCEEDED(player_->SetRate(1.0f));
    HRESULT hr = player_->SetRate((float)r);
    if (FAILED(hr)) {
        LogMsg("MFPlay SetRate(%.1f) 0x%08X", r, hr);
        return false;
    }
    return true;
}

double HwBackend::Position() const {
    if (!player_) return 0;
    if (seekPending_.load()) return seekTarget_.load();
    PROPVARIANT pv = {};
    if (SUCCEEDED(player_->GetPosition(GUID_NULL, &pv))) {
        double v = 0;
        if (pv.vt == VT_I8) v = (double)pv.hVal.QuadPart / 10000000.0;
        else if (pv.vt == VT_UI8) v = (double)pv.uhVal.QuadPart / 10000000.0;
        return v;
    }
    return 0;
}

double HwBackend::Duration() const { return duration_; }

bool HwBackend::IsPlaying() const {
    if (!player_ || eof_.load()) return false;
    MFP_MEDIAPLAYER_STATE st = MFP_MEDIAPLAYER_STATE_EMPTY;
    return SUCCEEDED(player_->GetState(&st)) && st == MFP_MEDIAPLAYER_STATE_PLAYING;
}

bool HwBackend::IsEof() const { return eof_.load(); }

void HwBackend::SetVolume(float v) {
    volume_ = Clampf(v, 0, 1);
    if (player_) player_->SetVolume(muted_ ? 0.0f : volume_);
}

void HwBackend::SetMuted(bool m) {
    muted_ = m;
    if (player_) player_->SetMute(m ? TRUE : FALSE);
    if (player_) player_->SetVolume(m ? 0.0f : volume_);
}

void HwBackend::Repaint() { if (player_) player_->UpdateVideo(); }

void HwBackend::OnEvent(int type, HRESULT hr) {
    // A stopped/prepared session is already held; an asynchronous pause racing
    // that state is not a decoder failure.
    if (hr == MF_E_SESSION_PAUSEWHILESTOPPED) return;
    if (type == MFP_EVENT_TYPE_ERROR || FAILED(hr)) {
        LogMsg("MFPlay error event 0x%08X", hr);
        failed_ = true;
        eventCv_.notify_all();
    } else if (type == MFP_EVENT_TYPE_PLAY && !wantPlaying_.load()) {
        Pause();
    } else if (type == MFP_EVENT_TYPE_PAUSE && wantPlaying_.load() && !seekPending_.load()) {
        player_->Play();
    } else if (type == MFP_EVENT_TYPE_MEDIAITEM_SET) {
        { std::lock_guard<std::mutex> lock(eventM_); itemReady_ = true; }
        eventCv_.notify_all();
    } else if (type == MFP_EVENT_TYPE_PLAYBACK_ENDED) {
        if (!seekPending_.load()) eof_.store(true);
    } else if (type == MFP_EVENT_TYPE_POSITION_SET) {
        std::lock_guard<std::mutex> lock(seekM_);
        if (!seekInFlight_) return;
        if (seekSubmitted_ != seekRequest_) { SubmitSeekLocked(); return; }
        seekInFlight_ = false;
        seekPending_.store(false);
        eof_.store(false);
        if (wantPlaying_.load()) player_->Play();
        else Pause();
    }
}

// ===========================================================================
// SwBackend (FFmpeg / SourceReader + WASAPI + GDI)
// ===========================================================================
SwBackend::SwBackend(HWND wnd, bool compatibility, bool hardware, bool softwareGpu) : wnd_(wnd), forceCompatibility_(compatibility), preferHardware_(hardware), softwareGpu_(softwareGpu) {}

SwBackend::~SwBackend() {
    Close();
    if (mix_) { CoTaskMemFree(mix_); mix_ = nullptr; }
}

bool SwBackend::EnsureAudio() {
    if (audioOk_) return true;
    avol_.reset(); arc_.reset(); ac_.reset();
    if (mix_) { CoTaskMemFree(mix_); mix_ = nullptr; }
    if (audioEvent_) { CloseHandle(audioEvent_); audioEvent_ = nullptr; }
    audioEventDriven_ = false;
    Com<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)enumerator.put());
    if (FAILED(hr)) { LogMsg("MMDeviceEnumerator 0x%08X", hr); return false; }
    Com<IMMDevice> dev;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, dev.put());
    if (FAILED(hr)) { LogMsg("GetDefaultAudioEndpoint 0x%08X", hr); return false; }
    hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)ac_.put());
    if (FAILED(hr)) { LogMsg("Activate(IAudioClient) 0x%08X", hr); return false; }
    if (FAILED(ac_->GetMixFormat(&mix_)) || !mix_) return false;
    if (mix_)
        LogMsg("endpoint mix: %u Hz %u ch %u bits tag=%u", mix_->nSamplesPerSec,
            mix_->nChannels, mix_->wBitsPerSample, mix_->wFormatTag);

    audioEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    auto initialize = [&](const WAVEFORMATEX* format, DWORD flags) {
        // A failed Initialize may leave the client unusable. Each retry owns a
        // fresh connection; event-driven output has a polling compatibility fallback.
        for (bool events : {true, false}) {
            ac_.reset();
            if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)ac_.put()))) return false;
            const bool useEvent = events && audioEvent_;
            hr = ac_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags | (useEvent ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0), 0, 0, format, nullptr);
            if (SUCCEEDED(hr) && (!useEvent || SUCCEEDED(hr = ac_->SetEventHandle(audioEvent_)))) {
                audioEventDriven_ = useEvent;
                have_ = *format;
                channelMask_=0;
                if(format->wFormatTag==WAVE_FORMAT_EXTENSIBLE && format->cbSize>=22) {
                    const auto* ext=(const WAVEFORMATEXTENSIBLE*)format;channelMask_=ext->dwChannelMask;
                    if(ext->SubFormat==MFAudioFormat_Float)have_.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;
                    else if(ext->SubFormat==MFAudioFormat_PCM)have_.wFormatTag=WAVE_FORMAT_PCM;
                }
                return true;
            }
            LogMsg("WASAPI format %u/%u/%u event=%d failed 0x%08X", format->nSamplesPerSec, format->nChannels, format->wBitsPerSample, (int)useEvent, hr);
        }
        return false;
    };
    // Preserve the endpoint's real speaker layout. Stereo endpoints receive a
    // proper downmix; 5.1/7.1 endpoints must not be flattened to stereo first.
    WAVEFORMATEXTENSIBLE surround{};
    surround.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;surround.Format.cbSize=22;
    surround.Format.nChannels=(WORD)std::max(1,std::min(8,(int)mix_->nChannels));
    surround.Format.nSamplesPerSec=std::max(8000ul,std::min(192000ul,mix_->nSamplesPerSec));
    surround.Format.wBitsPerSample=32;surround.Samples.wValidBitsPerSample=32;
    surround.Format.nBlockAlign=surround.Format.nChannels*4;
    surround.Format.nAvgBytesPerSec=surround.Format.nSamplesPerSec*surround.Format.nBlockAlign;
    surround.SubFormat=MFAudioFormat_Float;
    if(mix_->wFormatTag==WAVE_FORMAT_EXTENSIBLE && mix_->cbSize>=22)surround.dwChannelMask=((const WAVEFORMATEXTENSIBLE*)mix_)->dwChannelMask;
    if(!surround.dwChannelMask)surround.dwChannelMask=surround.Format.nChannels==1?SPEAKER_FRONT_CENTER:
        surround.Format.nChannels==2?(SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT):0;
    WAVEFORMATEX want{};
    want.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    want.nChannels = 2; want.nSamplesPerSec = 48000; want.wBitsPerSample = 32;
    want.nBlockAlign = 8; want.nAvgBytesPerSec = 384000;
    const DWORD convert = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    bool initialized = surround.dwChannelMask && initialize(&surround.Format,convert);
    if(!initialized)initialized=initialize(&want, convert);
    if (!initialized) {
        want.wFormatTag = WAVE_FORMAT_PCM; want.wBitsPerSample = 16;
        want.nBlockAlign = 4; want.nAvgBytesPerSec = 192000;
        initialized = initialize(&want, convert);
    }
    if (!initialized) {
        WORD tag = mix_->wFormatTag;
        if (tag == WAVE_FORMAT_EXTENSIBLE && mix_->cbSize >= 22) {
            const auto* ext = (const WAVEFORMATEXTENSIBLE*)mix_;
            if (ext->SubFormat == MFAudioFormat_Float) tag = WAVE_FORMAT_IEEE_FLOAT;
            else if (ext->SubFormat == MFAudioFormat_PCM) tag = WAVE_FORMAT_PCM;
        }
        if ((tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_IEEE_FLOAT) ||
            mix_->nChannels < 1 || mix_->nChannels > 8 || mix_->nSamplesPerSec < 8000 || mix_->nSamplesPerSec > 192000 ||
            (tag == WAVE_FORMAT_IEEE_FLOAT && mix_->wBitsPerSample != 32) ||
            (tag == WAVE_FORMAT_PCM && mix_->wBitsPerSample != 8 && mix_->wBitsPerSample != 16 && mix_->wBitsPerSample != 24 && mix_->wBitsPerSample != 32) ||
            !initialize(mix_, 0)) return false;
        have_.wFormatTag = tag;
    }
    have_.cbSize = 0;

    mixRate_ = have_.nSamplesPerSec;
    mixChannels_ = have_.nChannels;
    mixBits_ = have_.wBitsPerSample;
    mixFloat_ = have_.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;

    hr = ac_->GetBufferSize(&mixFrames_);
    if (FAILED(hr)) { LogMsg("GetBufferSize 0x%08X", hr); return false; }
    hr = ac_->GetService(__uuidof(IAudioRenderClient), (void**)arc_.put());
    if (FAILED(hr)) { LogMsg("GetService(render) 0x%08X", hr); return false; }
    ac_->GetService(__uuidof(ISimpleAudioVolume), (void**)avol_.put());
    audioOk_ = true;
    LogMsg("WASAPI ok: %d Hz %d ch %d bits float=%d buffer=%u event=%d", mixRate_, mixChannels_,
        mixBits_, (int)mixFloat_, mixFrames_, (int)audioEventDriven_);
    return true;
}

bool SwBackend::SetupReader(bool advancedProcessing, bool audioOnly) {
    selectedStreams_ = 0;
    Com<IMFAttributes> attrs;
    HRESULT hr = MFCreateAttributes(attrs.put(), 4);
    if (FAILED(hr)) return false;
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, !advancedProcessing);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, advancedProcessing);
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);

    Com<IMFByteStream> bs;
    if (!MakeByteStream(path_, bs.put())) return false;
    hr = MFCreateSourceReaderFromByteStream(bs.get(), attrs.get(), reader_.put());
    if (FAILED(hr)) { LogMsg("MFCreateSourceReaderFromByteStream 0x%08X", hr); return false; }

    reader_->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);

    // Audio-only files (wav/mp3/flac/...) have no video stream at all: play them
    // through the audio path and leave the video surface blank.
    Com<IMFMediaType> native;
    bool hasVideo = SUCCEEDED(reader_->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, native.put())) && native;
    if (hasVideo && audioOnly) return false;
    if (hasVideo) {
        if (MFGetAttributeUINT32(native.get(), kVideoRotation, 0) != 0) return false;
        if (FAILED(reader_->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE))) return false;
        selectedStreams_ |= 1;
        bool configured = false;
        for (const GUID format : {MFVideoFormat_RGB32, MFVideoFormat_NV12}) {
            Com<IMFMediaType> out;
            if (FAILED(MFCreateMediaType(out.put()))) return false;
            out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            out->SetGUID(MF_MT_SUBTYPE, format);
            UINT32 num = 0, den = 0;
            if (SUCCEEDED(MFGetAttributeRatio(native.get(), MF_MT_PIXEL_ASPECT_RATIO, &num, &den)) && num && den)
                MFSetAttributeRatio(out.get(), MF_MT_PIXEL_ASPECT_RATIO, num, den);
            hr = reader_->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, out.get());
            if (SUCCEEDED(hr)) { configured = true; break; }
            LogMsg("SW video output format 0x%08lX refused 0x%08X", format.Data1, hr);
        }
        if (!configured) {
            if (advancedProcessing) return false;
            LogMsg("Retrying with the system advanced video processor");
            reader_.reset();
            return SetupReader(true, audioOnly);
        }
    } else {
        vw_ = vh_ = 0;
        fps_ = 30.0;
        rgbOut_ = false;
        LogMsg("SW: audio-only source");
    }

    if (hasVideo && !UpdateVideoFormat()) return false;
    Com<IMFMediaType> audioType;
    hasAudio_ = SUCCEEDED(reader_->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, audioType.put()));
    audioDeviceUnavailable_ = hasAudio_ && !EnsureAudio();
    PROPVARIANT pv = {};
    if (SUCCEEDED(reader_->GetPresentationAttribute((DWORD)MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv))) {
        if (pv.vt == VT_UI8) duration_ = (double)pv.uhVal.QuadPart / 10000000.0;
        else if (pv.vt == VT_I8) duration_ = (double)pv.hVal.QuadPart / 10000000.0;
    }

    if (hasAudio_ && audioOk_) {
        hr = reader_->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
        if (SUCCEEDED(hr)) {
            // The render endpoint advertises its mix format as WAVE_FORMAT_EXTENSIBLE,
            // which the decoder output does not accept (MF_E_INVALIDMEDIATYPE). Ask for
            // the equivalent plain float/PCM type instead - the layout is identical, so
            // the samples we push to WASAPI keep matching mixChannels_/mixBits_.
            if (!SetAudioFormat()) {
                LogMsg("audio output type refused 0x%08X (tag=%u %u Hz %u ch %u bits)",
                    E_FAIL, have_.wFormatTag, have_.nSamplesPerSec, have_.nChannels, have_.wBitsPerSample);
                reader_->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, FALSE);
            } else selectedStreams_ |= 2;
        }
    }
    // An audio-only source with no usable audio stream is unplayable, but that
    // decision belongs to Player::Open so the UI can explain why.
    LogMsg("SW reader %dx%d @%.2f %.1fs rgb=%d audio=%d", vw_, vh_, fps_, duration_,
        (int)rgbOut_, (int)audioOk_);
    return selectedStreams_ != 0;
}

bool SwBackend::SetAudioFormat() {
    const HRESULT hr = SetPcmType(reader_.get(), have_,channelMask_);
    if (FAILED(hr)) LogMsg("SetAudioFormat failed 0x%08X", hr);
    return SUCCEEDED(hr);
}

bool SwBackend::UpdateVideoFormat() {
    Com<IMFMediaType> type;
    if (FAILED(reader_->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, type.put()))) return false;
    // The basic SourceReader converter does not apply display rotation. Let
    // the software compatibility decoder perform this transform explicitly.
    if (MFGetAttributeUINT32(type.get(), kVideoRotation, 0) != 0) return false;
    UINT32 w = 0, h = 0, num = 0, den = 0;
    GUID subtype{};
    if (FAILED(MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &w, &h)) || FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype))) return false;
    if (w < 1 || h < 1 || w > 16384 || h > 16384 || (uint64_t)w * h > 67108864) return false;
    rgbOut_ = subtype == MFVideoFormat_RGB32 || subtype == MFVideoFormat_ARGB32;
    if (!rgbOut_ && (subtype != MFVideoFormat_NV12 || (w & 1) || (h & 1))) return false;
    codedWidth_ = vw_ = (int)w; codedHeight_ = vh_ = (int)h;
    cropX_ = cropY_ = 0;
    MFVideoArea area{}; UINT32 size = 0;
    if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*)&area, sizeof(area), &size)) ||
        SUCCEEDED(type->GetBlob(MF_MT_GEOMETRIC_APERTURE, (UINT8*)&area, sizeof(area), &size))) {
        const int x = area.OffsetX.value, y = area.OffsetY.value;
        if (size != sizeof(area) || x < 0 || y < 0 || area.Area.cx <= 0 || area.Area.cy <= 0 ||
            (int64_t)x + area.Area.cx > w || (int64_t)y + area.Area.cy > h) return false;
        cropX_ = x; cropY_ = y; vw_ = area.Area.cx; vh_ = area.Area.cy;
    }
    if (!EnsureFrame(vw_, vh_)) return false;
    UINT32 stride = 0;
    if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) videoStride_ = (LONG)stride;
    else if (FAILED(MFGetStrideForBitmapInfoHeader(subtype.Data1, w, &videoStride_))) videoStride_ = rgbOut_ ? codedWidth_ * 4 : codedWidth_;
    if (std::llabs((long long)videoStride_) < (long long)codedWidth_ * (rgbOut_ ? 4 : 1) || (!rgbOut_ && (videoStride_ < 0 || (cropX_ & 1) || (cropY_ & 1)))) return false;
    if (SUCCEEDED(MFGetAttributeRatio(type.get(), MF_MT_FRAME_RATE, &num, &den)) && num && den) fps_ = (double)num / den;
    displayAspect_ = (double)vw_ / vh_;
    if (SUCCEEDED(MFGetAttributeRatio(type.get(), MF_MT_PIXEL_ASPECT_RATIO, &num, &den)) && num && den)
        displayAspect_ *= (double)num / den;
    const UINT32 matrix = MFGetAttributeUINT32(type.get(), MF_MT_YUV_MATRIX, vh_ <= 576 ? MFVideoTransferMatrix_BT601 : MFVideoTransferMatrix_BT709);
    bt709_ = matrix != MFVideoTransferMatrix_BT601;
    fullRange_ = MFGetAttributeUINT32(type.get(), MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235) == MFNominalRange_0_255;
    LogMsg("SW output %dx%d visible=%dx%d+%d+%d stride=%ld rgb=%d aspect=%.4f matrix=%u range=%s", codedWidth_, codedHeight_, vw_, vh_, cropX_, cropY_, videoStride_, (int)rgbOut_, displayAspect_, matrix, fullRange_ ? "full" : "limited");
    return true;
}

bool SwBackend::UpdateCompatibilityVideo() {
    const auto video = ffmpeg_->Video();
    codedWidth_ = vw_ = video.width; codedHeight_ = vh_ = video.height;
    cropX_ = cropY_ = 0; rgbOut_ = true;
    videoStride_ = vw_ * 4; displayAspect_ = video.aspect; fps_ = video.fps;
    return !ffmpeg_->HasVideo() || EnsureFrame(vw_, vh_);
}

bool SwBackend::SetupCompatibility(std::unique_ptr<FfmpegReader> prepared) {
    reader_.reset(); kindMap_.clear(); selectedStreams_ = 0;
    auto decoder = prepared ? std::move(prepared) : std::make_unique<FfmpegReader>();
    // Video remains usable when no audio endpoint is available. Audio-only
    // open failure is handled by Player, which can explain the missing output.
    const bool output = EnsureAudio();
    if(decoder->HasVideo() || decoder->HasAudio()) decoder->ConfigureAudio(output?&have_:nullptr);
    else if (!decoder->Open(path_, output ? &have_ : nullptr)) return false;
    hasAudio_ = decoder->HasAudio(); audioDeviceUnavailable_ = hasAudio_ && !output;
    if (decoder->HasVideo()) selectedStreams_ |= 1;
    if (decoder->DecodesAudio()) selectedStreams_ |= 2;
    duration_ = decoder->Duration();
    ffmpeg_ = std::move(decoder);
    if(channelMask_)ffmpeg_->SetOutputChannelMask(channelMask_);
    if(ffmpeg_->HasVideo()) {
        hardwareReady_=preferHardware_ && ffmpeg_->SetHardwareDecoding(true);
        if(!deferRenderer_)InitializeRenderer();
    }
    LogMsg("FFmpeg compatibility pipeline selected hardware_requested=%d",(int)preferHardware_);
    return UpdateCompatibilityVideo() && selectedStreams_;
}

void SwBackend::InitializeRenderer() {
    if(!wnd_ || !ffmpeg_ || !ffmpeg_->HasVideo())return;
    if(!renderer_) {
        renderer_=std::make_unique<VideoRenderer>();
        if(!renderer_->Open(wnd_,softwareGpu_))renderer_.reset();
    }
    if(renderer_) {
        ffmpeg_->SetNativeVideo(true);
        if(!renderer_->Configure(shaderPath_,forceSdr_))LogMsg("Saved GLSL shader unavailable; using default rendering");
        LogMsg("libplacebo D3D11 renderer ready");
    }
}

std::unique_ptr<VideoRenderer> SwBackend::ReleaseRenderer() {
    StopThreads();
    std::lock_guard<std::mutex> lock(frameM_);
    return std::move(renderer_);
}

void SwBackend::AdoptRenderer(std::unique_ptr<VideoRenderer> renderer) {
    if(!ffmpeg_ || !ffmpeg_->HasVideo())return;
    StopThreads();
    renderer_=std::move(renderer);
    InitializeRenderer();
    {std::lock_guard<std::mutex> lock(frameM_);frame_.valid=false;if(renderer_)frame_.bits.clear();}
    if(!UpdateCompatibilityVideo())failed_.store(true);
    StartThreads();
}

bool SwBackend::Open(const std::wstring& path, bool autoplay) {
    return OpenPrepared(path,{},autoplay);
}
bool SwBackend::OpenPrepared(const std::wstring& path, std::unique_ptr<FfmpegReader> prepared, bool autoplay) {
    Close();
    try {
    path_ = path;
    // Keep lightweight native PCM/FLAC audio, but use one consistent software
    // video/container decoder: native TS conversion can fail after a valid open.
    // The system video reader remains available if the optional DLLs are absent.
    const bool ready = prepared ? SetupCompatibility(std::move(prepared)) : forceCompatibility_ ? SetupCompatibility() :
        ((NativeAudioContainer(path) && SetupReader(false, true)) || SetupCompatibility() || SetupReader());
    if (!ready) { Close(); return false; }
    if (vw_ > 0 && !EnsureFrame(vw_, vh_)) { Close(); return false; }
    base_ = 0;
    qpc_ = QpcNow();
    rate_.store(1.0);
    playing_.store(autoplay);
    failed_.store(false);
    preview_.store(!autoplay && (selectedStreams_ & 1));
    discardBefore_.store(0);
    audioEnd_.store(0);
    audioPos_.store(0);
    audioLost_.store(false);
    if (autoplay && (selectedStreams_ & 2) && audioOk_ && ac_) audioLost_.store(FAILED(ac_->Start()));
    StartThreads();
    if (avol_) avol_->SetMasterVolume(muted_ ? 0.0f : volume_, nullptr);
    return true;
    } catch (const std::exception& error) {
        LogMsg("Software playback preparation failed: %s", error.what());
        Close();
        return false;
    }
}

void SwBackend::Close() {
    StopThreads();
    previewTarget_.store(-1);
    previewApplied_.store(previewRequest_.load());
    if (audioOk_ && ac_) { ac_->Stop(); ac_->Reset(); }
    {
        std::lock_guard<std::mutex> lk(aqM_);
        aq_.clear();
    }
    renderer_.reset();
    rendererFailed_.store(false);
    reader_.reset();
    ffmpeg_.reset();
    avol_.reset();
    arc_.reset();
    ac_.reset();
    if (audioEvent_) { CloseHandle(audioEvent_); audioEvent_ = nullptr; }
    audioEventDriven_ = false;
    if (mix_) { CoTaskMemFree(mix_); mix_ = nullptr; }
    have_ = {};
    audioOk_ = false;
    hasAudio_ = false;
    audioLost_.store(false);
    kindMap_.clear();
    playing_.store(false);
    eof_.store(false);
    duration_ = 0;
    {
        std::lock_guard<std::mutex> lk(frameM_);
        frame_.valid = false;
        frame_ = BgraFrame();
    }
}

void SwBackend::StartThreads() {
    if(ffmpeg_) ffmpeg_->Interrupt(false);
    closing_.store(false);
    auto run = [this](void (SwBackend::*loop)()) {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try { (this->*loop)(); }
        catch (const std::exception& error) {
            LogMsg("Software playback worker failed: %s", error.what());
            failed_.store(true); Pause(); preview_.store(false); eof_.store(true);
            aqCv_.notify_all();
        }
        if (SUCCEEDED(com)) CoUninitialize();
    };
    vthread_ = std::thread(run, &SwBackend::VideoLoop);
    if (selectedStreams_ & 2) athread_ = std::thread(run, &SwBackend::AudioLoop);
}

void SwBackend::StopThreads() {
    closing_.store(true);
    if(ffmpeg_) ffmpeg_->Interrupt();
    aqCv_.notify_all();
    if (audioEvent_) SetEvent(audioEvent_);
    if (vthread_.joinable()) vthread_.join();
    if (athread_.joinable()) athread_.join();
    closing_.store(false);
}

double SwBackend::PosLocked() const {
    if (previewApplied_.load() != previewRequest_.load()) return seekTarget_.load();
    if (!playing_.load() || preview_.load()) return base_;
    double r = rate_.load();
    double p = base_ + (QpcNow() - qpc_) * r;
    if (r == 1.0) {
        double ap = audioPos_.load();
        if (audioOk_ && !audioLost_.load() && audioEnd_.load() > ap + 0.005 && ap > 0.01 && std::fabs(ap - p) > 0.12 && std::fabs(ap - p) < 1.5) {
            base_ = ap;
            qpc_ = QpcNow();
            p = ap;
        }
    }
    if (duration_ > 0 && p > duration_) p = duration_;
    return p < 0 ? 0 : p;
}

double SwBackend::Position() const {
    std::lock_guard<std::mutex> lk(clockM_);
    return PosLocked();
}

void SwBackend::Play() {
    if (!reader_ && !ffmpeg_) return;
    if (eof_.load() && SeekReady()) Seek(0);
    {
        std::lock_guard<std::mutex> lk(clockM_);
        base_ = PosLocked();
        qpc_ = QpcNow();
    }
    playing_.store(true);
    if (audioOk_ && ac_ && previewApplied_.load() == previewRequest_.load()) {
        std::lock_guard<std::mutex> lk(audioM_);
        if (FAILED(ac_->Start())) audioLost_.store(true);
    }
    aqCv_.notify_all();
}

void SwBackend::Pause() {
    {
        std::lock_guard<std::mutex> lk(clockM_);
        base_ = PosLocked();
        qpc_ = QpcNow();
        playing_.store(false);
    }
    if (audioOk_ && ac_) {
        std::lock_guard<std::mutex> lk(audioM_);
        ac_->Stop();
    }
    aqCv_.notify_all();
}

void SwBackend::Seek(double sec) {
    if(Live()) return;
    if(IsNetworkMedia(path_) || IsPlaylistFile(path_)) {PreviewSeek(sec);return;}
    if (!std::isfinite(sec)) return;
    sec = Clampd(sec, 0, std::max(0.0, Duration() - 0.05));
    uint64_t request;
    {
        std::lock_guard<std::mutex> lock(seekM_);
        previewTarget_.store(-1);
        seekTarget_.store(sec);
        request = ++previewRequest_;
        ++seekGen_;
    }
    ApplySeek(sec, request);
}

void SwBackend::PreviewSeek(double sec) {
    if(Live()) return;
    if (!std::isfinite(sec)) return;
    sec = Clampd(sec, 0, std::max(0.0, Duration() - 0.05));
    std::lock_guard<std::mutex> lock(seekM_);
    seekTarget_.store(sec);
    ++previewRequest_;
    ++seekGen_; // Cancel an old frame's pacing wait before the next decode iteration.
    previewTarget_.store(sec);
    aqCv_.notify_all();
}

void SwBackend::ApplySeek(double sec, uint64_t request) {
    if ((!reader_ && !ffmpeg_) || !std::isfinite(sec)) return;
    const double duration = Duration();
    if (duration > 0) sec = Clampd(sec, 0, std::max(0.0, duration - 0.05));
    else sec = std::max(0.0, sec);
    std::lock_guard<std::mutex> io(ioM_);
    if (request != previewRequest_.load()) return;
    {
        discardBefore_.store(sec);
        bool positioned;
        if (ffmpeg_) positioned = ffmpeg_->Seek(sec);
        else {
            if (selectedStreams_ & 1) reader_->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
            if (selectedStreams_ & 2) reader_->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
            reader_->Flush(MF_SOURCE_READER_ALL_STREAMS);
            positioned = SetReaderTime(reader_.get(), sec);
        }
        if (!positioned) { LogMsg("SW seek failed"); failed_.store(true); return; }
        { std::lock_guard<std::mutex> ql(aqM_); aq_.clear(); }
    }
    {
        std::lock_guard<std::mutex> lk(clockM_);
        base_ = sec;
        qpc_ = QpcNow();
        preview_.store(true);
    }
    eof_.store(false);
    if (audioOk_ && ac_) {
        std::lock_guard<std::mutex> lk(audioM_);
        ac_->Stop();
        ac_->Reset();
        if (playing_.load()) ac_->Start();
        audioEnd_.store(sec);
        audioPos_.store(sec);
    }
    previewApplied_.store(request);
    aqCv_.notify_all();
}

bool SwBackend::SetRate(double r) {
    // Software playback has no pitch-preserving time stretcher. Let the UI use
    // its explicit scrub fallback instead of silently accepting a muted rate.
    if (!std::isfinite(r) || std::fabs(r - 1.0) > 0.001) return false;
    std::lock_guard<std::mutex> lk(clockM_);
    base_ = PosLocked();
    qpc_ = QpcNow();
    rate_.store(r);
    return true;
}

void SwBackend::SetVolume(float v) {
    volume_ = Clampf(v, 0, 1);
    std::lock_guard<std::mutex> lk(audioM_);
    if (avol_) avol_->SetMasterVolume(muted_ ? 0.0f : volume_, nullptr);
}

void SwBackend::SetMuted(bool m) {
    muted_ = m;
    std::lock_guard<std::mutex> lk(audioM_);
    if (avol_) avol_->SetMasterVolume(m ? 0.0f : volume_, nullptr);
}

bool SwBackend::EnsureFrame(int w, int h) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || (uint64_t)w * h > 67108864) return false;
    std::lock_guard<std::mutex> lk(frameM_);
    if (frame_.w == w && frame_.h == h && (renderer_ || !frame_.bits.empty())) return true;
    frame_.w = w;
    frame_.h = h;
    frame_.stride = w * 4;
    if(renderer_)frame_.bits.clear();else frame_.bits.assign((size_t)frame_.stride * h, 0);
    frame_.valid = false;
    return true;
}

void SwBackend::PaintFrame() {
    if (!wnd_) return;
    // DXGI resize/present belongs to the window thread. Presenting while the
    // UI waits for the decoder can otherwise deadlock through window messages.
    if(renderer_ && GetCurrentThreadId()!=GetWindowThreadProcessId(wnd_,nullptr)) {
        InvalidateRect(wnd_,nullptr,FALSE);return;
    }
    std::lock_guard<std::mutex> lk(frameM_);
    if(renderer_) { if(frame_.valid && !renderer_->Present()) {LogMsg("GPU video presentation failed");rendererFailed_.store(true);failed_.store(true);} return; }
    HDC dc = GetDC(wnd_);
    if (!dc) return;
    RECT rc;
    GetClientRect(wnd_, &rc);
    int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
    if (cw <= 0 || ch <= 0) { ReleaseDC(wnd_, dc); return; }

    if (!frame_.valid || frame_.w <= 0) {
        HBRUSH b = CreateSolidBrush(RGB(7, 8, 11));
        FillRect(dc, &rc, b);
        DeleteObject(b);
        ReleaseDC(wnd_, dc);
        return;
    }

    double ar = frame_.aspect;
    int dw = cw, dh = std::max(1, (int)std::min<double>(ch, cw / ar));
    if (ar < (double)cw / ch) dw = std::max(1, (int)std::min<double>(cw, ch * ar));
    int dx = (cw - dw) / 2, dy = (ch - dh) / 2;

    HBRUSH b = CreateSolidBrush(RGB(7, 8, 11));
    if (dy > 0) {
        RECT r1 = { 0, 0, cw, dy };
        RECT r2 = { 0, dy + dh, cw, ch };
        FillRect(dc, &r1, b);
        FillRect(dc, &r2, b);
    }
    if (dx > 0) {
        RECT r3 = { 0, dy, dx, dy + dh };
        RECT r4 = { dx + dw, dy, cw, dy + dh };
        FillRect(dc, &r3, b);
        FillRect(dc, &r4, b);
    }
    DeleteObject(b);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = frame_.w;
    bi.bmiHeader.biHeight = -frame_.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, dx, dy, dw, dh, 0, 0, frame_.w, frame_.h,
        frame_.bits.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(wnd_, dc);
}

void SwBackend::OnFrameReady() { PaintFrame(); }

void SwBackend::HandleVideo(IMFSample* sample, LONGLONG ts, uint64_t generation) {
    if (!EnsureFrame(vw_, vh_)) return;
    const double pts = (double)ts / 10000000.0;
    // Decoder output can extend a frame beyond the container duration. Never
    // wait for an unreachable clock value after the playback clock is clamped.
    const double duration = Duration();
    const double target = duration > 0 ? std::min(pts, duration) : pts;
    LONGLONG sampleDuration = 0;
    sample->GetSampleDuration(&sampleDuration);
    const double frameDuration = sampleDuration > 0 ? (double)sampleDuration / 10000000.0 : 1.0 / std::max(1.0, fps_);
    if (pts + frameDuration < discardBefore_.load()) return;
    // Pace before presentation, and discard work invalidated by seek. Decoding
    // while paused is restricted to one requested preview frame.
    while (!closing_.load() && generation == seekGen_.load() && playing_.load() && !preview_.load() && Position() < target - 0.015)
        Sleep(3);
    if (closing_.load() || generation != seekGen_.load()) return;
    if (playing_.load() && !preview_.load() && target < Position() - 0.15) return;
    if (!playing_.load() && !preview_.load()) return;
    bool ok = false;
    const int codedWidth = codedWidth_ ? codedWidth_ : vw_;
    const int codedHeight = codedHeight_ ? codedHeight_ : vh_;

    if(renderer_ && ffmpeg_) {
        std::lock_guard<std::mutex> lock(frameM_);
        const auto video=ffmpeg_->Video();
        if(renderer_->Upload(ffmpeg_->NativeVideo(),video.rotation,video.aspect)) {
            frame_.aspect=displayAspect_;frame_.pts=pts;frame_.valid=ok=true;
        }
        else {rendererFailed_.store(true);failed_.store(true);return;}
    }
    Com<IMFMediaBuffer> buf;
    DWORD count = 0;
    if (!renderer_) {
        if (SUCCEEDED(sample->GetBufferCount(&count)) && count == 1) sample->GetBufferByIndex(0, buf.put());
        else sample->ConvertToContiguousBuffer(buf.put());
    }
    if (buf) {
        Com<IMF2DBuffer2> b2;
        Com<IMF2DBuffer> legacy2D;
        BYTE* data = nullptr; BYTE* start = nullptr; DWORD length = 0;
        LONG pitch = videoStride_;
        int locked = -1;
        if (SUCCEEDED(buf->QueryInterface(__uuidof(IMF2DBuffer2), (void**)b2.put())) &&
            SUCCEEDED(b2->Lock2DSize(MF2DBuffer_LockFlags_Read, &data, &pitch, &start, &length))) locked = 2;
        else if (SUCCEEDED(buf->QueryInterface(__uuidof(IMF2DBuffer), (void**)legacy2D.put())) &&
            SUCCEEDED(legacy2D->Lock2D(&data, &pitch))) locked = 1;
        else if (SUCCEEDED(buf->Lock(&data, nullptr, &length))) {
            locked = 0; start = data;
            // Plain Lock returns the lowest address; Lock2D returns scanline 0.
            if (pitch < 0 && data && (uint64_t)(-(int64_t)pitch) * (codedHeight - 1) < length)
                data += (size_t)(-(int64_t)pitch) * (codedHeight - 1);
        }
        const int64_t rowBytes = (int64_t)codedWidth * (rgbOut_ ? 4 : 1);
        bool valid = data && std::llabs((long long)pitch) >= rowBytes && std::llabs((long long)pitch) <= 131072;
        if (!rgbOut_) valid = valid && pitch > 0 && !(codedWidth & 1) && !(codedHeight & 1);
        if (valid && locked != 1) {
            const int64_t offset = (int64_t)(uintptr_t)data - (int64_t)(uintptr_t)start;
            const int64_t last = offset + (int64_t)pitch * (rgbOut_ ? codedHeight - 1 : codedHeight + codedHeight / 2 - 1);
            valid = std::min(offset, last) >= 0 && std::max(offset, last) + rowBytes <= length;
        }
        if (locked >= 0 && valid) {
            std::lock_guard<std::mutex> lock(frameM_);
            if (rgbOut_) {
                for (int y = 0; y < vh_; ++y)
                    memcpy(frame_.bits.data() + (size_t)y * frame_.stride, data + (ptrdiff_t)(y + cropY_) * pitch + (size_t)cropX_ * 4, (size_t)vw_ * 4);
            } else {
                Nv12ToBgra(data + (size_t)cropY_ * pitch + cropX_, pitch,
                    data + (size_t)pitch * codedHeight + (size_t)(cropY_ / 2) * pitch + cropX_, pitch,
                    frame_.bits.data(), frame_.stride, vw_, vh_, bt709_, fullRange_);
            }
            frame_.aspect = displayAspect_;
            frame_.pts = pts;
            frame_.valid = ok = true;
        }
        if (locked == 2) b2->Unlock2D();
        else if (locked == 1) legacy2D->Unlock2D();
        else if (locked == 0) buf->Unlock();
    }

    if (!ok) { LogMsg("SW invalid video frame layout"); failed_.store(true); Pause(); eof_.store(true); return; }
    if (generation != seekGen_.load()) return;
    {
        std::lock_guard<std::mutex> clock(clockM_);
        if (preview_.load()) { qpc_ = QpcNow(); preview_.store(false); }
    }
    aqCv_.notify_all();
    PaintFrame();
}

bool SwBackend::HandleAudio(IMFSample* sample, LONGLONG ts, uint64_t generation) {
    if (!audioOk_) return false;
    Com<IMFMediaBuffer> buf;
    if (FAILED(sample->ConvertToContiguousBuffer(buf.put()))) return false;
    BYTE* p = nullptr;
    DWORD len = 0;
    if (FAILED(buf->Lock(&p, nullptr, &len))) return false;
    if (!p || len == 0) { buf->Unlock(); return false; }
    UINT32 block = (UINT32)(mixChannels_ * (mixBits_ / 8));
    UINT32 frames = block ? len / block : 0;
    if (!frames) { buf->Unlock(); return false; }
    const double pts = (double)ts / 10000000.0;
    UINT32 skip = (UINT32)std::min<double>(frames, std::max(0.0, (discardBefore_.load() - pts) * mixRate_));
    if (skip == frames) { buf->Unlock(); return false; }
    AudioChunk c;
    c.data.assign(p + (size_t)skip * block, p + (size_t)frames * block);
    c.frames = frames - skip;
    c.pts = pts + (double)skip / mixRate_;
    c.generation = generation;
    buf->Unlock();
    {
        std::lock_guard<std::mutex> lk(aqM_);
        if (generation != seekGen_.load()) return false;
        aq_.push_back(std::move(c));
    }
    aqCv_.notify_one();
    return true;
}

int SwBackend::KindOf(DWORD idx) {
    if (ffmpeg_) return idx == 0 ? 0 : 1;
    auto it = kindMap_.find(idx);
    if (it != kindMap_.end()) return it->second;
    int kind = 2;
    Com<IMFMediaType> mt;
    if (reader_ && SUCCEEDED(reader_->GetCurrentMediaType(idx, mt.put()))) {
        GUID g = {};
        if (SUCCEEDED(mt->GetGUID(MF_MT_MAJOR_TYPE, &g))) {
            if (g == MFMediaType_Video) kind = 0;
            else if (g == MFMediaType_Audio) kind = 1;
        }
    }
    kindMap_[idx] = kind;
    return kind;
}

void SwBackend::VideoLoop() {
    unsigned ended = 0;
    uint64_t lastGeneration = seekGen_.load();
    while (!closing_.load()) {
        double target;
        uint64_t request;
        {
            std::lock_guard<std::mutex> lock(seekM_);
            target = previewTarget_.exchange(-1);
            request = previewRequest_.load();
        }
        if (target >= 0) ApplySeek(target, request);
        if (previewApplied_.load() != previewRequest_.load()) { Sleep(1); continue; }
        if (!reader_ && !ffmpeg_) { Sleep(20); continue; }
        if (eof_.load()) { Sleep(20); continue; }
        if (!playing_.load() && !preview_.load()) {
            std::unique_lock<std::mutex> lock(aqM_);
            aqCv_.wait_for(lock, std::chrono::milliseconds(40));
            continue;
        }
        uint64_t gen = seekGen_.load();
        if (gen != lastGeneration) { ended = 0; lastGeneration = gen; }
        if (ended == selectedStreams_) {
            if (duration_ <= 0 || Position() >= duration_ - 0.01) {
                std::lock_guard<std::mutex> lock(clockM_);
                base_ = duration_ > 0 ? duration_ : PosLocked();
                playing_.store(false);
                eof_.store(true);
                preview_.store(false);
                if (audioOk_ && ac_) { std::lock_guard<std::mutex> al(audioM_); ac_->Stop(); }
            }
            Sleep(5);
            continue;
        }
        // At most about half a second of decoded audio, even for audio-only
        // files. Back-pressure replaces the previous 300-packet drop policy.
        bool bufferedAhead;
        const double position = Position();
        {
            std::lock_guard<std::mutex> lock(aqM_);
            bufferedAhead = !aq_.empty() && aq_.back().pts > position + 0.40;
        }
        if (bufferedAhead && playing_.load() && !preview_.load()) { Sleep(5); continue; }
        DWORD idx = 0, flags = 0;
        LONGLONG ts = 0;
        Com<IMFSample> sample;
        HRESULT hr;
        {
            std::lock_guard<std::mutex> lk(ioM_);
            if (gen != seekGen_.load() || previewApplied_.load() != previewRequest_.load()) continue;
            // Once a stream ends, request the remaining stream explicitly.
            // Deselecting an ended stream can tear down ASF/MOV processing
            // while its audio converter still has buffered output to drain.
            const DWORD stream = (ended & 1) ? (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM :
                                 (ended & 2) ? (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM : (DWORD)MF_SOURCE_READER_ANY_STREAM;
            if (ffmpeg_) {
                hr = ffmpeg_->Read(&idx, &flags, &ts, sample.put());
                std::lock_guard<std::mutex> clock(clockM_);
                duration_ = ffmpeg_->Duration();
            } else hr = reader_->ReadSample(stream, 0, &idx, &flags, &ts, sample.put());
        }
        if (closing_.load()) break;
        if (gen != seekGen_.load() || previewApplied_.load() != previewRequest_.load()) continue;
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            LogMsg("SW ReadSample 0x%08X flags=%lu stream=%lu ts=%lld ended=%u selected=%u pos=%.3f duration=%.3f", hr, flags, idx, (long long)ts, ended, selectedStreams_, Position(), duration_);
            failed_.store(true); Pause(); preview_.store(false); eof_.store(true); continue;
        }
        if (gen != seekGen_.load()) continue;
        int kind;
        {
            std::lock_guard<std::mutex> lock(ioM_);
            if (gen != seekGen_.load()) continue;
            kind = KindOf(idx);
            if (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
                LogMsg("SW media type event flags=%lu stream=%lu kind=%d", flags, idx, kind);
                bool valid = true;
                if (kind == 0) valid = ffmpeg_ ? UpdateCompatibilityVideo() : UpdateVideoFormat();
                else if (kind == 1 && (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
                    // Transport streams can reveal their AAC format only after
                    // initial packets. Reconfigure before asking the decoder for
                    // more output, even if the requested PCM layout is unchanged.
                    valid = SetAudioFormat();
                    sample.reset();
                }
                else if (kind == 1) {
                    Com<IMFMediaType> type;
                    GUID subtype{};
                    valid = SUCCEEDED(reader_->GetCurrentMediaType(idx, type.put()));
                    if (valid) {
                        type->GetGUID(MF_MT_SUBTYPE, &subtype);
                        if (MFGetAttributeUINT32(type.get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0) != (UINT32)mixRate_ ||
                            MFGetAttributeUINT32(type.get(), MF_MT_AUDIO_NUM_CHANNELS, 0) != (UINT32)mixChannels_ ||
                            MFGetAttributeUINT32(type.get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 0) != (UINT32)mixBits_ ||
                            subtype != (mixFloat_ ? MFAudioFormat_Float : MFAudioFormat_PCM)) {
                            valid = SetAudioFormat();
                            sample.reset(); // Do not interpret samples from the old layout.
                        }
                    }
                }
                if (!valid) { failed_.store(true); Pause(); preview_.store(false); eof_.store(true); continue; }
            }
        }
        if (sample) {
            if (kind == 0) HandleVideo(sample.get(), ts, gen);
            else if (kind == 1 && HandleAudio(sample.get(), ts, gen) && !(selectedStreams_ & 1)) {
                std::lock_guard<std::mutex> clock(clockM_);
                if (preview_.load()) { qpc_ = QpcNow(); preview_.store(false); }
            }
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            std::lock_guard<std::mutex> lock(ioM_);
            if (gen == seekGen_.load()) {
                ended |= kind == 0 ? 1 : (kind == 1 ? 2 : 0);
                if (kind == 0 || !(selectedStreams_ & 1)) {
                    std::lock_guard<std::mutex> clock(clockM_);
                    if (preview_.load()) { qpc_ = QpcNow(); preview_.store(false); }
                    aqCv_.notify_all();
                }
            }
            continue;
        }
        if (!sample) Sleep(1);
    }
}

void SwBackend::AudioLoop() {
    AudioChunk cur;
    bool have = false;
    uint64_t gen = 0;
    int failures = 0;
    while (!closing_.load()) {
        if (!audioOk_ || !arc_ || !ac_ || audioLost_.load()) { Sleep(20); continue; }
        if (!playing_.load() || !SeekReady()) {
            if(eof_.load()) {
                std::lock_guard<std::mutex> tap(renderedTapM_);
                if(renderedTap_ && !renderedFinished_) {
                    renderedFinished_=true;renderedTap_(nullptr,0,mixRate_,mixChannels_,audioEnd_.load());
                }
            }
            std::unique_lock<std::mutex> lk(aqM_);
            aqCv_.wait_for(lk, std::chrono::milliseconds(40));
            continue;
        }
        {
        std::lock_guard<std::mutex> audioLock(audioM_);
        if (gen != seekGen_.load()) { have = false; cur.data.clear(); gen = seekGen_.load(); }
        if (!have) {
            std::lock_guard<std::mutex> lk(aqM_);
            if (!aq_.empty()) { cur = std::move(aq_.front()); aq_.pop_front(); have = true; gen = seekGen_.load(); }
        }
        bool active = playing_.load() && rate_.load() == 1.0;
        if (have && cur.generation != seekGen_.load()) { have = false; continue; }
        UINT32 padding = 0;
        HRESULT hr = ac_->GetCurrentPadding(&padding);
        if (SUCCEEDED(hr) && have && active) {
            const UINT32 avail = mixFrames_ > padding ? mixFrames_ - padding : 0;
            if (avail > 0) {
                const UINT32 n = std::min(avail, cur.frames);
                BYTE* dst = nullptr;
                hr = arc_->GetBuffer(n, &dst);
                if (SUCCEEDED(hr) && dst) {
                    const UINT32 block = have_.nBlockAlign;
                    memcpy(dst, cur.data.data() + (size_t)cur.offset * block, (size_t)n * block);
                    hr = arc_->ReleaseBuffer(n, 0);
                    if (SUCCEEDED(hr)) {
                        std::lock_guard<std::mutex> tap(renderedTapM_);
                        if(renderedTap_ && cur.generation==seekGen_.load()) {
                            renderedFinished_=false;
                            const BYTE* input=cur.data.data()+(size_t)cur.offset*block;
                            const size_t samples=(size_t)n*mixChannels_, bytes=mixBits_/8;
                            renderedPcm_.resize(samples);
                            for(size_t i=0;i<samples;++i) {
                                const BYTE* p=input+i*bytes; float value=0;
                                if(mixFloat_ && bytes==4) memcpy(&value,p,4);
                                else if(bytes==1) value=((int)*p-128)/128.f;
                                else if(bytes==2) {int16_t v;memcpy(&v,p,2);value=v/32768.f;}
                                else if(bytes==3) {int32_t v=(p[0]|p[1]<<8|p[2]<<16);if(v&0x800000)v|=~0xffffff;value=v/8388608.f;}
                                else if(bytes==4) {int32_t v;memcpy(&v,p,4);value=(float)(v/2147483648.0);}
                                renderedPcm_[i]=value;
                            }
                            renderedTap_(renderedPcm_.data(),(int)n,mixRate_,mixChannels_,cur.pts);
                        }
                        cur.offset += n; cur.pts += (double)n / mixRate_; cur.frames -= n;
                        audioEnd_.store(cur.pts); padding += n;
                        if (!cur.frames) { have = false; cur.data.clear(); }
                    }
                }
            }
        }
        if (SUCCEEDED(hr)) {
            failures = 0;
            double pos = audioEnd_.load() - (double)padding / mixRate_;
            if (pos < 0) pos = 0;
            audioPos_.store(pos);
        } else if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING || ++failures >= 3) {
            LogMsg("WASAPI output lost 0x%08X", hr);
            audioLost_.store(true);
        }
        }
        if (audioEventDriven_) WaitForSingleObject(audioEvent_, 20);
        else Sleep(5);
    }
}
void SwBackend::SetRenderedTap(std::function<void(const float*,int,int,int,double)> sink) {
    std::lock_guard<std::mutex> lock(renderedTapM_); renderedTap_=std::move(sink);renderedFinished_=false;
}
bool SwBackend::ConfigureVideo(const std::wstring& path,bool sdr) {
    std::lock_guard<std::mutex> lock(frameM_);
    if(renderer_ && !renderer_->Configure(path,sdr))return false;
    if(!renderer_ && (ffmpeg_ || reader_) && !path.empty())return false;
    shaderPath_=path;forceSdr_=sdr;return true;
}
VideoRenderInfo SwBackend::RenderInfo() const {
    std::lock_guard<std::mutex> lock(frameM_);return renderer_?renderer_->Info():VideoRenderInfo{};
}
bool SwBackend::RecoverVideo(bool softwareGpu) {
    std::lock_guard<std::mutex> lock(frameM_);
    if(!renderer_ || !renderer_->Recover(softwareGpu))return false;
    rendererFailed_.store(false);failed_.store(false);LogMsg("Video renderer recreated software_gpu=%d",(int)softwareGpu);
    InvalidateRect(wnd_,nullptr,FALSE);return true;
}

// ===========================================================================
// Player facade
// ===========================================================================
Player::~Player() {
    Close();
}

void Player::SetWindows(HWND hwVideo, HWND swVideo) {
    hwWnd_ = hwVideo;
    swWnd_ = swVideo;
}

static std::wstring DefaultAudioDeviceId() {
    Com<IMMDeviceEnumerator> enumerator;
    Com<IMMDevice> device;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)enumerator.put())) ||
        FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put()))) return L"";
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id))) return L"";
    std::wstring result = id;
    CoTaskMemFree(id);
    return result;
}

// Best-effort check of whether the OS will let us create a shared-mode render
// stream right now. Used for the MFPlay path, which owns its own audio renderer
// and therefore cannot tell us directly. Returns true when we cannot determine
// the answer, so we never nag about a healthy machine.
static bool SystemAudioUsable() {
    Com<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)en.put()))) return true;
    Com<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, dev.put())) || !dev) return false;
    Com<IAudioClient> ac;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)ac.put())) || !ac) return true;
    WAVEFORMATEX* mix = nullptr;
    if (FAILED(ac->GetMixFormat(&mix)) || !mix) return true;
    HRESULT hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, mix, nullptr);
    CoTaskMemFree(mix);
    if (FAILED(hr)) LogMsg("SystemAudioUsable: Initialize 0x%08X", hr);
    return SUCCEEDED(hr);
}

bool Player::ModernCoreAvailable() { return MpvBackend::Available(); }
bool Player::CoreActive() const { return dynamic_cast<MpvBackend*>(backend_.get())!=nullptr; }
bool Player::Loading() const {auto* core=dynamic_cast<MpvBackend*>(backend_.get());return core && core->Loading();}
bool Player::CancelLoading() {
    auto* core=dynamic_cast<MpvBackend*>(backend_.get());
    if(!core || !core->CancelLoading())return false;
    StopTap();playbackHeld_=playRequested_=false;hasVideo_=false;path_.clear();return true;
}
CoreStatus Player::CoreInfo() const {auto* core=dynamic_cast<MpvBackend*>(backend_.get());return core?core->Status():CoreStatus{};}
void Player::ConfigurePlayback(const CoreOptions& options) {
    coreOptions_=options;preferHardware_=options.preferHardware;
    shaderPath_=options.shaderPath;forceSdr_=options.forceSdr;
    if(auto* core=dynamic_cast<MpvBackend*>(backend_.get()))core->SetOptions(options);
}
bool Player::SelectAudioTrack(int64_t id) {
    auto* core=dynamic_cast<MpvBackend*>(backend_.get());if(!core)return false;
    if(!core->SelectAudioTrack(id))return false;
    ++tapSeekGen_;if(audioReset_)audioReset_(Position());return true;
}
bool Player::SelectSubtitleTrack(int64_t id) {
    auto* core=dynamic_cast<MpvBackend*>(backend_.get());return core && core->SelectSubtitleTrack(id);
}
bool Player::AddSubtitle(const std::wstring& path) {
    auto* core=dynamic_cast<MpvBackend*>(backend_.get());if(!core || !FileExists(path))return false;
    core->AddSubtitle(path,true);return true;
}
void Player::SubtitlePresentation(bool visible,double scale) {
    if(auto* core=dynamic_cast<MpvBackend*>(backend_.get()))core->SetSubtitlePresentation(visible,scale);
}
bool Player::SetABLoop(double a,double b) {
    auto* core=dynamic_cast<MpvBackend*>(backend_.get());
    if(!core || !CanSeek() || (a>=0 && b>=0 && b<=a))return false;
    core->SetABLoop(a,b);return true;
}
bool Player::FrameStep(int frames) {
    auto* core=dynamic_cast<MpvBackend*>(backend_.get());if(!core || !HasVideo() || !CanSeek())return false;
    Pause();++tapSeekGen_;if(audioReset_)audioReset_(Position());core->FrameStep(frames);return true;
}

bool Player::Open(const std::wstring& path, bool preferHardware, bool autoplay, std::unique_ptr<FfmpegReader> prepared) {
    Close();
    path_ = path;
    preferHardware_=preferHardware;
    videoRecoveries_=0;
    hwAvailable_ = preferHardware && !IsNetworkMedia(path) && !IsPlaylistFile(path);
    hwFailed_ = false;
    audioFailed_ = false;
    audioDeviceId_ = DefaultAudioDeviceId();
    nextAudioCheck_ = QpcNow() + 2;
    nextAudioRetry_ = QpcNow() + 5;
    audioRetries_ = 0;
    if(modernCore_ && MpvBackend::Available()) {
        coreOptions_.preferHardware=preferHardware;coreOptions_.shaderPath=shaderPath_;coreOptions_.forceSdr=forceSdr_;
        auto core=std::make_unique<MpvBackend>(swWnd_,coreOptions_);
        core->SetVolume(volume_);core->SetMuted(muted_);
        if(core->Open(path,autoplay)) {
            backend_=std::move(core);playRequested_=autoplay;useHw_=false;
            hwAvailable_=preferHardware;hasVideo_=false;rate_=1;return true;
        }
    }

    // DXVA-capable system decoders need not appear in the hardware-only MFT
    // enumeration. Try the actual playback pipeline instead of excluding it.
    if (hwAvailable_ && !prepared && !NativeAudioContainer(path) && !VideoRenderer::Available()) {
        auto* hw = new HwBackend(hwWnd_);
        hw->SetVolume(volume_);
        hw->SetMuted(muted_);
        if (hw->Open(path, autoplay)) {
            audioFailed_ = hw->HasAudio() && !SystemAudioUsable();
            hasVideo_ = hw->Width() > 0 && hw->Height() > 0;
            backend_.reset(hw);
            playRequested_ = autoplay;
            useHw_ = true;
            rate_ = 1.0;
            SetVolume(volume_);
            SetMuted(muted_);
            return true;
        }
        LogMsg("hardware backend open failed, falling back to software");
        delete hw;
        hwFailed_ = true;
    }

    auto* sw = new SwBackend(swWnd_,false,preferHardware);
    sw->ConfigureVideo(shaderPath_,forceSdr_);
    sw->SetVolume(volume_);
    sw->SetMuted(muted_);
    if (!sw->OpenPrepared(path, std::move(prepared),autoplay)) { audioFailed_ = sw->AudioDeviceUnavailable(); delete sw; path_.clear(); return false; }
    hasVideo_ = sw->Width() > 0 && sw->Height() > 0;
    audioFailed_ = sw->HasAudio() && !sw->AudioReady();
    if (!hasVideo_ && audioFailed_) {
        // Nothing to show and nothing to hear: report a failed open, but keep the
        // reason so the caller can tell the user what actually went wrong.
        delete sw;
        path_.clear();
        return false;
    }
    backend_.reset(sw);
    playRequested_ = autoplay;
    useHw_ = false;
    rate_ = 1.0;
    SetVolume(volume_);
    SetMuted(muted_);
    return true;
}

void Player::Close() {
    StopTap();
    scrubbing_.store(false);
    backend_.reset();
    useHw_ = false;
    hasVideo_ = false;
    playbackHeld_ = playRequested_ = false;
    rate_.store(1.0);
    path_.clear();
}

bool Player::SwitchToSoftware() {
    return ReplaceBackend(false, true);
}

bool Player::SwitchDecoder(bool preferHardware) {
    if (!backend_) return false;
    if(auto* core=dynamic_cast<MpvBackend*>(backend_.get())) {
        coreOptions_.preferHardware=preferHardware;preferHardware_=preferHardware;
        core->SetOptions(coreOptions_);return true;
    }
    if(IsNetworkMedia(path_) || IsPlaylistFile(path_)) return !preferHardware;
    if (preferHardware == preferHardware_ && !backend_->Failed()) return true;
    return ReplaceBackend(preferHardware, false);
}

bool Player::ReplaceBackend(bool hardware, bool recovery) {
    if (!backend_) return false;
    if(recovery && backend_->RenderFailed() && hasVideo_ && backend_->RenderInfo().adapter[0]) {
        const bool warp=videoRecoveries_>=1;++videoRecoveries_;
        if(backend_->RecoverVideo(warp))return true;
    }
    if(IsNetworkMedia(path_) || IsPlaylistFile(path_)) return false;
    const bool retryingAudio = !useHw_ && (audioFailed_ || backend_->AudioLost());
    const bool compatibility = backend_->IsCompatibility() || (!useHw_ && backend_->Failed());
    const bool videoFailure=recovery && backend_->Failed() && hasVideo_;
    const bool softwareGpu=videoFailure && videoRecoveries_>=1;
    if(videoFailure)++videoRecoveries_;
    const bool system=hardware && !VideoRenderer::Available();
    std::unique_ptr<Backend> candidate = system
        ? std::unique_ptr<Backend>(new HwBackend(hwWnd_))
        : std::unique_ptr<Backend>(new SwBackend(swWnd_, compatibility,hardware,softwareGpu));
    auto* next=dynamic_cast<SwBackend*>(candidate.get());
    if(next)next->deferRenderer_=true;
    candidate->ConfigureVideo(shaderPath_,forceSdr_);
    candidate->SetVolume(volume_);
    candidate->SetMuted(muted_);
    // A failed user switch must leave the working decoder and tap intact.
    // Prepare the other pipeline while the current media can still play.
    if (!candidate->Open(path_, false)) {
        LogMsg("decoder switch unavailable hardware=%d; retaining current backend", (int)hardware);
        return false;
    }
    if(hardware && !candidate->HardwareReady()) {
        LogMsg("D3D11VA unavailable for this codec/profile; retaining current decoder");return false;
    }
    const bool restartTap = TapRunning();
    StopTap();
    const double pos = backend_->Position();
    const bool requested = recovery ? playRequested_ : WantsToPlay();
    const bool wasPlaying = requested && !playbackHeld_ && (!backend_->IsEof() || backend_->Failed());
    const double rate = rate_.load();
    backend_->Pause();
    // Keep one swapchain per HWND. Preparing a second one while the current
    // flip chain is alive silently forces DXGI into slower bitblt presentation.
    std::unique_ptr<VideoRenderer> renderer;
    if(next && !videoFailure) {
        if(auto* previous=dynamic_cast<SwBackend*>(backend_.get()))renderer=previous->ReleaseRenderer();
    }
    backend_.reset();
    if(next)next->AdoptRenderer(std::move(renderer));
    backend_ = std::move(candidate);
    playRequested_ = requested;
    useHw_ = system;preferHardware_=hardware;
    hasVideo_ = backend_->Width() > 0 && backend_->Height() > 0;
    rate_.store(1.0);
    if (rate != 1 && backend_->SetRate(rate)) rate_.store(rate);
    hwFailed_ = false;
    hwAvailable_ = hardware;
    const auto deviceId = DefaultAudioDeviceId();
    if (deviceId != audioDeviceId_) audioRetries_ = 0;
    audioDeviceId_ = deviceId;
    nextAudioCheck_ = QpcNow() + 2;
    backend_->Seek(pos);
    if (wasPlaying) backend_->Play(); else backend_->Pause();
    auto* sw = dynamic_cast<SwBackend*>(backend_.get());
    audioFailed_ = backend_->HasAudio() && (sw ? !sw->AudioReady() : !SystemAudioUsable());
    // Count the attempt even when Initialize succeeds: Start or the render
    // worker can invalidate that same endpoint immediately afterwards.
    if ((recovery && retryingAudio) || audioFailed_) {
        ++audioRetries_;
        nextAudioRetry_ = QpcNow() + 5 * (1u << std::min(audioRetries_ - 1, 2u));
    }
    SetVolume(volume_);
    SetMuted(muted_);
    LogMsg("switched decoder hardware=%d at %.2fs compatibility=%d", (int)hardware, pos, (int)backend_->IsCompatibility());
    if (restartTap) StartTap();
    return true;
}

bool Player::NeedsFallback() {
    if(CoreActive())return false;
    return backend_ && videoRecoveries_<2 && (hwFailed_ || backend_->Failed());
}

bool Player::NeedsAudioRecovery() {
    if(CoreActive())return false;
    if (!backend_ || useHw_ || !HasAudio() || backend_->IsEof()) return false;
    const double now = QpcNow();
    const bool lost = backend_->AudioLost();
    if (now >= nextAudioCheck_) {
        nextAudioCheck_ = now + 2;
        if (DefaultAudioDeviceId() != audioDeviceId_) return true;
        if (!lost && !audioFailed_ && now >= nextAudioRetry_) audioRetries_ = 0;
    }
    return audioRetries_ < 3 && (lost || audioFailed_) &&
        ((lost && audioRetries_ == 0) || now >= nextAudioRetry_);
}

void Player::Play() {
    playRequested_ = true;
    if (playbackHeld_) return;
    // Reset the tap generation even for sub-second files, whose EOF-to-zero
    // distance is too short for the audio tap's drift detection heuristic.
    if (backend_ && backend_->IsEof() && backend_->SeekReady()) Seek(0);
    if (backend_) backend_->Play();
}

void Player::Pause() {
    playRequested_ = false;
    if (backend_) backend_->Pause();
}

void Player::HoldPlayback(bool hold) {
    if (playbackHeld_ == hold || !backend_) return;
    if (hold) {
        playRequested_ = WantsToPlay();
        backend_->Pause();
        playbackHeld_ = true;
    } else {
        playbackHeld_ = false;
        LogMsg("Caption playback released at %.3fs pending=%d", Position(), (int)playRequested_);
        if (playRequested_) backend_->Play();
    }
}

void Player::TogglePlay() {
    if (!backend_) return;
    if (WantsToPlay()) Pause();
    else Play();
}

void Player::SeekRelative(double d) {
    if (backend_) Seek(backend_->Position() + d);
}

void Player::Seek(double sec) {
    if (!CanSeek()) return;
    std::lock_guard<std::mutex> lk(tapMutex_);
    sec = Clampd(sec, 0, std::max(0.0, backend_->Duration()));
    backend_->Seek(sec);
    tapSeekGen_.fetch_add(1);
    if (audioReset_) audioReset_(sec);
}

void Player::PreviewSeek(double sec) {
    if (!CanSeek() || !std::isfinite(sec)) return;
    ++tapSeekGen_;
    backend_->PreviewSeek(Clampd(sec, 0, std::max(0.0, Duration())));
}

void Player::SetScrubbing(bool enabled) {
    if (scrubbing_.exchange(enabled) == enabled) return;
    std::lock_guard<std::mutex> lock(tapMutex_);
    ++tapSeekGen_;
    if (enabled && audioReset_) audioReset_(Position());
}

void Player::SetVolume(float v) {
    volume_ = Clampf(v, 0, 1);
    if (backend_) backend_->SetVolume(volume_);
}

void Player::SetMuted(bool m) {
    muted_ = m;
    if (backend_) backend_->SetMuted(m);
}

// --- Caption audio tap -----------------------------------------------------
bool Player::EnsureTap() {
    if (tapReader_ || tapCompatibility_) return true;
    if (IsCompatibility()) return EnsureCompatibilityTap();
    return EnsureNativeTap() || EnsureCompatibilityTap();
}

bool Player::EnsureCompatibilityTap() {
    tapReader_.reset();
    auto reader = std::make_unique<FfmpegReader>();
    WAVEFORMATEX audio{};
    audio.wFormatTag = WAVE_FORMAT_PCM; audio.nChannels = 1; audio.nSamplesPerSec = 16000;
    audio.wBitsPerSample = 16; audio.nBlockAlign = 2; audio.nAvgBytesPerSec = 32000;
    if (!reader->Open(path_, &audio, true) || !reader->DecodesAudio()) return false;
    tapCompatibility_ = std::move(reader);
    tapRate_ = 16000; tapChannels_ = 1;
    LogMsg("FFmpeg audio tap ready 16000 Hz 1 ch");
    return true;
}

bool Player::EnsureNativeTap() {
    if (path_.empty()) return false;
    Com<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(attrs.put(), 2))) return false;

    Com<IMFByteStream> bs;
    if (!MakeByteStream(path_, bs.put())) return false;
    Com<IMFSourceReader> rd;
    if (FAILED(MFCreateSourceReaderFromByteStream(bs.get(), attrs.get(), rd.put()))) return false;
    rd->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(rd->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE))) return false;

    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 1;
    wf.nSamplesPerSec = 16000;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2;
    wf.nAvgBytesPerSec = 32000;
    HRESULT hr = SetPcmType(rd.get(), wf);
    if (FAILED(hr)) {
        wf.nChannels = 2;
        wf.nSamplesPerSec = 48000;
        wf.nBlockAlign = 4;
        wf.nAvgBytesPerSec = 192000;
        hr = SetPcmType(rd.get(), wf);
        tapRate_ = 48000;
        tapChannels_ = 2;
    } else {
        tapRate_ = 16000;
        tapChannels_ = 1;
    }
    if (FAILED(hr)) { LogMsg("tap audio type refused 0x%08X", hr); return false; }
    tapReader_ = rd;
    LogMsg("audio tap ready %d Hz %d ch", tapRate_, tapChannels_);
    return true;
}

void Player::StartTap() {
    if (tapRunning_.load()) return;
    if(auto* core=dynamic_cast<MpvBackend*>(backend_.get())) {
        if(!core->SupportsPcmTap() || !core->HasAudio())return;
        tapFailed_=false;tapRunning_=true;
        tapPos_.store(Position());
        if(audioReset_)audioReset_(Position());
        core->SetPcmSink([this](const float* pcm,int count,int rate,int channels,double time){
            std::lock_guard<std::mutex> lock(tapMutex_);
            if(tapRunning_ && !scrubbing_ && backend_ && backend_->SeekReady()) {
                if(!std::isfinite(time))time=Position();
                if(count && (time<tapPos_.load()-0.2 || time>tapPos_.load()+0.9) && audioReset_)audioReset_(time);
                if(rate>0)tapPos_.store(time+(double)count/rate);
                if(count && audioSink_)audioSink_(pcm,count,rate,channels,time);
                else if(!count && audioFinished_)audioFinished_(time);
            }
        });
        LogMsg("audio tap ready mpv selected PCM; same playback connection");return;
    }
    if(IsNetworkMedia(path_) || IsPlaylistFile(path_)) {
        auto* sw=dynamic_cast<SwBackend*>(backend_.get());
        if(!sw || !sw->AudioReady()) return;
        tapFailed_.store(false); tapRunning_.store(true);
        if(audioReset_) audioReset_(Position());
        sw->SetRenderedTap([this](const float* pcm,int count,int rate,int channels,double time){
            std::lock_guard<std::mutex> lock(tapMutex_);
            if(tapRunning_.load() && !scrubbing_.load() && backend_ && backend_->SeekReady() && rate_.load()==1) {
                if(count && audioSink_) audioSink_(pcm,count,rate,channels,time);
                else if(!count && audioFinished_) audioFinished_(time);
            }
        });
        LogMsg("audio tap ready rendered PCM; no second stream connection");
        return;
    }
    if (tapThread_.joinable()) tapThread_.join();
    try {
        if (!EnsureTap()) return;
        tapFailed_.store(false);
        tapPos_.store(Position());
        tapRunning_.store(true);
        tapThread_ = std::thread([this] {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            try { TapLoop(); }
            catch (const std::exception& error) { LogMsg("Caption audio worker failed: %s", error.what()); tapFailed_.store(true); }
            tapRunning_.store(false);
            if (SUCCEEDED(com)) CoUninitialize();
        });
    } catch (const std::exception& error) {
        LogMsg("Caption audio thread failed: %s", error.what());
        tapFailed_.store(true); tapRunning_.store(false);
    }
}

void Player::StopTap() {
    tapRunning_.store(false);
    if(auto* core=dynamic_cast<MpvBackend*>(backend_.get()))core->SetPcmSink({});
    if(auto* sw=dynamic_cast<SwBackend*>(backend_.get())) sw->SetRenderedTap({});
    if(tapCompatibility_) tapCompatibility_->Interrupt();
    if (tapThread_.joinable()) tapThread_.join();
    std::lock_guard<std::mutex> lk(tapMutex_);
    tapReader_.reset();
    tapCompatibility_.reset();
}

void Player::TapLoop() {
    bool eof = false;
    bool tailFlushed = false;
    uint64_t generation = UINT64_MAX;
    double discardBefore = 0;
    std::vector<float> tmp;
    auto finishInput = [&] {
        std::lock_guard<std::mutex> lk(tapMutex_);
        if (!scrubbing_.load() && backend_ && backend_->SeekReady() && generation == tapSeekGen_.load()) {
            eof = true;
            if (!tailFlushed && audioFinished_) audioFinished_(tapPos_.load());
            tailFlushed = true;
        }
    };
    while (tapRunning_.load()) {
        if (!backend_ || scrubbing_.load() || !backend_->SeekReady() || (!backend_->IsPlaying() && !backend_->IsEof()) || rate_.load() != 1.0) { Sleep(40); continue; }
        const double pos = backend_->Position();
        const uint64_t requested = tapSeekGen_.load();
        if (generation != requested || std::fabs(tapPos_.load() - pos) > 0.9) {
            // Only this worker uses the reader. StopTap joins it before reader
            // replacement; a slow demux seek must not hold the UI's delivery lock.
            bool positioned = false;
            if (tapCompatibility_) positioned = tapCompatibility_->Seek(pos);
            else if (tapReader_) {
                tapReader_->Flush(MF_SOURCE_READER_ALL_STREAMS);
                positioned = SetReaderTime(tapReader_.get(), pos);
            }
            {
                std::lock_guard<std::mutex> lk(tapMutex_);
                if (scrubbing_.load() || !backend_->SeekReady() || requested != tapSeekGen_.load()) continue;
                if (!positioned && !tapCompatibility_ && EnsureCompatibilityTap()) positioned = tapCompatibility_->Seek(pos);
                if (!positioned) { LogMsg("Caption audio seek failed at %.3fs", pos); tapFailed_.store(true); break; }
                generation = requested;
                discardBefore = pos;
                if (audioReset_) audioReset_(pos);
                tapPos_.store(pos);
                eof = false;
                tailFlushed = false;
            }
        }
        if (tapPos_.load() > pos + 0.30) { Sleep(30); continue; }
        if (eof) { Sleep(50); continue; }

        DWORD idx = 0, flags = 0;
        LONGLONG ts = 0;
        Com<IMFSample> sample;
        HRESULT hr = S_OK;
        if (scrubbing_.load() || !backend_->SeekReady() || generation != tapSeekGen_.load()) continue;
        if (tapCompatibility_) hr = tapCompatibility_->Read(&idx, &flags, &ts, sample.put());
        else if (tapReader_) hr = tapReader_->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &idx, &flags, &ts, sample.put());
        else break;
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            std::lock_guard<std::mutex> lock(tapMutex_);
            if (scrubbing_.load() || !backend_->SeekReady() || generation != tapSeekGen_.load()) continue;
            LogMsg("Caption audio ReadSample failed 0x%08X flags=%lu", hr, flags);
            if (!tapCompatibility_ && EnsureCompatibilityTap()) {
                const double restart = backend_->Position();
                if (tapCompatibility_->Seek(restart)) {
                    generation = tapSeekGen_.load(); tapPos_.store(restart);
                    discardBefore = restart;
                    if (audioReset_) audioReset_(restart);
                    continue;
                }
            }
            tapFailed_.store(true); break;
        }
        if (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
            std::lock_guard<std::mutex> lock(tapMutex_);
            if (scrubbing_.load() || !backend_->SeekReady() || generation != tapSeekGen_.load()) continue;
            Com<IMFMediaType> type;
            GUID subtype{};
            hr = tapReader_->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, type.put());
            if (SUCCEEDED(hr)) type->GetGUID(MF_MT_SUBTYPE, &subtype);
            if (FAILED(hr) || subtype != MFAudioFormat_PCM ||
                MFGetAttributeUINT32(type.get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 0) != 16 ||
                MFGetAttributeUINT32(type.get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0) != (UINT32)tapRate_ ||
                MFGetAttributeUINT32(type.get(), MF_MT_AUDIO_NUM_CHANNELS, 0) != (UINT32)tapChannels_ ||
                (flags & MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
                WAVEFORMATEX format{};
                format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = (WORD)tapChannels_;
                format.nSamplesPerSec = tapRate_; format.wBitsPerSample = 16;
                format.nBlockAlign = (WORD)(tapChannels_ * 2); format.nAvgBytesPerSec = tapRate_ * format.nBlockAlign;
                if (FAILED(SetPcmType(tapReader_.get(), format))) { tapFailed_.store(true); break; }
                sample.reset();
            }
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) && !sample) {
            finishInput();
            continue;
        }
        if (!sample) { Sleep(5); continue; }

        Com<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(buf.put()))) continue;
        BYTE* p = nullptr;
        DWORD len = 0;
        if (FAILED(buf->Lock(&p, nullptr, &len))) continue;
        if (!p || !len) { buf->Unlock(); continue; }
        int samples = (int)(len / sizeof(int16_t));
        int frames = tapChannels_ > 0 ? samples / tapChannels_ : samples;
        double pts = (double)ts / 10000000.0;
        const int skipped = (int)std::min<double>(frames, std::max(0.0, std::ceil((discardBefore - pts) * tapRate_)));
        frames -= skipped;
        pts += (double)skipped / tapRate_;
        if (frames > 0 && audioSink_) {
            tmp.resize((size_t)frames);
            const int16_t* s = (const int16_t*)p + (size_t)skipped * tapChannels_;
            for (int i = 0; i < frames; ++i) {
                float v;
                if (tapChannels_ == 2) v = (s[i * 2] + s[i * 2 + 1]) * 0.5f / 32768.0f;
                else v = s[i] / 32768.0f;
                tmp[i] = v;
            }
            // Serialize delivery with Seek: converted samples from an older
            // generation must never reach a newly reset caption stream.
            std::lock_guard<std::mutex> lk(tapMutex_);
            if (!scrubbing_.load() && backend_->SeekReady() && generation == tapSeekGen_.load()) {
                audioSink_(tmp.data(), frames, tapRate_, 1, pts);
                tapPos_.store(pts + (double)frames / tapRate_);
            }
        }
        buf->Unlock();
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) finishInput();
        Sleep(2);
    }
    tapRunning_.store(false);
}
