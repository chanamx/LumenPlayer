#include "mpv_backend.h"
#include "playlist.h"
#include "../third_party/mpv/include/mpv/client.h"
#include <condition_variable>
#include <chrono>
#include <limits>
#include <cstring>
#include <cctype>
#include <cstdarg>

namespace {
using Clock = std::chrono::steady_clock;
using LumenCallback = void (*)(void*, const float*, int, int, int, double);
using LumenRegister = int (*)(mpv_handle*, LumenCallback, void*);
std::mutex moduleMutex;
unsigned moduleUsers=0;

struct MpvApi {
    HMODULE module = nullptr;
    bool registered=false;
    DWORD loadError = ERROR_SUCCESS;
#define MPV_FUNCTION(name) decltype(&mpv_##name) name = nullptr
    MPV_FUNCTION(create); MPV_FUNCTION(initialize); MPV_FUNCTION(terminate_destroy);
    MPV_FUNCTION(set_option); MPV_FUNCTION(set_option_string);
    MPV_FUNCTION(set_property); MPV_FUNCTION(set_property_string);
    MPV_FUNCTION(get_property); MPV_FUNCTION(free_node_contents);
    MPV_FUNCTION(wait_event); MPV_FUNCTION(command_async);
    MPV_FUNCTION(set_wakeup_callback); MPV_FUNCTION(error_string);
    MPV_FUNCTION(request_log_messages);
#undef MPV_FUNCTION
    LumenRegister lumen = nullptr;
    bool Load() {
        std::lock_guard<std::mutex> lock(moduleMutex);
        if(module)return registered;
        // Do not search the current directory or PATH for a playback engine.
        std::wstring path = PathJoin(ExeDir(), L"libmpv-2.dll");
        module = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) {loadError=GetLastError();return false;}
#define LOAD_MPV(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(module,"mpv_" #name)); if (!name) {loadError=GetLastError();return false;}
        LOAD_MPV(create); LOAD_MPV(initialize); LOAD_MPV(terminate_destroy);
        LOAD_MPV(set_option); LOAD_MPV(set_option_string);
        LOAD_MPV(set_property); LOAD_MPV(set_property_string);
        LOAD_MPV(get_property); LOAD_MPV(free_node_contents);
        LOAD_MPV(wait_event); LOAD_MPV(command_async);
        LOAD_MPV(set_wakeup_callback); LOAD_MPV(error_string);
        LOAD_MPV(request_log_messages);
#undef LOAD_MPV
        lumen = reinterpret_cast<LumenRegister>(GetProcAddress(module,
            "mpv_lumen_set_audio_callback"));
        registered=true;++moduleUsers;return true;
    }
    ~MpvApi() {
        if(!module)return;
        std::lock_guard<std::mutex> lock(moduleMutex);
        if(registered&&!--moduleUsers){
            // libavutil can outlive libmpv. Its callback must not retain the
            // Windows import thunk for av_log_default_callback inside libmpv.
            auto avutil=GetModuleHandleW(L"avutil-61.dll");
            using Callback=void(*)(void*,int,const char*,va_list);
            using SetCallback=void(*)(Callback);
            auto set=reinterpret_cast<SetCallback>(GetProcAddress(avutil,"av_log_set_callback"));
            auto callback=reinterpret_cast<Callback>(GetProcAddress(avutil,"av_log_default_callback"));
            if(set&&callback)set(callback);
        }
        FreeLibrary(module);
    }
};

const mpv_node* Field(const mpv_node& node, const char* name) {
    if (node.format != MPV_FORMAT_NODE_MAP || !node.u.list) return nullptr;
    for (int i = 0; i < node.u.list->num; ++i)
        if (!std::strcmp(node.u.list->keys[i], name)) return &node.u.list->values[i];
    return nullptr;
}
std::string Text(const mpv_node* node) {
    return node && node->format == MPV_FORMAT_STRING && node->u.string
        ? node->u.string : "";
}
int64_t Integer(const mpv_node* node, int64_t fallback = 0) {
    if (!node) return fallback;
    if (node->format == MPV_FORMAT_INT64) return node->u.int64;
    if (node->format == MPV_FORMAT_DOUBLE) return (int64_t)node->u.double_;
    return fallback;
}
bool Flag(const mpv_node* node) { return node && node->format == MPV_FORMAT_FLAG && node->u.flag; }
bool IsHdr(const std::string& gamma) {
    return gamma == "pq" || gamma == "hlg" || gamma == "st2084" || gamma == "arib-std-b67";
}
CoreOptions CleanOptions(CoreOptions value) {
    value.targetPeak = std::isfinite(value.targetPeak) && value.targetPeak > 0
        ? Clampd(value.targetPeak, 10, 10000) : 0;
    value.referenceWhite = std::isfinite(value.referenceWhite) && value.referenceWhite > 0
        ? Clampd(value.referenceWhite, 10, 10000) : 0;
    value.peakDecay = Clampd(value.peakDecay, 0, 1000);
    if (value.audioDevice.empty() || value.audioDevice.size() > 2048) value.audioDevice = "auto";
    value.audioLanguages.resize(std::min<size_t>(value.audioLanguages.size(), 256));
    value.subtitleLanguages.resize(std::min<size_t>(value.subtitleLanguages.size(), 256));
    static constexpr const char* tones[]={"auto","spline","bt.2390","bt.2446a","st2094-40","reinhard"};
    static constexpr const char* gamuts[]={"auto","perceptual","relative","saturation","clip","absolute","desaturate","darken","warn","linear"};
    if(std::find(std::begin(tones),std::end(tones),value.toneMapping)==std::end(tones))value.toneMapping="auto";
    if(std::find(std::begin(gamuts),std::end(gamuts),value.gamutMapping)==std::end(gamuts))value.gamutMapping="auto";
    return value;
}
std::string Number(double value) { return FmtUtf8("%.9g", value); }
}

struct MpvBackend::Impl {
    enum class Kind { Open, Close, Pause, Seek, Rate, Volume, Mute, Options,
                      AudioTrack, SubtitleTrack, Subtitle, SubtitleStyle, AB, Frame, Tap, Screenshot };
    struct Request {
        Kind kind;
        uint64_t generation = 0, serial = 0;
        double value = 0, second = 0;
        int64_t id = 0;
        bool flag = false;
        std::wstring path;
    };
    struct PcmBlock {
        std::vector<float> pcm;
        int frames = 0, rate = 0, channels = 0;
        double pts = 0;
        uint64_t generation = 0;
    };

    HWND window;
    mutable std::mutex stateMutex;
    CoreStatus state;
    CoreOptions options;
    Clock::time_point positionAt = Clock::now();
    std::mutex queueMutex;
    std::condition_variable wake;
    std::deque<Request> requests;
    std::atomic<bool> stopping{false}, signaled{false}, scrubbing{false}, initFailed{false};
    std::atomic<bool> wantsPlaying{false}, tapActive{false};
    std::atomic<uint64_t> fileEpoch{0}, seekEpoch{0};
    std::thread worker, consumer;

    std::mutex pcmMutex, deliveryMutex;
    std::condition_variable pcmWake;
    std::deque<PcmBlock> pcmQueue;
    double queuedSeconds = 0;
    size_t queuedBytes = 0;
    PcmSink sink;

    MpvApi api;
    mpv_handle* handle = nullptr; // Accessed only on worker.
    // Only asynchronous stop may cross to the window thread. This short lock
    // also prevents handle destruction or a stale load racing cancellation.
    std::mutex controlMutex;
    mpv_handle* controlHandle = nullptr;
    CoreOptions applied;
    uint64_t activeFile = 0, activeSeek = 0;
    bool seekPending = false, seekAcknowledged = false, seekRestarted = false, fileLoaded = false;
    bool listsDirty = true;
    bool readyLogged = false;
    int64_t activeEntry = -1;
    std::unique_ptr<Request> deferredSeek;
    double seekTarget = 0, workerRate = 1;
    Clock::time_point loadAt = Clock::now();
    std::string currentPath;
    bool lastTap = false, lastPassthrough = false, audioPolicyValid = false;
    bool sharedAudioFallback = false, defaultDeviceFallback = false;
    int audioRecoveries = 0;
    int64_t requestedAudioTrack = -1;
    Clock::time_point audioChangedAt = Clock::now();
    std::wstring outputFallbackReason;
    float volume = 1;
    bool muted = false;
    double loopA = -1, loopB = -1;
    bool subtitleVisible = true;
    double subtitleScale = 1;
    uint64_t commandToken = 1;

    explicit Impl(HWND wnd, CoreOptions settings) : window(wnd), options(CleanOptions(settings)) {
        worker = std::thread([this] { Run(); });
        try {consumer = std::thread([this] { Consume(); });}
        catch(...) {stopping=true;wake.notify_all();if(worker.joinable())worker.join();throw;}
    }
    ~Impl() {
        tapActive = false;
        { std::lock_guard<std::mutex> lock(pcmMutex); stopping = true; }
        StopAsync();
        wake.notify_all(); pcmWake.notify_all();
        if (worker.joinable()) worker.join();
        if (consumer.joinable()) consumer.join();
    }
    void StopAsync() {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (controlHandle) {
            const char* args[] = {"stop", nullptr};
            api.command_async(controlHandle, 0, args);
        }
    }
    void LoadCurrent() {
        std::lock_guard<std::mutex> lock(controlMutex);
        if (!stopping && activeFile == fileEpoch.load() &&
            Command({"loadfile", currentPath, "replace"}) < 0)
            Fail(L"播放核心无法打开此媒体。");
    }
    CoreStatus Snapshot() const {
        std::lock_guard<std::mutex> lock(stateMutex);
        CoreStatus value = state;
        value.position=PositionLocked();
        return value;
    }
    double PositionLocked() const {
        double value=state.position;
        if(state.ready && !state.paused && !state.seeking && !state.buffering && !state.eof) {
            value+=std::chrono::duration<double>(Clock::now()-positionAt).count()*state.rate;
            if(state.duration>0)value=std::min(value,state.duration);
        }
        return value;
    }
    template<class F>auto Read(F field) const {
        std::lock_guard<std::mutex> lock(stateMutex);return field(state);
    }
    struct TimelineState {
        bool ready,paused,seeking,eof,seekable;
        double position,duration;
    };
    TimelineState Timeline() const {
        std::lock_guard<std::mutex> lock(stateMutex);
        return {state.ready,state.paused,state.seeking,state.eof,state.seekable,PositionLocked(),state.duration};
    }
    void Queue(Request request, bool coalesce = false) {
        if (stopping) return;
        if (!request.generation) request.generation = fileEpoch.load();
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (coalesce) {
                for (auto it = requests.begin(); it != requests.end();) {
                    if (it->kind == request.kind && it->generation == request.generation)
                        it = requests.erase(it);
                    else ++it;
                }
            }
            requests.push_back(std::move(request));
        }
        wake.notify_one();
    }
    void ClearPcm() {
        std::lock_guard<std::mutex> lock(pcmMutex);
        pcmQueue.clear(); queuedSeconds = 0; queuedBytes = 0;
    }
    static void Wake(void* context) {
        auto* self = static_cast<Impl*>(context);
        self->signaled = true;
        self->wake.notify_one(); // Reentrant callback: never enter libmpv here.
    }
    static void Audio(void* context, const float* samples, int frames, int rate, int channels, double pts) {
        auto* self = static_cast<Impl*>(context);
        if (self->stopping || !self->tapActive) return;
        bool end = !samples && frames == 0;
        if (!end && (!samples || frames <= 0 || rate < 8000 || rate > 768000 ||
            channels < 1 || channels > 64 || !std::isfinite(pts) || frames > rate * 3)) return;
        size_t bytes = end ? 0 : size_t(frames) * size_t(channels) * sizeof(float);
        if (bytes > 16 * 1024 * 1024) return;
        // The audio filter must never wait for ASR, UI, or a saturated queue.
        std::unique_lock<std::mutex> lock(self->pcmMutex, std::try_to_lock);
        if (!lock.owns_lock()) return;
        double seconds = end ? 0 : double(frames) / rate;
        if (self->queuedSeconds + seconds > 3.0 || self->queuedBytes + bytes > 16 * 1024 * 1024 ||
            self->pcmQueue.size() >= 512) return;
        PcmBlock block;
        block.frames = frames; block.rate = rate; block.channels = channels;
        block.pts = pts; block.generation = self->seekEpoch.load();
        if (!end) {
            try { block.pcm.assign(samples, samples + size_t(frames) * size_t(channels)); }
            catch (...) { return; }
            // Filter reconfiguration can signal EOF before creating its next
            // instance. A continuing stream invalidates such a pending marker.
            for (auto it=self->pcmQueue.begin();it!=self->pcmQueue.end();) {
                if (!it->frames) it=self->pcmQueue.erase(it); else ++it;
            }
        }
        self->queuedSeconds += seconds; self->queuedBytes += bytes;
        self->pcmQueue.push_back(std::move(block));
        lock.unlock(); self->pcmWake.notify_one();
    }
    void Consume() {
        while (!stopping) {
            PcmBlock block;
            bool deliver = false;
            const auto status = Timeline();
            {
                std::unique_lock<std::mutex> lock(pcmMutex);
                if (pcmQueue.empty()) {
                    pcmWake.wait(lock,[this]{return stopping.load() || !pcmQueue.empty();});
                    continue;
                }
                auto& front = pcmQueue.front();
                bool stale = front.generation != seekEpoch.load() || !tapActive;
                bool end = front.frames == 0;
                bool gated = scrubbing || status.seeking || !status.ready ||
                    (status.paused && !status.eof);
                // Discard stale audio, including pre-seek filter output. Leave
                // prefetched audio queued until its media PTS reaches playback.
                stale = stale || (!end && front.pts + double(front.frames) / front.rate < status.position - 0.6);
                if (!stale && (gated || (!end && front.pts > status.position + 0.06) ||
                    (end && !status.eof))) {
                    pcmWake.wait_for(lock, std::chrono::milliseconds(15));
                    continue;
                }
                deliver = !stale;
                queuedSeconds -= end ? 0 : double(front.frames) / front.rate;
                queuedBytes -= front.pcm.size() * sizeof(float);
                block = std::move(front); pcmQueue.pop_front();
            }
            if (deliver) {
                std::lock_guard<std::mutex> lock(deliveryMutex);
                if (sink && tapActive && block.generation == seekEpoch.load() && !scrubbing) {
                    try {
                        sink(block.pcm.empty() ? nullptr : block.pcm.data(), block.frames,
                            block.rate, block.channels, block.frames ? block.pts : status.position);
                    } catch (...) { LogMsg("mpv PCM consumer rejected a block"); }
                }
            }
        }
    }
    bool InitOption(const char* key, const char* value, bool required = false) {
        int result = api.set_option_string(handle, key, value);
        // The local core is built without Lua/JavaScript. Their absent options
        // mean the execution feature does not exist, rather than an unsafe
        // enabled default. Stock builds still have to accept disabling it.
        if(result==MPV_ERROR_OPTION_NOT_FOUND &&
           (!std::strcmp(key,"load-scripts") || !std::strcmp(key,"ytdl")))return true;
        if (result < 0) {
            LogMsg("mpv option %s unavailable (%d)", key, result);
            if (required) Fail(L"播放核心不支持必要的安全或显示选项。");
        }
        return result >= 0;
    }
    bool Property(const char* key, const std::string& value) {
        int result = api.set_property_string(handle, key, value.c_str());
        if (result < 0) LogMsg("mpv property %s rejected (%d)", key, result);
        return result >= 0;
    }
    bool Numeric(const char* key, double value) {
        return api.set_property(handle, key, MPV_FORMAT_DOUBLE, &value) >= 0;
    }
    bool Boolean(const char* key, bool value) {
        int flag = value;
        return api.set_property(handle, key, MPV_FORMAT_FLAG, &flag) >= 0;
    }
    int Command(std::initializer_list<std::string> values, uint64_t token = 0) {
        std::vector<const char*> args;
        for (const auto& value : values) args.push_back(value.c_str());
        args.push_back(nullptr);
        return api.command_async(handle, token ? token : commandToken++, args.data());
    }
    double GetDouble(const char* key, double fallback = 0) {
        double value = fallback;
        if (api.get_property(handle, key, MPV_FORMAT_DOUBLE, &value) < 0 || !std::isfinite(value)) return fallback;
        return value;
    }
    int64_t GetInt(const char* key, int64_t fallback = 0) {
        int64_t value = fallback;
        return api.get_property(handle, key, MPV_FORMAT_INT64, &value) < 0 ? fallback : value;
    }
    bool GetFlag(const char* key, bool fallback = false) {
        int value = fallback;
        return api.get_property(handle, key, MPV_FORMAT_FLAG, &value) < 0 ? fallback : value != 0;
    }
    std::string GetText(const char* key) {
        mpv_node node{};
        if (api.get_property(handle, key, MPV_FORMAT_NODE, &node) < 0) return {};
        std::string value = Text(&node);
        api.free_node_contents(&node);
        return value;
    }
    void Fail(const std::wstring& message) {
        std::lock_guard<std::mutex> lock(stateMutex);
        if(!state.available)initFailed=true;
        state.failed = true; state.loading = false; state.ready = false;
        state.paused = true; state.error = message;
    }
    bool Initialize() {
        if (!api.Load()) {
            LogMsg("mpv runtime load failed Windows error=%lu",static_cast<unsigned long>(api.loadError));
            Fail(L"无法加载播放核心（Windows 错误 "+std::to_wstring(api.loadError)+L"）。");return false;
        }
        handle = api.create();
        if (!handle) { Fail(L"无法创建播放核心。"); return false; }
        bool valid = true;
        valid &= InitOption("config", "no", true);
        valid &= InitOption("load-scripts", "no", true);
        valid &= InitOption("ytdl", "no", true);
        valid &= InitOption("load-unsafe-playlists", "no", true);
        valid &= InitOption("tls-verify", "yes", true);
        for (const char* key : {"input-default-bindings",
             "input-builtin-bindings", "input-vo-keyboard", "input-terminal", "terminal"})
            InitOption(key, "no");
        InitOption("osd-level", "0");
        InitOption("idle", "yes"); InitOption("keep-open", "yes");
        InitOption("keep-open-pause", "yes");
        InitOption("save-position-on-quit", "no");
        valid &= InitOption("vo", "gpu-next", true);
        valid &= InitOption("gpu-api", "d3d11", true);
        InitOption("gpu-context", "d3d11");
        InitOption("ao", "wasapi");
        InitOption("audio-channels", "auto");
        InitOption("sub-ass-override", "no");
        InitOption("sub-ass-use-video-data", "all");
        InitOption("embeddedfonts", "yes");
        InitOption("secondary-sid", "no");
        InitOption("sub-auto", "no"); // The application owns sidecar discovery.
        InitOption("network-timeout", "12");
        InitOption("rtsp-transport", "tcp");
        InitOption("cache", "auto");
        InitOption("demuxer-max-bytes", "64MiB");
        InitOption("demuxer-max-back-bytes", "16MiB");
        // Portable caches stay beside this application instead of the account
        // profile. Disk-backed media buffering is disabled by default.
        auto cache=PathJoin(ExeDir(),L"cache");CreateDirectoryW(cache.c_str(),nullptr);
        cache=PathJoin(cache,L"mpv");CreateDirectoryW(cache.c_str(),nullptr);
        InitOption("gpu-shader-cache-dir",WToUtf8(cache).c_str());
        InitOption("icc-cache-dir",WToUtf8(cache).c_str());
        InitOption("demuxer-cache-dir",WToUtf8(cache).c_str());
        InitOption("window-dragging","no");
        InitOption("cursor-autohide","no");
        InitOption("target-colorspace-hint-mode","target");
        InitOption("target-colorspace-hint-strict","yes");
        int64_t wid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window));
        if (api.set_option(handle, "wid", MPV_FORMAT_INT64, &wid) < 0) valid = false;
        if (!valid) return false;
        api.set_wakeup_callback(handle, Wake, this);
        int result = api.initialize(handle);
        if (result < 0) {
            Fail(L"播放核心初始化失败：" + Utf8ToW(api.error_string(result)));
            return false;
        }
        bool pcm = api.lumen && api.lumen(handle, Audio, this) >= 0;
        api.request_log_messages(handle, "warn");
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            state.available = true; state.supportsPcmTap = pcm;
        }
        ApplyOptions(true);
        ReadLists(true);
        { std::lock_guard<std::mutex> lock(controlMutex); controlHandle = handle; }
        return true;
    }
    void SetShader(const std::wstring& path) {
        std::wstring accepted = path;
        bool invalid = false;
        if (!path.empty()) {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            invalid = path.find(L'\0') != std::wstring::npos ||
                !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
                (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.nFileSizeHigh ||
                data.nFileSizeLow > 4 * 1024 * 1024 || !data.nFileSizeLow;
            if (invalid) accepted.clear();
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            state.shaderError = invalid;
            state.shaderWarning = invalid ? L"着色器文件无效或超过 4 MiB，已恢复默认画质。" : L"";
        }
        std::string text = WToUtf8(accepted);
        mpv_node value{}, entry{};
        mpv_node_list list{};
        value.format = MPV_FORMAT_NODE_ARRAY; value.u.list = &list;
        if (!text.empty()) {
            entry.format = MPV_FORMAT_STRING; entry.u.string = const_cast<char*>(text.c_str());
            list.num = 1; list.values = &entry;
        }
        int result = api.set_property(handle, "glsl-shaders", MPV_FORMAT_NODE, &value);
        if (result < 0) {
            LogMsg("mpv shader configuration rejected (%d)", result);
            list.num = 0; list.values = nullptr;
            api.set_property(handle, "glsl-shaders", MPV_FORMAT_NODE, &value);
            std::lock_guard<std::mutex> lock(stateMutex);
            state.shaderError = true;
            state.shaderWarning = L"播放核心无法应用着色器，已恢复默认画质。";
        }
    }
    void ApplyAudioPolicy(bool force = false) {
        bool canTap;
        { std::lock_guard<std::mutex> lock(stateMutex); canTap = state.supportsPcmTap; }
        bool tap = tapActive && canTap;
        bool bitstream = applied.passthrough && !sharedAudioFallback && !tap && std::abs(workerRate - 1) < 0.00001;
        if (!force && audioPolicyValid && tap == lastTap && bitstream == lastPassthrough) return;
        audioPolicyValid = true; lastTap = tap; lastPassthrough = bitstream;
        // A bitstream cannot pass through a PCM filter or a tempo processor.
        std::string filters = tap ? "lumen" : "";
        if (!bitstream && applied.pitchCorrection) {
            if (!filters.empty()) filters += ",";
            filters += "scaletempo2=min-speed=0.01:max-speed=0";
        }
        if(bitstream) {
            Property("af","");
            Boolean("audio-exclusive",true);
            Property("audio-spdif","ac3,eac3,dts,dts-hd,truehd");
        } else {
            // Stop producing encoded carriers before adding a float PCM tap.
            Property("audio-spdif","");
            Boolean("audio-exclusive",!sharedAudioFallback && applied.exclusiveAudio);
            Property("af",filters);
        }
        std::lock_guard<std::mutex> lock(stateMutex);
        state.audioFallbackReason = applied.passthrough && tap
            ? L"实时字幕需要 PCM 音频，已暂时关闭功放源码输出。"
            : applied.passthrough && std::abs(workerRate - 1) >= 0.00001
            ? L"变速播放需要 PCM 音频，已暂时关闭功放源码输出。" : outputFallbackReason;
    }
    void ApplyOptions(bool force = false) {
        CoreOptions next;
        { std::lock_guard<std::mutex> lock(stateMutex); next = options; }
        if (force || next.preferHardware != applied.preferHardware)
            Property("hwdec", next.preferHardware ? "auto-safe" : "no");
        if (force || next.audioLanguages != applied.audioLanguages) {
            Property("alang", next.audioLanguages);
            if (!force) Property("aid", "auto");
        }
        if (force || next.subtitleLanguages != applied.subtitleLanguages) {
            Property("slang", next.subtitleLanguages);
            if (!force) Property("sid", "auto");
        }
        Boolean("audio-pitch-correction", next.pitchCorrection);
        bool outputChanged = force || next.audioDevice != applied.audioDevice ||
            next.exclusiveAudio != applied.exclusiveAudio || next.passthrough != applied.passthrough;
        if(outputChanged) {
            sharedAudioFallback=defaultDeviceFallback=false;audioRecoveries=0;
            outputFallbackReason.clear();audioChangedAt=Clock::now();
            ReadLists();
            bool known=next.audioDevice=="auto";
            {std::lock_guard<std::mutex> lock(stateMutex);
             for(const auto& device:state.audioDevices)if(device.id==next.audioDevice)known=true;}
            if(!known) {
                defaultDeviceFallback=true;
                outputFallbackReason=L"所选音频设备不可用，已回退到系统默认设备。";
                LogMsg("mpv audio device unavailable; using system default");
            }
            Property("audio-device",defaultDeviceFallback?"auto":next.audioDevice);
        }
        Property("target-colorspace-hint", next.forceSdr ? "no" : "auto");
        Property("target-trc", next.forceSdr ? "bt.1886" : "auto");
        Property("target-prim", next.forceSdr ? "bt.709" : "auto");
        Property("target-peak", next.targetPeak > 0 ? Number(next.targetPeak) : "auto");
        Property("hdr-reference-white", next.referenceWhite > 0 ? Number(next.referenceWhite) : "auto");
        Property("tone-mapping", next.toneMapping);
        Property("gamut-mapping-mode", next.gamutMapping);
        Property("hdr-compute-peak", next.dynamicPeak ? "auto" : "no");
        Numeric("hdr-peak-decay-rate", next.peakDecay);
        if (force || next.shaderPath != applied.shaderPath) SetShader(next.shaderPath);
        bool audioChanged = outputChanged || force || next.exclusiveAudio != applied.exclusiveAudio ||
            next.passthrough != applied.passthrough || next.pitchCorrection != applied.pitchCorrection;
        applied = std::move(next);
        ApplyAudioPolicy(audioChanged);
    }
    bool RecoverAudio() {
        if(audioRecoveries>=2 || requestedAudioTrack==0)return false;
        ++audioRecoveries;sharedAudioFallback=true;defaultDeviceFallback=true;
        outputFallbackReason=L"音频设备或独占输出初始化失败，已回退到默认设备的共享 PCM。";
        LogMsg("mpv audio recovery %d: default shared PCM",audioRecoveries);
        audioChangedAt=Clock::now();
        Property("audio-device","auto");ApplyAudioPolicy(true);
        Property("aid",requestedAudioTrack>0?std::to_string(requestedAudioTrack):"auto");
        ClearPcm();listsDirty=true;return true;
    }
    void ReadLists(bool initial=false) {
        mpv_node tracks{}, devices{}, protocols{};
        std::vector<MediaTrack> trackList;
        std::vector<AudioDevice> deviceList;
        std::vector<std::string> protocolList;
        if (api.get_property(handle, "track-list", MPV_FORMAT_NODE, &tracks) >= 0) {
            if (tracks.format == MPV_FORMAT_NODE_ARRAY && tracks.u.list) {
                for (int i = 0; i < tracks.u.list->num && i < 1024; ++i) {
                    const auto& item = tracks.u.list->values[i];
                    MediaTrack track;
                    track.id = Integer(Field(item, "id"), -1);
                    track.type = Text(Field(item, "type"));
                    track.language = Text(Field(item, "lang"));
                    track.codec = Text(Field(item, "codec"));
                    track.title = Utf8ToW(Text(Field(item, "title")));
                    track.selected = Flag(Field(item, "selected"));
                    track.defaultTrack = Flag(Field(item, "default"));
                    track.forced = Flag(Field(item, "forced"));
                    track.external = Flag(Field(item, "external"));
                    track.channels = (int)Integer(Field(item, "audio-channels"));
                    track.sampleRate = (int)Integer(Field(item, "audio-samplerate"));
                    trackList.push_back(std::move(track));
                }
            }
            api.free_node_contents(&tracks);
        }
        if (api.get_property(handle, "audio-device-list", MPV_FORMAT_NODE, &devices) >= 0) {
            if (devices.format == MPV_FORMAT_NODE_ARRAY && devices.u.list) {
                for (int i = 0; i < devices.u.list->num && i < 256; ++i) {
                    const auto& item = devices.u.list->values[i];
                    deviceList.push_back({Text(Field(item, "name")),
                        Utf8ToW(Text(Field(item, "description")))});
                }
            }
            api.free_node_contents(&devices);
        }
        if (initial && api.get_property(handle, "protocol-list", MPV_FORMAT_NODE, &protocols) >= 0) {
            if (protocols.format == MPV_FORMAT_NODE_ARRAY && protocols.u.list)
                for (int i = 0; i < protocols.u.list->num && i < 512; ++i)
                    protocolList.push_back(Text(&protocols.u.list->values[i]));
            api.free_node_contents(&protocols);
        }
        std::lock_guard<std::mutex> lock(stateMutex);
        if (activeFile == fileEpoch.load()) state.tracks = std::move(trackList);
        state.audioDevices = std::move(deviceList);
        if(initial)state.protocols = std::move(protocolList);
    }
    void Refresh(bool slow) {
        if (!handle) return;
        bool paused = GetFlag("pause", true);
        bool seeking = GetFlag("seeking");
        bool eof = GetFlag("eof-reached");
        bool buffering = GetFlag("paused-for-cache");
        double position = GetDouble("time-pos", 0);
        double duration = GetDouble("duration", 0);
        bool finishedSeek = seekPending && seekAcknowledged && !seeking &&
            (seekRestarted || std::abs(position - seekTarget) < 0.5 || (eof && duration > 0 && seekTarget >= duration - 0.5));
        if (finishedSeek && activeSeek == seekEpoch.load()) {
            seekPending = false;
            // Drop decoder prefetch straddling the seek boundary. New callback
            // blocks carry the new generation and resume at the media clock.
            ClearPcm();
        }
        CorePlaybackState value;
        { std::lock_guard<std::mutex> lock(stateMutex); value = state; }
        if (activeFile != fileEpoch.load()) return;
        value.loading = !fileLoaded && !currentPath.empty() && !value.failed;
        value.ready = fileLoaded && !value.failed;
        value.paused = paused;
        value.seeking = seeking || seekPending || activeSeek != seekEpoch.load();
        value.eof = eof && fileLoaded && !value.seeking;
        value.buffering = buffering;
        value.seekable = GetFlag("seekable");
        value.duration = std::max(0.0, duration);
        if (!value.seeking) value.position = std::max(0.0, position);
        value.rate = GetDouble("speed",workerRate);
        value.abA = loopA; value.abB = loopB;
        if (slow) {
            value.width = (int)GetInt("dwidth"); value.height = (int)GetInt("dheight");
            value.hardwareDecoder = GetText("hwdec-current");
            value.hardware = !value.hardwareDecoder.empty() && value.hardwareDecoder != "no";
            value.videoCodec = GetText("video-codec");
            value.videoOutput = GetText("current-vo");
            value.audioOutput = GetText("current-ao");
            value.audioCodec = GetText("audio-codec-name");
            value.audioDevice = GetText("audio-device");
            value.audioFormat = GetText("audio-out-params/format");
            value.audioSampleRate = (int)GetInt("audio-out-params/samplerate");
            value.audioChannels = (int)GetInt("audio-out-params/channel-count");
            value.hasVideo = GetInt("vid", -1) > 0;
            value.hasAudio = GetInt("aid", -1) > 0;
            value.audioTrack = GetInt("aid", -1);
            value.subtitleTrack = GetInt("sid", -1);
            value.audioLost = value.hasAudio && value.audioSampleRate == 0 &&
                GetText("current-ao").empty() && Clock::now() - loadAt > std::chrono::seconds(1);
            value.hdrInput = IsHdr(GetText("video-params/gamma"));
            value.hdrOutput = IsHdr(GetText("video-target-params/gamma"));
            // mpv exposes average-bpp, not component depth; do not mislabel it.
            auto pixel = GetText("video-params/hw-pixelformat");
            if (pixel.empty()) pixel = GetText("video-params/pixelformat");
            value.inputDepth = pixel.find("p010") != std::string::npos || pixel.find("10") != std::string::npos ? 10 :
                pixel.find("12") != std::string::npos ? 12 : pixel.find("16") != std::string::npos ? 16 :
                pixel.empty() ? 0 : 8;
            value.outputDepth = 0; // No verified swapchain component-depth property.
            value.passthroughActive = value.audioFormat.find("spdif") != std::string::npos ||
                value.audioCodec.find("spdif") != std::string::npos;
            value.actualInfo = value.hardware ? "mpv / " + value.hardwareDecoder : "mpv / software";
            if(!value.videoOutput.empty())value.actualInfo+=" / "+value.videoOutput;
            if(!value.audioOutput.empty())value.actualInfo+=" / "+value.audioOutput;
            if (!value.audioFormat.empty()) value.actualInfo += " / " + value.audioFormat;
            ReadLists();
            bool sourceAudio=false;
            {std::lock_guard<std::mutex> lock(stateMutex);
             for(const auto& track:state.tracks)if(track.type=="audio")sourceAudio=true;}
            bool noOutput=sourceAudio && requestedAudioTrack!=0 && fileLoaded &&
                !eof && GetText("current-ao").empty() &&
                Clock::now()-audioChangedAt>std::chrono::milliseconds(1200);
            if(noOutput && RecoverAudio())value.audioLost=false;
            else if(noOutput)value.audioLost=true;
            if(applied.passthrough && !lastTap && std::abs(workerRate-1)<0.00001 &&
               !value.passthroughActive && !value.audioFormat.empty() && outputFallbackReason.empty())
                value.audioFallbackReason=L"当前音轨或设备未使用压缩直通，实际输出为 PCM。";
            if(value.ready && !readyLogged && (value.hasVideo || value.hasAudio) &&
               (!value.hasVideo || !value.videoOutput.empty()) &&
               (!value.hasAudio || (!value.audioOutput.empty() && value.audioSampleRate>0))) {
                LogMsg("mpv ready hw=%d vo=%s ao=%s audio=%s rate=%d channels=%d tap=%d",
                    int(value.hardware),value.videoOutput.c_str(),value.audioOutput.c_str(),
                    value.audioFormat.c_str(),value.audioSampleRate,value.audioChannels,int(value.supportsPcmTap));
                readyLogged=true;
            }
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            // A UI seek/open may have happened while properties were queried.
            if (activeFile != fileEpoch.load()) return;
            if (activeSeek != seekEpoch.load()) {
                value.position = state.position; value.seeking = true; value.eof = false;
            }
            value.shaderError = state.shaderError; value.shaderWarning = state.shaderWarning;
            if(!state.audioFallbackReason.empty())value.audioFallbackReason=state.audioFallbackReason;
            static_cast<CorePlaybackState&>(state) = std::move(value); positionAt = Clock::now();
        }
        pcmWake.notify_one();
    }
    void Apply(Request& request) {
        if (request.kind != Kind::Options && request.kind != Kind::Tap && request.kind != Kind::Rate &&
            request.kind != Kind::SubtitleStyle &&
            request.kind != Kind::Volume && request.kind != Kind::Mute &&
            request.generation != fileEpoch.load()) return;
        if (request.kind != Kind::Open && request.kind != Kind::Close &&
            request.kind != Kind::Options && request.kind != Kind::Tap && request.kind != Kind::Rate &&
            request.kind != Kind::SubtitleStyle &&
            request.kind != Kind::Volume && request.kind != Kind::Mute &&
            request.generation != activeFile) return;
        switch (request.kind) {
        case Kind::Open:
            activeFile = request.generation;
            // mpv canonicalizes local paths before publishing the path property.
            // Keep the epoch guard in the same representation, including .. .
            currentPath = WToUtf8(IsNetworkMedia(request.path)?request.path:Playlists::Absolute(request.path));
            fileLoaded = false; seekPending = false; activeSeek = seekEpoch.load();
            activeEntry = -1; deferredSeek.reset(); loadAt = Clock::now(); listsDirty = true;
            requestedAudioTrack=-1;audioRecoveries=0;audioChangedAt=Clock::now();readyLogged=false;
            ClearPcm();
            loopA = loopB = -1;
            Property("ab-loop-a", "no"); Property("ab-loop-b", "no");
            Property("aid", "auto"); Property("sid", "auto");
            Property("secondary-sid", "no");
            Boolean("pause", !request.flag);
            ApplyOptions();
            LoadCurrent();
            break;
        case Kind::Close:
            activeFile = request.generation; currentPath.clear(); fileLoaded = false;
            activeEntry = -1; deferredSeek.reset();
            seekPending = false; activeSeek = seekEpoch.load(); loopA = loopB = -1;
            Command({"stop"}); break;
        case Kind::Pause:
            Boolean("pause", request.flag); break;
        case Kind::Seek:
            if (request.serial != seekEpoch.load()) break;
            if (!fileLoaded) { deferredSeek.reset(new Request(request)); break; }
            activeSeek = request.serial; seekTarget = request.value;
            seekPending = true; seekAcknowledged = seekRestarted = false;
            ClearPcm();
            Boolean("pause", request.flag || !wantsPlaying.load());
            if (Command({"seek", Number(seekTarget), "absolute+exact"},
                (uint64_t(1) << 63) | activeSeek) < 0) {
                seekPending = false;
                std::lock_guard<std::mutex> lock(stateMutex);
                state.seeking = false;
            }
            break;
        case Kind::Rate:
            workerRate = request.value;
            ApplyAudioPolicy();
            Numeric("speed", workerRate); break;
        case Kind::Volume:
            volume = (float)request.value; Numeric("volume", volume * 100); break;
        case Kind::Mute:
            muted = request.flag; Boolean("mute", muted); break;
        case Kind::Options: ApplyOptions(); break;
        case Kind::AudioTrack:
        case Kind::SubtitleTrack:
            if(request.kind==Kind::AudioTrack)requestedAudioTrack=request.id;
            Property(request.kind == Kind::AudioTrack ? "aid" : "sid",
                request.id < 0 ? "auto" : request.id == 0 ? "no" : std::to_string(request.id));
            ClearPcm(); break;
        case Kind::Subtitle:
            Command({"sub-add", WToUtf8(request.path), request.flag ? "select" : "auto"}); break;
        case Kind::SubtitleStyle:
            subtitleVisible = request.flag; subtitleScale = request.value;
            Boolean("sub-visibility", subtitleVisible); Numeric("sub-scale", subtitleScale); break;
        case Kind::AB:
            loopA = request.value; loopB = request.second;
            Property("ab-loop-a", loopA < 0 ? "no" : Number(loopA));
            Property("ab-loop-b", loopB < 0 ? "no" : Number(loopB)); break;
        case Kind::Frame:
            wantsPlaying = false; Boolean("pause", true); ClearPcm();
            ++seekEpoch; activeSeek = seekEpoch.load();
            Command({"frame-step", std::to_string(request.id), request.id < 0 ? "seek" : "mute"}); break;
        case Kind::Tap: ClearPcm(); ApplyAudioPolicy(); break;
        case Kind::Screenshot:
            Command({"screenshot-to-file", WToUtf8(request.path), "window"}); break;
        }
    }
    void Events() {
        for (int count = 0; count < 256; ++count) {
            mpv_event* event = api.wait_event(handle, 0);
            if (!event || event->event_id == MPV_EVENT_NONE) break;
            if (event->event_id == MPV_EVENT_START_FILE) {
                auto* start = static_cast<mpv_event_start_file*>(event->data);
                if (start && activeFile == fileEpoch.load() && GetText("path") == currentPath)
                    activeEntry = start->playlist_entry_id;
            } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
                // loadfile is asynchronous. A replaced source's late event must
                // not mark a newly requested source ready.
                if (activeFile == fileEpoch.load() && GetText("path") == currentPath) {
                    fileLoaded = true;
                    listsDirty = true;
                    ClearPcm();
                    if (deferredSeek) {
                        Request request = std::move(*deferredSeek); deferredSeek.reset(); Apply(request);
                    }
                    // Refresh publishes readiness together with dimensions,
                    // audio metadata and the complete track list. Publishing
                    // FILE_LOADED alone lets UI code misclassify it as silent.
                }
            } else if(event->event_id==MPV_EVENT_PLAYBACK_RESTART) {
                // Exact seeks can land past a timestamp gap. The core's restart
                // event confirms completion even when the target is unreachable.
                // Only accept events after the current command reply; older
                // queued restarts and superseded UI seeks must not release PCM.
                if(seekPending && seekAcknowledged && activeSeek==seekEpoch.load())seekRestarted=true;
            } else if(event->event_id==MPV_EVENT_SEEK && !seekPending) {
                // A-B loops and frame-back-step also reset the decoded audio
                // timeline, even though the UI did not issue a time seek.
                // Do not invalidate a UI seek arriving between the check and increment.
                uint64_t expected=activeSeek;
                if(seekEpoch.compare_exchange_strong(expected,expected+1)) {
                    activeSeek=expected+1;ClearPcm();
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.seekGeneration=activeSeek;
                }
            } else if (event->event_id == MPV_EVENT_END_FILE) {
                auto* end = static_cast<mpv_event_end_file*>(event->data);
                if (end && end->reason == MPV_END_FILE_REASON_ERROR && activeFile == fileEpoch.load() &&
                    (end->playlist_entry_id == activeEntry || (!fileLoaded && activeEntry < 0))) {
                    if(end->error==MPV_ERROR_AO_INIT_FAILED && RecoverAudio()) {
                        // Pure audio can end when the last output is removed.
                        // Retry after that connection is closed, never beside it.
                        LoadCurrent();
                    } else Fail(L"媒体加载或播放失败：" + Utf8ToW(api.error_string(end->error)));
                }
            } else if (event->event_id == MPV_EVENT_COMMAND_REPLY &&
                       (event->reply_userdata & (uint64_t(1) << 63))) {
                uint64_t serial = event->reply_userdata & ~(uint64_t(1) << 63);
                if (serial == activeSeek && serial == seekEpoch.load()) {
                    seekAcknowledged = event->error >= 0;
                    if (event->error < 0) {
                        seekPending = false;
                        std::lock_guard<std::mutex> lock(stateMutex); state.seeking = false;
                    }
                }
            } else if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
                auto* log = static_cast<mpv_event_log_message*>(event->data);
                // Never copy arbitrary engine messages: URLs may contain secrets.
                std::string message = log && log->text ? log->text : "";
                std::transform(message.begin(), message.end(), message.begin(),
                    [](unsigned char c) { return char(std::tolower(c)); });
                if (message.find("shader") != std::string::npos &&
                    (message.find("error") != std::string::npos || message.find("failed") != std::string::npos)) {
                    SetShader(L"");
                    std::lock_guard<std::mutex> lock(stateMutex);
                    state.shaderError = true;
                    state.shaderWarning = L"着色器编译失败，已恢复默认画质。";
                }
            } else if (event->event_id == MPV_EVENT_SHUTDOWN && !stopping) {
                Fail(L"播放核心已意外退出。");
            }
        }
    }
    void Run() {
        if (Initialize()) {
            auto lastSlow = Clock::time_point{};
            while (!stopping) {
                Events();
                std::deque<Request> work;
                { std::lock_guard<std::mutex> lock(queueMutex); work.swap(requests); }
                for (auto& request : work) { if (stopping) break; Apply(request); }
                Events();
                auto now = Clock::now();
                bool slow = listsDirty || now - lastSlow >= std::chrono::milliseconds(250);
                Refresh(slow);
                if (slow) { lastSlow = now; listsDirty = false; }
                std::unique_lock<std::mutex> lock(queueMutex);
                wake.wait_for(lock, std::chrono::milliseconds(35), [this] {
                    return stopping.load() || !requests.empty() || signaled.exchange(false);
                });
            }
        } else {
            std::lock_guard<std::mutex> lock(stateMutex);
            initFailed=true;state.failed=true;state.loading=false;state.ready=false;
            if(state.error.empty())state.error=L"播放核心初始化失败。";
        }
        { std::lock_guard<std::mutex> lock(controlMutex); controlHandle = nullptr; }
        if (handle) {
            if (api.lumen) api.lumen(handle, nullptr, nullptr);
            api.set_wakeup_callback(handle, nullptr, nullptr);
            api.terminate_destroy(handle); handle = nullptr;
        }
    }
};

MpvBackend::MpvBackend(HWND wnd, const CoreOptions& options) : impl_(new Impl(wnd, options)) {}
MpvBackend::~MpvBackend() = default;
bool MpvBackend::Available() { return FileExists(PathJoin(ExeDir(), L"libmpv-2.dll")); }
bool MpvBackend::Open(const std::wstring& path, bool autoplay) {
    if (path.empty() || path.find(L'\0') != std::wstring::npos || !Available()) return false;
    { std::lock_guard<std::mutex> lock(impl_->stateMutex);
      if (impl_->initFailed || (impl_->state.failed && !impl_->state.available)) return false; }
    uint64_t generation = ++impl_->fileEpoch;
    ++impl_->seekEpoch; impl_->wantsPlaying = autoplay; impl_->scrubbing = false;
    impl_->ClearPcm();
    {
        std::lock_guard<std::mutex> lock(impl_->stateMutex);
        if(impl_->initFailed)return false;
        auto devices = std::move(impl_->state.audioDevices);
        auto protocols = std::move(impl_->state.protocols);
        bool available = impl_->state.available, pcm = impl_->state.supportsPcmTap;
        impl_->state = {};
        impl_->state.available = available; impl_->state.supportsPcmTap = pcm;
        impl_->state.audioDevices = std::move(devices); impl_->state.protocols = std::move(protocols);
        impl_->state.loading = true; impl_->state.paused = !autoplay;
        impl_->state.fileGeneration = generation; impl_->state.seekGeneration = impl_->seekEpoch;
        impl_->positionAt = Clock::now();
    }
    Impl::Request request{Impl::Kind::Open}; request.path = path;
    request.flag = autoplay; request.generation = generation;
    impl_->Queue(std::move(request)); return true;
}
void MpvBackend::Close() {
    uint64_t generation = ++impl_->fileEpoch;
    ++impl_->seekEpoch; impl_->wantsPlaying = false; impl_->scrubbing = false;
    impl_->ClearPcm();
    {
        std::lock_guard<std::mutex> lock(impl_->stateMutex);
        bool available = impl_->state.available, pcm = impl_->state.supportsPcmTap;
        auto devices = std::move(impl_->state.audioDevices);
        auto protocols = std::move(impl_->state.protocols);
        impl_->state = {}; impl_->state.available = available; impl_->state.supportsPcmTap = pcm;
        impl_->state.audioDevices = std::move(devices); impl_->state.protocols = std::move(protocols);
        impl_->state.fileGeneration = generation;
    }
    Impl::Request request{Impl::Kind::Close}; request.generation = generation; impl_->Queue(request);
    impl_->StopAsync();
}
bool MpvBackend::CancelLoading() {
    if (!Loading()) return false;
    Close(); return true;
}
void MpvBackend::Play() {
    impl_->wantsPlaying = true; impl_->scrubbing = false;
    { std::lock_guard<std::mutex> lock(impl_->stateMutex); impl_->state.paused = false; }
    Impl::Request request{Impl::Kind::Pause}; request.flag = false; impl_->Queue(request, true);
}
void MpvBackend::Pause() {
    impl_->wantsPlaying = false;
    { std::lock_guard<std::mutex> lock(impl_->stateMutex); impl_->state.paused = true; }
    Impl::Request request{Impl::Kind::Pause}; request.flag = true; impl_->Queue(request, true);
}
void MpvBackend::Seek(double sec) {
    if (!std::isfinite(sec)) return;
    const auto status = impl_->Timeline();
    if (status.ready && !status.seekable) return;
    sec = std::max(0.0, sec); if (status.duration > 0) sec = std::min(sec, status.duration);
    uint64_t serial = ++impl_->seekEpoch; impl_->scrubbing = false;
    impl_->ClearPcm();
    { std::lock_guard<std::mutex> lock(impl_->stateMutex);
      impl_->state.position = sec; impl_->state.seeking = true; impl_->state.eof = false;
      impl_->state.seekGeneration = serial; }
    Impl::Request request{Impl::Kind::Seek}; request.serial = serial; request.value = sec;
    impl_->Queue(request, true);
}
void MpvBackend::PreviewSeek(double sec) {
    if (!std::isfinite(sec)) return;
    const auto status = impl_->Timeline(); if (status.ready && !status.seekable) return;
    sec = std::max(0.0, sec); if (status.duration > 0) sec = std::min(sec, status.duration);
    uint64_t serial = ++impl_->seekEpoch; impl_->scrubbing = true; impl_->ClearPcm();
    { std::lock_guard<std::mutex> lock(impl_->stateMutex);
      impl_->state.position = sec; impl_->state.seeking = true; impl_->state.eof = false;
      impl_->state.seekGeneration = serial; }
    Impl::Request request{Impl::Kind::Seek}; request.serial = serial; request.value = sec; request.flag = true;
    impl_->Queue(request, true);
}
bool MpvBackend::SeekReady() const { return impl_->Read([](const CoreStatus& s){return !s.seeking;}); }
bool MpvBackend::SetRate(double rate) {
    if (!std::isfinite(rate) || rate < 0.1 || rate > 12) return false;
    { std::lock_guard<std::mutex> lock(impl_->stateMutex); impl_->state.rate = rate; }
    Impl::Request request{Impl::Kind::Rate}; request.value = rate; impl_->Queue(request, true); return true;
}
double MpvBackend::Position() const { return impl_->Timeline().position; }
double MpvBackend::Duration() const { return impl_->Read([](const CoreStatus& s){return s.duration;}); }
bool MpvBackend::IsPlaying() const { return impl_->wantsPlaying && impl_->Read([](const CoreStatus& s){return !s.failed && !s.eof;}); }
bool MpvBackend::IsEof() const { return impl_->Read([](const CoreStatus& s){return s.eof;}); }
int MpvBackend::Width() const { return impl_->Read([](const CoreStatus& s){return s.width;}); }
int MpvBackend::Height() const { return impl_->Read([](const CoreStatus& s){return s.height;}); }
bool MpvBackend::HasAudio() const { return impl_->Read([](const CoreStatus& s){return s.hasAudio;}); }
bool MpvBackend::AudioLost() const { return impl_->Read([](const CoreStatus& s){return s.audioLost;}); }
void MpvBackend::SetVolume(float volume) {
    Impl::Request request{Impl::Kind::Volume}; request.value = Clampf(volume, 0, 1); impl_->Queue(request, true);
}
void MpvBackend::SetMuted(bool muted) {
    Impl::Request request{Impl::Kind::Mute}; request.flag = muted; impl_->Queue(request, true);
}
bool MpvBackend::Failed() const { return impl_->Read([](const CoreStatus& s){return s.failed;}); }
bool MpvBackend::Loading() const { return impl_->Read([](const CoreStatus& s){return s.loading;}); }
bool MpvBackend::Live() const { return impl_->Read([](const CoreStatus& s){return s.ready && !s.seekable;}); }
bool MpvBackend::HardwareDecoding() const { return impl_->Read([](const CoreStatus& s){return s.hardware;}); }
bool MpvBackend::HardwareReady() const { return impl_->Read([](const CoreStatus& s){return s.ready && s.hasVideo;}); }
bool MpvBackend::ConfigureVideo(const std::wstring& shaderPath, bool forceSdr) {
    auto options = Options(); options.shaderPath = shaderPath; options.forceSdr = forceSdr;
    SetOptions(options); return true;
}
VideoRenderInfo MpvBackend::RenderInfo() const {
    return impl_->Read([this](const CoreStatus& s) {
        VideoRenderInfo info{};
        info.inputDepth = s.inputDepth; info.outputDepth = s.outputDepth;
        info.hdrInput = s.hdrInput; info.hdrOutput = s.hdrOutput;
        info.shaderError = s.shaderError;
        info.shader = !impl_->options.shaderPath.empty() && !s.shaderError;
        std::strncpy(info.adapter, "mpv D3D11 / libplacebo", sizeof(info.adapter) - 1);
        return info;
    });
}
bool MpvBackend::RecoverVideo(bool) { return false; }
CoreStatus MpvBackend::Status() const { return impl_->Snapshot(); }
CoreOptions MpvBackend::Options() const { std::lock_guard<std::mutex> lock(impl_->stateMutex); return impl_->options; }
void MpvBackend::SetOptions(const CoreOptions& options) {
    { std::lock_guard<std::mutex> lock(impl_->stateMutex); impl_->options = CleanOptions(options); }
    impl_->Queue({Impl::Kind::Options}, true);
}
bool MpvBackend::SelectAudioTrack(int64_t id) {
    if (id < -1) return false;
    if (id > 0) { auto s = Status(); if (!std::any_of(s.tracks.begin(), s.tracks.end(),
        [id](const MediaTrack& t) { return t.type == "audio" && t.id == id; })) return false; }
    Impl::Request request{Impl::Kind::AudioTrack}; request.id = id; impl_->Queue(request, true); return true;
}
bool MpvBackend::SelectSubtitleTrack(int64_t id) {
    if (id < -1) return false;
    if (id > 0) { auto s = Status(); if (!std::any_of(s.tracks.begin(), s.tracks.end(),
        [id](const MediaTrack& t) { return t.type == "sub" && t.id == id; })) return false; }
    Impl::Request request{Impl::Kind::SubtitleTrack}; request.id = id; impl_->Queue(request, true); return true;
}
void MpvBackend::AddSubtitle(const std::wstring& path, bool select) {
    if (path.empty() || path.find(L'\0') != std::wstring::npos) return;
    Impl::Request request{Impl::Kind::Subtitle}; request.path = path; request.flag = select; impl_->Queue(std::move(request));
}
void MpvBackend::SetSubtitlePresentation(bool visible, double scale) {
    Impl::Request request{Impl::Kind::SubtitleStyle}; request.flag = visible;
    request.value = Clampd(scale, 0.25, 4); impl_->Queue(request, true);
}
void MpvBackend::SetABLoop(double a, double b) {
    if (!std::isfinite(a) || !std::isfinite(b)) return;
    if (a >= 0 && b >= 0 && b <= a) { a = -1; b = -1; }
    Impl::Request request{Impl::Kind::AB}; request.value = a; request.second = b; impl_->Queue(request, true);
}
void MpvBackend::FrameStep(int frames) {
    if (!frames || !impl_->Read([](const CoreStatus& s){return s.ready;})) return;
    Impl::Request request{Impl::Kind::Frame}; request.id = std::max(-100, std::min(frames, 100)); impl_->Queue(request);
}
void MpvBackend::SetPcmSink(PcmSink sink) {
    bool active = bool(sink);
    impl_->tapActive = false;
    { std::lock_guard<std::mutex> lock(impl_->deliveryMutex); impl_->sink = std::move(sink); }
    impl_->ClearPcm(); impl_->tapActive = active;
    impl_->Queue({Impl::Kind::Tap}, true); impl_->pcmWake.notify_one();
}
bool MpvBackend::SupportsPcmTap() const { return impl_->Read([](const CoreStatus& s){return s.supportsPcmTap;}); }
bool MpvBackend::Screenshot(const std::wstring& path) {
    if (path.empty() || path.find(L'\0') != std::wstring::npos || !Status().ready) return false;
    Impl::Request request{Impl::Kind::Screenshot}; request.path = path; impl_->Queue(std::move(request)); return true;
}
