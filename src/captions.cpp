#include "captions.h"
#include "caption_models.h"
#include "moonshine_api.h"
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>
#include <exception>
#include <set>
#include <sherpa-onnx/c-api/c-api.h>

// ===========================================================================
// Zipformer CTC INT8 (bundled streaming model, CPU single thread)
// ===========================================================================
static const wchar_t* kModelDirName = L"sherpa-onnx-streaming-zipformer-ctc-zh-int8-2025-06-30";

struct LiveCaptions::Impl {
    // ---- shared ----
    std::atomic<bool> running{false};
    std::mutex rmutex;
    std::wstring partial;
    std::vector<SubCue> finals;
    double lastEnd = 0;
    std::atomic<double> fedSec{0};

    // ---- sherpa-onnx C API (bundled, matching DLLs) ----
    HMODULE ortDll = nullptr, sherpaDll = nullptr;
    decltype(&SherpaOnnxCreateOnlineRecognizer) createRecognizer = nullptr;
    decltype(&SherpaOnnxDestroyOnlineRecognizer) destroyRecognizer = nullptr;
    decltype(&SherpaOnnxCreateOnlineStream) createStream = nullptr;
    decltype(&SherpaOnnxDestroyOnlineStream) destroyStream = nullptr;
    decltype(&SherpaOnnxOnlineStreamAcceptWaveform) accept = nullptr;
    decltype(&SherpaOnnxIsOnlineStreamReady) ready = nullptr;
    decltype(&SherpaOnnxDecodeOnlineStream) decode = nullptr;
    decltype(&SherpaOnnxGetOnlineStreamResult) result = nullptr;
    decltype(&SherpaOnnxDestroyOnlineRecognizerResult) destroyResult = nullptr;
    decltype(&SherpaOnnxOnlineStreamReset) reset = nullptr;
    decltype(&SherpaOnnxOnlineStreamInputFinished) finish = nullptr;
    decltype(&SherpaOnnxOnlineStreamIsEndpoint) endpoint = nullptr;
    const SherpaOnnxOnlineRecognizer* recognizer = nullptr;
    const SherpaOnnxOnlineStream* stream = nullptr;
    HMODULE moonDll=nullptr;
    int32_t moonHandle=-1,moonStream=-1;
    int32_t (*moonLoad)(const char*,uint32_t,const MoonOption*,uint64_t,int32_t)=nullptr;
    void (*moonFree)(int32_t)=nullptr;
    int32_t (*moonCreateStream)(int32_t,uint32_t)=nullptr;
    int32_t (*moonFreeStream)(int32_t,int32_t)=nullptr;
    int32_t (*moonStart)(int32_t,int32_t)=nullptr;
    int32_t (*moonStop)(int32_t,int32_t)=nullptr;
    int32_t (*moonAdd)(int32_t,int32_t,const float*,uint64_t,int32_t,uint32_t)=nullptr;
    int32_t (*moonTranscribe)(int32_t,int32_t,uint32_t,MoonTranscript**)=nullptr;
    std::thread worker;
    std::mutex qmutex;
    std::condition_variable queueChanged;
    std::deque<float> queue;
    double queuePts = 0, nextPts = 0;
    bool hasPts = false, resetPending = false, finishPending = false, inputEnded = false;
    uint64_t generation = 0;
    static const size_t kCapSamples = 16000 * 2;   // Bound latency on overloaded CPUs.
    double resPos = 0;      // any rate -> 16 kHz
    float resLast = 0;
    bool resInit = false;


};

// ---------------------------------------------------------------------------
// CTC timestamps are segment-relative. v1.13.8 exposes the segment's start_time
// only in JSON, so read that one numeric field (not the recognized text).
// ---------------------------------------------------------------------------
static double SegmentStart(const char* json) {
    const char* p = json ? strstr(json, "\"start_time\"") : nullptr;
    p = p ? strchr(p, ':') : nullptr;
    const double value = p ? std::strtod(p + 1, nullptr) : 0;
    return std::isfinite(value) && value >= 0 ? value : 0;
}

static void PublishCtcResult(LiveCaptions::Impl* im, uint64_t generation,
                             double origin, double end, bool final) {
    const auto* result = im->result(im->recognizer, im->stream);
    if (!result) return;
    {
        std::lock_guard<std::mutex> ql(im->qmutex);
        if (generation == im->generation) {
            std::wstring text = Utf8ToW(result->text ? result->text : "");
            std::lock_guard<std::mutex> rl(im->rmutex);
            if (!final) im->partial = text;
            else {
                if (!text.empty() && end > origin) {
                    double start = im->lastEnd, stop = end;
                    if (result->count > 0 && result->timestamps) {
                        const double segment = origin + SegmentStart(result->json);
                        start = segment + result->timestamps[0];
                        stop = segment + result->timestamps[result->count - 1] + 0.2;
                    }
                    start = Clampd(start, origin, std::max(origin, end - 0.04));
                    stop = Clampd(stop, start + 0.04, end);
                    im->finals.push_back({start, stop, std::move(text)});
                    im->lastEnd = stop;
                }
                im->partial.clear();
            }
        }
    }
    im->destroyResult(result);
}

static void ZipformerWorker(LiveCaptions::Impl* im) {
    std::vector<float> buf;
    double origin = 0, processedEnd = 0;
    bool finished = false;
    for (;;) {
        uint64_t generation;
        bool flush = false, stopping = false;
        {
            std::unique_lock<std::mutex> lk(im->qmutex);
            im->queueChanged.wait(lk, [&] { return !im->running.load() || im->queue.size() >= 1600 || im->resetPending || im->finishPending; });
            if (im->resetPending) {
                // A seek needs a new stream: Reset alone retains old feature frames.
                im->destroyStream(im->stream);
                im->stream = im->createStream(im->recognizer);
                origin = processedEnd = im->queuePts;
                finished = false;
                im->resetPending = false;
                std::lock_guard<std::mutex> rl(im->rmutex);
                im->partial.clear();
                im->lastEnd = origin;
            }
            if (!im->stream) { im->running.store(false); LogMsg("Zipformer stream creation failed"); break; }
            generation = im->generation;
            const size_t n = std::min<size_t>(3200, im->queue.size());
            buf.resize(n);                  // Up to 200 ms of normalized mono PCM.
            for (size_t i = 0; i < n; ++i) {
                buf[i] = im->queue.front();
                im->queue.pop_front();
            }
            processedEnd = im->queuePts + (double)n / 16000;
            im->queuePts = processedEnd;
            stopping = !im->running.load() && im->queue.empty();
            flush = (im->finishPending || stopping) && im->queue.empty();
            if (flush) im->finishPending = false;
        }
        if (!finished) {
            if (!buf.empty()) im->accept(im->stream, 16000, buf.data(), (int)buf.size());
            while (im->ready(im->recognizer, im->stream)) im->decode(im->recognizer, im->stream);
            if (flush) {
                // Supply right context and decode all remaining frames. Synthetic
                // silence never extends exported media timestamps.
                const float tail[12800] = {};
                im->accept(im->stream, 16000, tail, 12800);
                im->finish(im->stream);
                while (im->ready(im->recognizer, im->stream)) im->decode(im->recognizer, im->stream);
                PublishCtcResult(im, generation, origin, processedEnd, true);
                finished = true;
            } else {
                const bool endpoint = im->endpoint(im->recognizer, im->stream) != 0;
                PublishCtcResult(im, generation, origin, processedEnd, endpoint);
                if (endpoint) im->reset(im->recognizer, im->stream);
            }
        }
        if (stopping) break;
    }
}

static bool PublishMoonResult(LiveCaptions::Impl* im,uint64_t generation,double origin,double end,
                              bool flush,std::set<uint64_t>& completed) {
    MoonTranscript* transcript=nullptr;
    if(im->moonTranscribe(im->moonHandle,im->moonStream,flush?1:0,&transcript)!=0)return false;
    if(!transcript)return true;
    if(transcript->line_count>100000 || (transcript->line_count&&!transcript->lines))return false;
    std::vector<const MoonLine*> lines;
    for(uint64_t i=0;i<transcript->line_count;++i)if(transcript->lines[i].text)lines.push_back(&transcript->lines[i]);
    std::sort(lines.begin(),lines.end(),[](const auto* a,const auto* b){return a->start_time<b->start_time;});
    std::lock_guard<std::mutex> q(im->qmutex);if(generation!=im->generation)return true;
    std::lock_guard<std::mutex> r(im->rmutex);std::wstring partial;
    for(const auto* line:lines){
        if(completed.count(line->id))continue;
        std::wstring text=Utf8ToW(line->text);if(text.empty())continue;
        if(line->is_complete || flush){
            if(end>origin && std::isfinite(line->start_time) && std::isfinite(line->duration)){
                double start=Clampd(origin+line->start_time,origin,std::max(origin,end-.04));
                double stop=Clampd(origin+line->start_time+std::max(.04f,line->duration),start+.04,end);
                start=std::max(start,im->lastEnd);
                if(stop>start){im->finals.push_back({start,stop,std::move(text)});im->lastEnd=stop;}
            }
            completed.insert(line->id);
        }else {if(!partial.empty())partial+=L" ";partial+=text;}
    }
    im->partial=std::move(partial);return true;
}

static void MoonshineWorker(LiveCaptions::Impl* im) {
    std::vector<float> audio;double origin=0,processedEnd=0;bool finished=false;std::set<uint64_t> completed;
    for(;;){uint64_t generation;bool flush=false,stopping=false;
        {std::unique_lock<std::mutex> q(im->qmutex);
            im->queueChanged.wait(q,[&]{return !im->running.load()||im->queue.size()>=1600||im->resetPending||im->finishPending;});
            if(im->resetPending){
                if(im->moonStream>=0)im->moonFreeStream(im->moonHandle,im->moonStream);
                im->moonStream=im->moonCreateStream(im->moonHandle,0);
                if(im->moonStream<0 || im->moonStart(im->moonHandle,im->moonStream)!=0){im->running.store(false);break;}
                origin=processedEnd=im->queuePts;finished=false;completed.clear();im->resetPending=false;
                std::lock_guard<std::mutex> r(im->rmutex);im->partial.clear();im->lastEnd=origin;
            }
            generation=im->generation;size_t n=std::min<size_t>(3200,im->queue.size());audio.resize(n);
            for(size_t i=0;i<n;++i){audio[i]=im->queue.front();im->queue.pop_front();}
            processedEnd=im->queuePts+double(n)/16000;im->queuePts=processedEnd;
            stopping=!im->running.load()&&im->queue.empty();flush=(im->finishPending||stopping)&&im->queue.empty();if(flush)im->finishPending=false;
        }
        if(!finished){
            if(!audio.empty() && im->moonAdd(im->moonHandle,im->moonStream,audio.data(),audio.size(),16000,0)!=0){im->running.store(false);break;}
            if(flush && im->moonStop(im->moonHandle,im->moonStream)!=0){im->running.store(false);break;}
            if(!PublishMoonResult(im,generation,origin,processedEnd,flush,completed)){LogMsg("Moonshine transcription failed");im->running.store(false);break;}
            finished=flush;
        }
        if(stopping)break;
    }
}

LiveCaptions::LiveCaptions() : impl_(new Impl()) {}
LiveCaptions::~LiveCaptions() { Stop(); delete impl_; }

bool LiveCaptions::Running() const { return active_.load() && impl_->running.load(); }

bool LiveCaptions::Configure(const std::string& id,const std::wstring& directory){
    const auto* model=FindCaptionModel(id);if(!model||model->Translation()||active_.load()||impl_->worker.joinable())return false;
    selectedModel_=id;configuredDirectory_=directory;return Probe();
}
std::wstring LiveCaptions::ModelName() const {const auto* m=FindCaptionModel(selectedModel_);return m?m->name:L"字幕模型";}
uint64_t LiveCaptions::TimelineGeneration(){std::lock_guard<std::mutex> lock(impl_->qmutex);return impl_->generation;}

bool LiveCaptions::Probe() {
    dllPath_.clear();
    packDir_.clear();
    modelPath_.clear();
    const auto* chosen=FindCaptionModel(selectedModel_);if(!chosen)return false;
    locale_=chosen->language=="zh"?L"cmn-Hans-CN":Utf8ToW(chosen->language);

    std::wstring dir = ExeDir();
    std::wstring dll = PathJoin(dir, chosen->engine=="moonshine"?L"moonshine.dll":L"sherpa-onnx-c-api.dll");
    std::wstring model = configuredDirectory_.empty()?PathJoin(dir,kModelDirName):configuredDirectory_;
    if(chosen->engine=="moonshine"){
        bool ok=FileExists(dll)&&FileExists(PathJoin(dir,L"onnxruntime.dll"))&&FileExists(PathJoin(dir,L"onnxruntime_providers_shared.dll"));
        for(const auto& f:chosen->files)ok=ok&&FileExists(PathJoin(model,f.path));
        if(!ok)return false;dllPath_=dll;packDir_=model;modelPath_=PathJoin(model,L"encoder.ort");return true;
    }
    const std::wstring optimized = PathJoin(model, L"model.int8.ort");
    const std::wstring legacy = PathJoin(model, L"model.int8.onnx");
    const std::wstring selected = FileExists(optimized) ? optimized : legacy;
    DWORD attr = GetFileAttributesW(model.c_str());
    bool modelOk = attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) &&
        FileExists(selected) && FileExists(PathJoin(model, L"tokens.txt"));
    bool runtimeOk = FileExists(dll) && FileExists(PathJoin(dir, L"onnxruntime.dll")) &&
        FileExists(PathJoin(dir, L"onnxruntime_providers_shared.dll"));
    if (!runtimeOk || !modelOk) {
        LogMsg("Zipformer missing: runtime=%d model=%d", (int)runtimeOk, (int)modelOk);
        return false;
    }
    dllPath_ = dll;
    packDir_ = model;
    modelPath_ = selected;
    return true;

}

bool LiveCaptions::Start(const std::wstring&) {
    if (active_.load()) return Running();
    try {
        const auto* model=FindCaptionModel(selectedModel_);
        if (model && (model->engine=="moonshine"?StartMoonshine():StartZipformer())) return true;
    } catch (const std::exception& error) {
        LogMsg("Caption startup failed: %s", error.what());
    }
    Stop();
    return false;
}

bool LiveCaptions::StartZipformer() {
    Impl* im = impl_;
    if ((!Available() || !FileExists(modelPath_)) && !Probe()) return false;
    const double began = QpcNow();

    const DWORD search = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
    im->ortDll = LoadLibraryExW(PathJoin(ExeDir(), L"onnxruntime.dll").c_str(), nullptr, search);
    if (!im->ortDll) { LogMsg("LoadLibrary(onnxruntime) failed %lu", GetLastError()); return false; }
    im->sherpaDll = LoadLibraryExW(dllPath_.c_str(), nullptr, search);
    if (!im->sherpaDll) { LogMsg("LoadLibrary(sherpa-onnx) failed %lu", GetLastError()); return false; }
#define LOAD_CTC(member, name) im->member = reinterpret_cast<decltype(im->member)>(GetProcAddress(im->sherpaDll, #name)); if (!im->member) { LogMsg("Missing CTC export: " #name); return false; }
    LOAD_CTC(createRecognizer, SherpaOnnxCreateOnlineRecognizer)
    LOAD_CTC(destroyRecognizer, SherpaOnnxDestroyOnlineRecognizer)
    LOAD_CTC(createStream, SherpaOnnxCreateOnlineStream)
    LOAD_CTC(destroyStream, SherpaOnnxDestroyOnlineStream)
    LOAD_CTC(accept, SherpaOnnxOnlineStreamAcceptWaveform)
    LOAD_CTC(ready, SherpaOnnxIsOnlineStreamReady)
    LOAD_CTC(decode, SherpaOnnxDecodeOnlineStream)
    LOAD_CTC(result, SherpaOnnxGetOnlineStreamResult)
    LOAD_CTC(destroyResult, SherpaOnnxDestroyOnlineRecognizerResult)
    LOAD_CTC(reset, SherpaOnnxOnlineStreamReset)
    LOAD_CTC(finish, SherpaOnnxOnlineStreamInputFinished)
    LOAD_CTC(endpoint, SherpaOnnxOnlineStreamIsEndpoint)
#undef LOAD_CTC
    const double loaded = QpcNow();
    const std::string model = WToUtf8(modelPath_);
    const std::string tokens = WToUtf8(PathJoin(packDir_, L"tokens.txt"));
    // Keep allocator caches from retaining an extra arena after each chunk.
    // Missing, unreadable or modified packaged options retain sherpa defaults.
    const auto cpuOptions = PathJoin(ExeDir(), L"caption-cpu.conf");
    std::string provider = "cpu";
    if (FILE* file = _wfopen(cpuOptions.c_str(), L"rb")) {
        char bytes[64]{};
        const size_t count = fread(bytes, 1, sizeof(bytes), file);
        fclose(file);
        static constexpr char expected[] = "EnableCpuMemArena=0\n";
        if (count == sizeof(expected) - 1 && !memcmp(bytes, expected, count))
            provider += ":" + WToUtf8(cpuOptions);
    }
    SherpaOnnxOnlineRecognizerConfig config{};
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.zipformer2_ctc.model = model.c_str();
    config.model_config.tokens = tokens.c_str();
    config.model_config.num_threads = 1;
    config.model_config.provider = provider.c_str();
    config.model_config.modeling_unit = "cjkchar";
    config.decoding_method = "greedy_search";
    config.enable_endpoint = 1;
    config.rule1_min_trailing_silence = 2.4f;
    config.rule2_min_trailing_silence = 0.8f;
    config.rule3_min_utterance_length = 15.0f;
    im->recognizer = im->createRecognizer(&config);
    if (!im->recognizer) { LogMsg("Zipformer model initialization failed"); return false; }
    const double initialized = QpcNow();
    im->stream = im->createStream(im->recognizer);
    if (!im->stream) return false;

    {
        std::lock_guard<std::mutex> lk(im->qmutex);
        im->queue.clear();
        im->queuePts = im->nextPts = 0;
        im->hasPts = im->resetPending = im->finishPending = im->inputEnded = false;
        im->generation = 0;
    }
    {
        std::lock_guard<std::mutex> lk(im->rmutex);
        im->partial.clear();
        im->finals.clear();
        im->lastEnd = 0;
    }
    im->fedSec.store(0);
    im->resPos = 0;
    im->resInit = false;
    im->running.store(true);
    im->worker = std::thread([im] {
        try { ZipformerWorker(im); }
        catch (const std::exception& error) { LogMsg("Zipformer worker failed: %s", error.what()); im->running.store(false); }
    });
    active_.store(true);
    LogMsg("Zipformer startup format=%s dll_ms=%.2f session_ms=%.2f remaining_ms=%.2f total_ms=%.2f",
        modelPath_.size() >= 4 && modelPath_.substr(modelPath_.size() - 4) == L".ort" ? "ort" : "onnx",
        (loaded-began)*1000, (initialized-loaded)*1000, (QpcNow()-initialized)*1000, (QpcNow()-began)*1000);
    LogMsg("Zipformer CTC INT8 captions started (CPU, 1 thread, greedy)");
    LogMsg("Zipformer CPU memory arena: %s", provider == "cpu" ? "default" : "disabled");
    return true;
}

bool LiveCaptions::StartMoonshine(){
    const auto* model=FindCaptionModel(selectedModel_);Impl* im=impl_;
    if(!model||(!Available()&&!Probe()))return false;
    const DWORD search=LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32;
    SetEnvironmentVariableW(L"MOONSHINE_ORT_SINGLE_THREAD",L"1");
    im->ortDll=LoadLibraryExW(PathJoin(ExeDir(),L"onnxruntime.dll").c_str(),nullptr,search);if(!im->ortDll)return false;
    im->moonDll=LoadLibraryExW(dllPath_.c_str(),nullptr,search);if(!im->moonDll)return false;
#define LOAD_MOON(field,name) im->field=reinterpret_cast<decltype(im->field)>(GetProcAddress(im->moonDll,#name));if(!im->field)return false;
    LOAD_MOON(moonLoad,moonshine_load_transcriber_from_files)
    LOAD_MOON(moonFree,moonshine_free_transcriber)
    LOAD_MOON(moonCreateStream,moonshine_create_stream)
    LOAD_MOON(moonFreeStream,moonshine_free_stream)
    LOAD_MOON(moonStart,moonshine_start_stream)
    LOAD_MOON(moonStop,moonshine_stop_stream)
    LOAD_MOON(moonAdd,moonshine_transcribe_add_audio_to_stream)
    LOAD_MOON(moonTranscribe,moonshine_transcribe_stream)
#undef LOAD_MOON
    const std::string interval=std::to_string(model->interval),path=WToUtf8(packDir_);
    MoonOption options[]={{"ort_providers","CPU"},{"return_audio_data","false"},{"max_tokens_per_second",model->language=="ja"?"13.0":"6.5"},{"transcription_interval",interval.c_str()},{"vad_threshold","0.5"}};
    im->moonHandle=im->moonLoad(path.c_str(),model->architecture,options,5,30000);if(im->moonHandle<0)return false;
    im->moonStream=im->moonCreateStream(im->moonHandle,0);if(im->moonStream<0||im->moonStart(im->moonHandle,im->moonStream)!=0)return false;
    {std::lock_guard<std::mutex> q(im->qmutex);im->queue.clear();im->queuePts=im->nextPts=0;im->hasPts=im->resetPending=im->finishPending=im->inputEnded=false;im->generation=0;}
    {std::lock_guard<std::mutex> r(im->rmutex);im->partial.clear();im->finals.clear();im->lastEnd=0;}
    im->fedSec.store(0);im->resPos=0;im->resInit=false;im->running.store(true);
    im->worker=std::thread([im]{try{MoonshineWorker(im);}catch(const std::exception& e){LogMsg("Moonshine worker failed: %s",e.what());im->running.store(false);}});
    active_.store(true);LogMsg("Moonshine captions started: %s",selectedModel_.c_str());return true;
}

void LiveCaptions::RequestStop() {
    Impl* im = impl_;
    { std::lock_guard<std::mutex> lock(im->qmutex); im->running.store(false); }
    im->queueChanged.notify_all();
}

void LiveCaptions::Stop() {
    Impl* im = impl_;
    RequestStop();
    if (im->worker.joinable()) im->worker.join();
    if(im->moonStream>=0&&im->moonFreeStream){im->moonFreeStream(im->moonHandle,im->moonStream);im->moonStream=-1;}
    if(im->moonHandle>=0&&im->moonFree){im->moonFree(im->moonHandle);im->moonHandle=-1;}
    if(im->moonDll){FreeLibrary(im->moonDll);im->moonDll=nullptr;}
    if (im->stream) { im->destroyStream(im->stream); im->stream = nullptr; }
    if (im->recognizer) { im->destroyRecognizer(im->recognizer); im->recognizer = nullptr; }
    if (im->sherpaDll) { FreeLibrary(im->sherpaDll); im->sherpaDll = nullptr; }
    if (im->ortDll) { FreeLibrary(im->ortDll); im->ortDll = nullptr; }
    { std::lock_guard<std::mutex> lock(im->qmutex); std::deque<float>().swap(im->queue); }
    { std::lock_guard<std::mutex> lock(im->rmutex); im->partial.clear(); }
    active_.store(false);
}

void LiveCaptions::PushMix(const float* interleaved, int frames, int rate, int channels, double mediaPts) {
    if (!Running() || frames <= 0 || frames > 1920000 || !interleaved || channels <= 0 || channels > 32 || rate < 8000 || rate > 192000 || !std::isfinite(mediaPts) || mediaPts < 0) return;
    Impl* im = impl_;

    double step = (double)rate / 16000.0;
    std::lock_guard<std::mutex> lk(im->qmutex);
    if (im->inputEnded || (im->hasPts && std::fabs(mediaPts - im->nextPts) > 0.12)) {
        im->queue.clear();
        im->resPos = 0;
        im->resInit = false;
        im->resetPending = true;
        ++im->generation;
        im->finishPending = im->inputEnded = false;
        std::lock_guard<std::mutex> rl(im->rmutex);
        im->partial.clear();
        im->finals.clear();
    }
    if (!im->hasPts) {
        im->resetPending = true;
        im->hasPts = true;
    }
    if (im->queue.empty()) im->queuePts = mediaPts;
    im->nextPts = mediaPts + (double)frames / rate;
    for (int i = 0; i < frames; ++i) {
        float sum = 0;
        for (int c = 0; c < channels; ++c) sum += interleaved[(size_t)i * channels + c];
        float cur = Clampf(sum / channels, -1, 1);
        if (!im->resInit) im->resLast = cur;
        while (im->resPos < 1.0) {
            float v = im->resLast + (cur - im->resLast) * (float)im->resPos;
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            im->queue.push_back(v);
            im->resPos += step;
        }
        im->resPos -= 1.0;
        im->resLast = cur;
        im->resInit = true;
    }
    if (im->queue.size() > Impl::kCapSamples) {
        const size_t dropped = im->queue.size() - Impl::kCapSamples;
        im->queue.erase(im->queue.begin(), im->queue.begin() + dropped);
        im->queuePts += (double)dropped / 16000;
        im->resetPending = true;
        ++im->generation;
        std::lock_guard<std::mutex> rl(im->rmutex);
        im->partial.clear();
        im->finals.clear();
        LogMsg("Zipformer backlog: dropped %.3fs to keep captions current", (double)dropped / 16000);
    }
    im->fedSec.store(mediaPts + (double)frames / rate);
    im->queueChanged.notify_one();
    return;

}

void LiveCaptions::ResetTimeline(double mediaPts) {
    if (!Running() || !std::isfinite(mediaPts)) return;
    Impl* im = impl_;
    mediaPts = std::max(0.0, mediaPts);
    std::lock_guard<std::mutex> lk(im->qmutex);
    im->queue.clear();
    im->queuePts = im->nextPts = mediaPts;
    im->hasPts = im->resetPending = true;
    im->finishPending = im->inputEnded = false;
    im->resPos = 0;
    im->resInit = false;
    ++im->generation;
    std::lock_guard<std::mutex> rl(im->rmutex);
    im->partial.clear();
    im->finals.clear();

    im->fedSec.store(mediaPts);
    im->queueChanged.notify_one();
}

void LiveCaptions::FinishInput(double mediaPts) {
    if (!Running() || !std::isfinite(mediaPts)) return;
    Impl* im = impl_;
    std::lock_guard<std::mutex> lk(im->qmutex);
    if (!im->hasPts || im->inputEnded) return;
    im->finishPending = im->inputEnded = true;
    im->queueChanged.notify_one();

}

std::wstring LiveCaptions::Partial() {
    if (!impl_) return L"";
    std::lock_guard<std::mutex> lk(impl_->rmutex);
    return impl_->partial;
}

void LiveCaptions::TakeFinals(std::vector<SubCue>& out,uint64_t* generation) {
    if (!impl_) return;
    std::lock_guard<std::mutex> q(impl_->qmutex);
    std::lock_guard<std::mutex> lk(impl_->rmutex);
    if(generation)*generation=impl_->generation;
    out.insert(out.end(), impl_->finals.begin(), impl_->finals.end());
    impl_->finals.clear();
}

size_t LiveCaptions::FinalCount() {
    if (!impl_) return 0;
    std::lock_guard<std::mutex> lk(impl_->rmutex);
    return impl_->finals.size();
}

void LiveCaptions::ClearResults() {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->rmutex);
    std::vector<SubCue>().swap(impl_->finals);
    impl_->partial.clear();
    impl_->lastEnd = 0;
}
