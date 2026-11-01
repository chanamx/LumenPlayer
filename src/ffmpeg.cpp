#include "ffmpeg.h"
#include "playlist.h"
#include <mfapi.h>
#include <mfreadwrite.h>
#include <mmreg.h>
#include <climits>
#include <cstring>
#include <cerrno>
#include <d3d11.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#define FF_FUNCTIONS(X) \
    X(av_version_info) X(av_strerror) X(av_malloc) X(av_free) X(av_freep) X(av_log_set_level) X(avformat_network_init) X(avformat_network_deinit) \
    X(avformat_alloc_context) X(avformat_open_input) X(avformat_find_stream_info) X(avformat_close_input) \
    X(av_find_best_stream) X(av_read_frame) X(avformat_seek_file) X(avformat_version) \
    X(avio_alloc_context) X(avio_context_free) X(avio_open2) X(av_dict_set) X(av_dict_free) \
    X(avcodec_find_decoder) X(avcodec_find_decoder_by_name) X(avcodec_alloc_context3) X(avcodec_parameters_to_context) X(avcodec_open2) \
    X(avcodec_free_context) X(avcodec_send_packet) X(avcodec_receive_frame) X(avcodec_flush_buffers) \
    X(avcodec_version) X(avcodec_license) X(avcodec_configuration) X(avcodec_get_hw_config) \
    X(av_packet_alloc) X(av_packet_free) X(av_packet_unref) X(av_packet_side_data_get) \
    X(av_frame_alloc) X(av_frame_free) X(av_frame_unref) X(av_frame_get_side_data) X(av_frame_clone) X(av_frame_move_ref) X(av_frame_copy_props) \
    X(av_hwdevice_ctx_create) X(av_hwframe_transfer_data) X(av_buffer_unref) X(av_pix_fmt_desc_get) \
    X(av_channel_layout_default) X(av_channel_layout_from_mask) X(av_channel_layout_copy) X(av_channel_layout_uninit) X(av_channel_layout_compare) \
    X(av_display_rotation_get) X(sws_getCachedContext) X(sws_freeContext) X(sws_getCoefficients) \
    X(sws_setColorspaceDetails) X(sws_scale) X(swr_alloc_set_opts2) X(swr_init) X(swr_free) \
    X(swr_get_delay) X(swr_get_out_samples) X(swr_convert)

namespace {
struct Runtime {
#define DECLARE(name) decltype(&::name) name = nullptr;
    FF_FUNCTIONS(DECLARE)
#undef DECLARE
    std::mutex mutex;
    HMODULE modules[5]{};
    unsigned users = 0;
    bool networkStarted=false;
    static Runtime& Get() { static Runtime runtime; return runtime; }
    void Unload() {
        if(networkStarted) {avformat_network_deinit();networkStarted=false;}
        for (int i = 4; i >= 0; --i) if (modules[i]) { FreeLibrary(modules[i]); modules[i] = nullptr; }
    }
    bool Acquire() {
        std::lock_guard<std::mutex> lock(mutex);
        if (users) { ++users; return true; }
        const wchar_t* names[] = {L"avutil-61.dll", L"swresample-7.dll", L"swscale-10.dll", L"avcodec-63.dll", L"avformat-63.dll"};
        for (int i = 0; i < 5; ++i) {
            modules[i] = LoadLibraryExW(PathJoin(ExeDir(), names[i]).c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!modules[i]) { LogMsg("FFmpeg runtime unavailable: %s error=%lu", WToUtf8(names[i]).c_str(), GetLastError()); Unload(); return false; }
        }
        auto symbol = [&](const char* name) -> FARPROC {
            for (HMODULE module : modules) if (FARPROC proc = GetProcAddress(module, name)) return proc;
            return nullptr;
        };
#define LOAD(name) name = reinterpret_cast<decltype(name)>(symbol(#name)); if (!name) { LogMsg("Missing FFmpeg export: " #name); Unload(); return false; }
        FF_FUNCTIONS(LOAD)
#undef LOAD
        if ((avcodec_version() >> 16) != LIBAVCODEC_VERSION_MAJOR || (avformat_version() >> 16) != LIBAVFORMAT_VERSION_MAJOR) {
            LogMsg("FFmpeg runtime ABI mismatch"); Unload(); return false;
        }
        av_log_set_level(AV_LOG_ERROR);
        avformat_network_init();
        networkStarted=true;
        users = 1;
        LogMsg("FFmpeg %s software decoder loaded (%s)", av_version_info(), avcodec_license());
        return true;
    }
    void Release() {
        std::lock_guard<std::mutex> lock(mutex);
        if (users && !--users) Unload();
    }
    void Error(const char* operation, int error) const {
        char text[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(error, text, sizeof(text));
        LogMsg("FFmpeg %s: %s (%d)", operation, text, error);
    }
};

bool ValidSize(int w, int h) { return w > 0 && h > 0 && w <= 16384 && h <= 16384 && (uint64_t)w * h <= 67108864; }
HRESULT MakeSample(const BYTE* data, DWORD bytes, double pts, double duration, IMFSample** output) {
    Com<IMFSample> sample; Com<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateSample(sample.put());
    if (SUCCEEDED(hr)) hr = MFCreateMemoryBuffer(bytes, buffer.put());
    BYTE* dst = nullptr;
    if (SUCCEEDED(hr)) hr = buffer->Lock(&dst, nullptr, nullptr);
    if (SUCCEEDED(hr)) { memcpy(dst, data, bytes); buffer->Unlock(); hr = buffer->SetCurrentLength(bytes); }
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.get());
    if (FAILED(hr)) return hr;
    sample->SetSampleTime((LONGLONG)(pts * 10000000.0));
    sample->SetSampleDuration((LONGLONG)(duration * 10000000.0));
    *output = sample.detach();
    return S_OK;
}
}

struct FfmpegReader::Impl {
    Runtime& lib = Runtime::Get();
    bool acquired = false;
    FILE* file = nullptr;
    int64_t fileSize = 0;
    AVIOContext* io = nullptr;
    AVFormatContext* format = nullptr;
    AVCodecContext* codecs[2]{};
    int streams[2]{-1, -1};
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* nativeFrame = nullptr;
    AVFrame* transferFrame = nullptr;
    bool nativeVideo = false, preferHardware = false;
    std::atomic<bool> hardwareUsed{false};
    SwsContext* scaler = nullptr;
    SwrContext* resampler = nullptr;
    AVChannelLayout inputLayout{}, outputLayout{};
    AVSampleFormat inputSample = AV_SAMPLE_FMT_NONE, outputSample = AV_SAMPLE_FMT_FLT;
    int inputRate = 0;
    WAVEFORMATEX audio{};
    VideoFormat video;
    double origin = 0, duration = 0, observedEnd = 0, videoNext = 0, audioNext = 0, discardBefore = 0;
    int rotation = 0, pending = -1;
    bool packetPending = false, demuxEof = false;
    bool sentDrain[2]{}, drained[2]{}, ended[2]{};
    bool firstVideo = true, firstAudio = true, resamplerDrained = false;
    unsigned decodeErrors = 0;
    bool live=false;
    bool networkInput=false;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::atomic<bool> interrupted{false};
    std::atomic<double> deadline{0};
    std::vector<BYTE> converted, rotated;

    ~Impl() {
        if (!acquired) return;
        if (resampler) lib.swr_free(&resampler);
        lib.av_channel_layout_uninit(&inputLayout);
        lib.av_channel_layout_uninit(&outputLayout);
        if (scaler) lib.sws_freeContext(scaler);
        lib.av_frame_free(&nativeFrame);lib.av_frame_free(&transferFrame);
        lib.av_frame_free(&frame); lib.av_packet_free(&packet);
        for (auto& codec : codecs) lib.avcodec_free_context(&codec);
        lib.avformat_close_input(&format);
        if (io) { lib.av_freep(&io->buffer); lib.avio_context_free(&io); }
        if (file) fclose(file);
        lib.Release();
    }
    static int ReadFile(void* opaque, uint8_t* buffer, int bytes) {
        auto& self = *static_cast<Impl*>(opaque);
        const size_t read = fread(buffer, 1, (size_t)bytes, self.file);
        return read ? (int)read : (ferror(self.file) ? AVERROR(EIO) : AVERROR_EOF);
    }
    static int64_t SeekFile(void* opaque, int64_t offset, int whence) {
        auto& self = *static_cast<Impl*>(opaque);
        if (whence & AVSEEK_SIZE) return self.fileSize;
        whence &= ~AVSEEK_FORCE;
        if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) return AVERROR(EINVAL);
        if (_fseeki64(self.file, offset, whence)) return AVERROR(EIO);
        return _ftelli64(self.file);
    }
    static int DenyExternal(AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**) { return AVERROR(EPERM); }
    static int OpenHlsFile(AVFormatContext* context,AVIOContext** output,const char* url,int flags,AVDictionary** options) {
        auto& lib=Runtime::Get();std::string name=url,protocol=name;
        const bool local=!static_cast<Impl*>(context->opaque)->networkInput;
        if(protocol.rfind("crypto+",0)==0 || protocol.rfind("crypto:",0)==0) protocol.erase(0,7);
        if(local && (protocol.rfind("file:",0)==0 || (protocol.size()>2 && protocol[1]==':') || (!protocol.empty() && protocol[0]=='/'))) {
            std::string decoded;
            auto hex=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
            for(size_t i=0;i<name.size();++i) {
                if(name[i]=='%' && i+2<name.size() && hex(name[i+1])>=0 && hex(name[i+2])>=0) {
                    char value=(char)(hex(name[i+1])*16+hex(name[i+2]));if(!value)return AVERROR(EINVAL);
                    decoded+=value;i+=2;
                } else decoded+=name[i];
            }
            name=std::move(decoded);
        }
        AVDictionary* localOptions=nullptr;auto selected=options?options:&localOptions;
        lib.av_dict_set(selected,"protocol_whitelist",local?"file,http,https,tcp,tls,crypto,httpproxy":"http,https,tcp,tls,crypto,httpproxy",0);
        lib.av_dict_set(selected,"tls_verify","1",0);
        lib.av_dict_set(selected,"rw_timeout","5000000",0);
        const int result=lib.avio_open2(output,name.c_str(),flags,&context->interrupt_callback,selected);
        if(!options)lib.av_dict_free(&localOptions);
        return result;
    }
    static int Interrupted(void* opaque) {
        auto& self=*static_cast<Impl*>(opaque);
        return self.interrupted.load() || (self.cancel && self.cancel->load()) || (self.deadline>0 && QpcNow()>self.deadline);
    }
    bool OpenCodec(int kind) {
        const auto* parameters = format->streams[streams[kind]]->codecpar;
        const AVCodec* decoder = lib.avcodec_find_decoder(parameters->codec_id);
        AVBufferRef* av1Device=nullptr;
        if(kind==0 && preferHardware && parameters->codec_id==AV_CODEC_ID_AV1 &&
            lib.av_hwdevice_ctx_create(&av1Device,AV_HWDEVICE_TYPE_D3D11VA,nullptr,nullptr,0)>=0) {
            auto* context=(AVHWDeviceContext*)av1Device->data;
            auto* device=(AVD3D11VADeviceContext*)context->hwctx;
            const GUID profile0={0xb8be4ccb,0xcf53,0x46ba,{0x8d,0x59,0xd6,0xb8,0xa6,0xda,0x5d,0x2a}};
            bool supported=false;
            if(device->video_device) for(UINT i=0;i<device->video_device->GetVideoDecoderProfileCount();++i) {
                GUID profile{};if(SUCCEEDED(device->video_device->GetVideoDecoderProfile(i,&profile)) && profile==profile0)supported=true;
            }
            if(supported) {if(const auto* native=lib.avcodec_find_decoder_by_name("av1"))decoder=native;}
        }
        if(av1Device)lib.av_buffer_unref(&av1Device);
        if (!decoder || !(codecs[kind] = lib.avcodec_alloc_context3(decoder))) return false;
        AVCodecContext* codec = codecs[kind];
        int error = lib.avcodec_parameters_to_context(codec, parameters);
        if (error < 0) { lib.Error("codec parameters", error); return false; }
        codec->pkt_timebase = format->streams[streams[kind]]->time_base;
        codec->thread_count = (int)std::max(1u, std::min(4u, std::thread::hardware_concurrency() / 2));
        codec->max_pixels = 67108864;
        const auto* pixel=lib.av_pix_fmt_desc_get((AVPixelFormat)parameters->format);
        const bool compatiblePixel=!pixel || (pixel->log2_chroma_w==1 && pixel->log2_chroma_h==1 && pixel->comp[0].depth<=10 &&
            (parameters->codec_id!=AV_CODEC_ID_H264 || pixel->comp[0].depth==8));
        if(kind==0 && preferHardware && compatiblePixel) {
            for(int i=0;;++i) {
                const auto* config=lib.avcodec_get_hw_config(decoder,i);if(!config)break;
                if(config->device_type!=AV_HWDEVICE_TYPE_D3D11VA || !(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))continue;
                if(lib.av_hwdevice_ctx_create(&codec->hw_device_ctx,AV_HWDEVICE_TYPE_D3D11VA,nullptr,nullptr,0)>=0) {
                    codec->opaque=this;
                    codec->get_format=[](AVCodecContext* ctx,const AVPixelFormat* formats)->AVPixelFormat {
                        auto& self=*static_cast<Impl*>(ctx->opaque);
                        for(const auto* fmt=formats;*fmt!=AV_PIX_FMT_NONE;++fmt) if(*fmt==AV_PIX_FMT_D3D11) {
                            return *fmt;
                        }
                        self.hardwareUsed=false;
                        for(const auto* fmt=formats;*fmt!=AV_PIX_FMT_NONE;++fmt) {
                            const auto* desc=self.lib.av_pix_fmt_desc_get(*fmt);
                            if(desc && !(desc->flags&AV_PIX_FMT_FLAG_HWACCEL))return *fmt;
                        }
                        return AV_PIX_FMT_NONE;
                    };
                    codec->thread_count=1;
                }
                break;
            }
        }
        error = lib.avcodec_open2(codec, decoder, nullptr);
        if (error < 0) { lib.Error("open decoder", error); return false; }
        return true;
    }
    bool Open(const std::wstring& path, const WAVEFORMATEX* target, bool audioOnly) {
        acquired = lib.Acquire();
        if (!acquired) return false;
        const bool remote=IsNetworkMedia(path), hls=IsPlaylistFile(path);
        networkInput=remote;
        if(!remote) {
        file = _wfopen(path.c_str(), L"rb");
        if (!file || _fseeki64(file, 0, SEEK_END)) return false;
        fileSize = _ftelli64(file);
        if (fileSize <= 0 || _fseeki64(file, 0, SEEK_SET)) return false;
        BYTE* buffer = (BYTE*)lib.av_malloc(65536);
        if (!buffer) return false;
        io = lib.avio_alloc_context(buffer, 65536, 0, this, ReadFile, nullptr, SeekFile);
        if (!io) { lib.av_free(buffer); return false; }
        }
        format = lib.avformat_alloc_context();
        if (!format) return false;
        if(io) {format->pb=io;format->flags|=AVFMT_FLAG_CUSTOM_IO;}
        format->opaque=this;
        format->io_open=(remote||hls)?OpenHlsFile:DenyExternal;
        format->interrupt_callback={Interrupted,this}; deadline=QpcNow()+20;
        format->probesize = 5 * 1024 * 1024;
        format->max_analyze_duration = 3 * AV_TIME_BASE;
        format->max_streams = 64;
        AVDictionary* options=nullptr;
        lib.av_dict_set(&options,"protocol_whitelist",remote?"http,https,tcp,tls,crypto,httpproxy":"file,http,https,tcp,tls,crypto,httpproxy",0);
        lib.av_dict_set(&options,"rw_timeout","5000000",0);
        lib.av_dict_set(&options,"tls_verify","1",0);
        if(remote || hls) lib.av_dict_set(&options,"allowed_extensions",
            "3gp,aac,avi,ac3,eac3,flac,mkv,m3u,m3u8,m4a,m4s,m4v,mpg,mov,mp2,mp3,mp4,mpeg,mpegts,ogg,ogv,oga,ts,vob,vtt,wav,webvtt,cmfv,cmfa,ec3,fmp4,key,bin",0);
        std::string inputName=WToUtf8(path);
        if(!remote && hls) {
            // The root manifest uses our Unicode file handle. Escape URI syntax
            // for HLS relative resolution; decode only file I/O in OpenHlsFile.
            std::string escaped;
            for(char c:inputName) {
                if(c=='%')escaped+="%25";else if(c=='#')escaped+="%23";else if(c=='?')escaped+="%3F";else escaped+=c;
            }
            inputName=std::move(escaped);
        }
        int error = lib.avformat_open_input(&format, inputName.c_str(), nullptr, &options);
        lib.av_dict_free(&options);
        if (error < 0) { lib.Error("open local input", error); return false; }
        error = lib.avformat_find_stream_info(format, nullptr);
        if (error < 0) { lib.Error("find streams", error); return false; }
        origin = format->start_time == AV_NOPTS_VALUE ? 0 : (double)format->start_time / AV_TIME_BASE;
        duration = format->duration == AV_NOPTS_VALUE ? 0 : std::max(0.0, (double)format->duration / AV_TIME_BASE);
        live=duration<=0 && (remote || hls);
        streams[0] = audioOnly ? -1 : lib.av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        streams[1] = lib.av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (streams[0] >= 0 && (format->streams[streams[0]]->disposition & AV_DISPOSITION_ATTACHED_PIC)) streams[0] = -1;
        for (unsigned i = 0; i < format->nb_streams; ++i) format->streams[i]->discard = AVDISCARD_ALL;
        if (streams[0] >= 0) {
            AVStream* stream = format->streams[streams[0]];
            if (!ValidSize(stream->codecpar->width, stream->codecpar->height) || !OpenCodec(0)) return false;
            stream->discard = AVDISCARD_DEFAULT;
            const AVPacketSideData* matrix = lib.av_packet_side_data_get(stream->codecpar->coded_side_data, stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
            if (matrix && matrix->size >= 9 * sizeof(int32_t)) {
                const double angle = -lib.av_display_rotation_get((const int32_t*)matrix->data);
                if (std::isfinite(angle)) rotation = (((int)std::lround(angle / 90) % 4) + 4) % 4;
            }
            AVRational rate = stream->avg_frame_rate.num ? stream->avg_frame_rate : stream->r_frame_rate;
            if (rate.num > 0 && rate.den > 0) video.fps = std::max(1.0, std::min(1000.0, av_q2d(rate)));
            SetVideo(stream->codecpar->width, stream->codecpar->height, stream->sample_aspect_ratio);
        }
        ConfigureAudio(target);
        packet = lib.av_packet_alloc(); frame = lib.av_frame_alloc();
        double streamDuration = 0;
        bool knownDuration = true;
        for (int kind = 0; kind < 2; ++kind) if (codecs[kind]) {
            const AVStream* stream = format->streams[streams[kind]];
            if (stream->duration == AV_NOPTS_VALUE) knownDuration = false;
            else {
                const double start = stream->start_time == AV_NOPTS_VALUE ? origin : stream->start_time * av_q2d(stream->time_base);
                streamDuration = std::max(streamDuration, stream->duration * av_q2d(stream->time_base) + std::max(0.0, start - origin));
            }
        }
        if (!live && knownDuration && streamDuration > 0) duration = streamDuration;
        deadline=0;
        LogMsg("FFmpeg input video=%d audio=%d origin=%.6f duration=%.6f live=%d rotation=%d", codecs[0] != nullptr, streams[1]>=0, origin, duration, live, rotation * 90);
        return packet && frame && (codecs[0] || codecs[1] || (!target && streams[1]>=0));
    }
    bool ConfigureAudio(const WAVEFORMATEX* target) {
        if (streams[1] >= 0 && target && !codecs[1]) {
            audio = *target;
            if (audio.nChannels < 1 || audio.nChannels > 8 || audio.nSamplesPerSec < 8000 || audio.nSamplesPerSec > 192000) return false;
            if (audio.wFormatTag == WAVE_FORMAT_IEEE_FLOAT && audio.wBitsPerSample == 32) outputSample = AV_SAMPLE_FMT_FLT;
            else if (audio.wFormatTag == WAVE_FORMAT_PCM && audio.wBitsPerSample == 16) outputSample = AV_SAMPLE_FMT_S16;
            else if (audio.wFormatTag == WAVE_FORMAT_PCM && (audio.wBitsPerSample == 24 || audio.wBitsPerSample == 32)) outputSample = AV_SAMPLE_FMT_S32;
            else if (audio.wFormatTag == WAVE_FORMAT_PCM && audio.wBitsPerSample == 8) outputSample = AV_SAMPLE_FMT_U8;
            else return false;
            lib.av_channel_layout_default(&outputLayout, audio.nChannels);
            if (OpenCodec(1)) format->streams[streams[1]]->discard = AVDISCARD_DEFAULT;
            else lib.avcodec_free_context(&codecs[1]);
        }
        return codecs[1]!=nullptr;
    }
    void SetVideo(int width, int height, AVRational sar) {
        video.width = (rotation & 1) ? height : width;
        video.height = (rotation & 1) ? width : height;
        video.rotation = rotation;
        double aspect = (double)width / height;
        if (sar.num > 0 && sar.den > 0) aspect *= av_q2d(sar);
        video.aspect = (rotation & 1) ? 1 / aspect : aspect;
    }
    double FrameTime(int kind) const {
        const int64_t timestamp = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
        return timestamp != AV_NOPTS_VALUE ? timestamp * av_q2d(format->streams[streams[kind]]->time_base) - origin : (kind ? audioNext : videoNext);
    }
    HRESULT VideoSample(DWORD* flags, LONGLONG* timestamp, IMFSample** output) {
        if(frame->format==AV_PIX_FMT_D3D11) {
            if(!transferFrame)transferFrame=lib.av_frame_alloc();
            if(!transferFrame)return E_OUTOFMEMORY;
            lib.av_frame_unref(transferFrame);
            int error=lib.av_hwframe_transfer_data(transferFrame,frame,0);
            if(error<0){lib.Error("D3D11 frame transfer",error);return E_FAIL;}
            if(lib.av_frame_copy_props(transferFrame,frame)<0)return E_OUTOFMEMORY;
            if(!hardwareUsed.exchange(true))LogMsg("D3D11VA hardware frame received and transferred successfully");
            lib.av_frame_unref(frame);lib.av_frame_move_ref(frame,transferFrame);
        }
        const int w = frame->width, h = frame->height;
        if (!ValidSize(w, h)) return E_INVALIDARG;
        const double pts = std::max(0.0, FrameTime(0));
        const double length = frame->duration > 0 ? frame->duration * av_q2d(format->streams[streams[0]]->time_base) : 1 / video.fps;
        videoNext = pts + length;
        if (pts + length < discardBefore) return S_FALSE;
        const VideoFormat previous = video;
        AVRational sar = frame->sample_aspect_ratio;
        if (sar.num <= 0 || sar.den <= 0) sar = format->streams[streams[0]]->sample_aspect_ratio;
        SetVideo(w, h, sar);
        if (firstVideo || previous.width != video.width || previous.height != video.height || std::fabs(previous.aspect - video.aspect) > 0.0001)
            *flags |= MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED;
        firstVideo = false;
        if(nativeVideo) {
            lib.av_frame_free(&nativeFrame);nativeFrame=lib.av_frame_clone(frame);
            if(!nativeFrame)return E_OUTOFMEMORY;
            observedEnd=std::max(observedEnd,pts+length);if(!live)duration=std::max(duration,observedEnd);
            *timestamp=(LONGLONG)(pts*10000000.0);
            const BYTE placeholder[4]{};
            return MakeSample(placeholder,4,pts,length,output);
        }
        scaler = lib.sws_getCachedContext(scaler, w, h, (AVPixelFormat)frame->format, w, h, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scaler) return E_OUTOFMEMORY;
        int matrix = frame->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 :
                     frame->colorspace == AVCOL_SPC_BT2020_NCL || frame->colorspace == AVCOL_SPC_BT2020_CL ? SWS_CS_BT2020 : SWS_CS_ITU601;
        const int* coefficients = lib.sws_getCoefficients(matrix);
        lib.sws_setColorspaceDetails(scaler, coefficients, frame->color_range == AVCOL_RANGE_JPEG, coefficients, 1, 0, 1 << 16, 1 << 16);
        const size_t pixelBytes = (size_t)w * h * 4;
        converted.resize(pixelBytes + AV_INPUT_BUFFER_PADDING_SIZE);
        BYTE* planes[]{converted.data(), nullptr, nullptr, nullptr};
        int strides[]{w * 4, 0, 0, 0};
        if (lib.sws_scale(scaler, frame->data, frame->linesize, 0, h, planes, strides) != h) return E_FAIL;
        const BYTE* pixels = converted.data();
        if (rotation) {
            rotated.resize(pixelBytes + AV_INPUT_BUFFER_PADDING_SIZE);
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                const int dx = rotation == 1 ? h - 1 - y : rotation == 2 ? w - 1 - x : y;
                const int dy = rotation == 1 ? x : rotation == 2 ? h - 1 - y : w - 1 - x;
                memcpy(rotated.data() + ((size_t)dy * video.width + dx) * 4, converted.data() + ((size_t)y * w + x) * 4, 4);
            }
            pixels = rotated.data();
        }
        observedEnd = std::max(observedEnd, pts + length);
        if(!live) duration = std::max(duration, observedEnd);
        *timestamp = (LONGLONG)(pts * 10000000.0);
        return MakeSample(pixels, (DWORD)pixelBytes, pts, length, output);
    }
    HRESULT PcmSample(int samples, double pts, LONGLONG* timestamp, IMFSample** output) {
        if (audio.wBitsPerSample == 24) {
            for (int i = 0; i < samples * audio.nChannels; ++i)
                memmove(converted.data() + (size_t)i * 3, converted.data() + (size_t)i * 4 + 1, 3);
        }
        const double next = pts + (double)samples / audio.nSamplesPerSec;
        audioNext = next; observedEnd = std::max(observedEnd, next); if(!live) duration = std::max(duration, observedEnd);
        const int skip = (int)std::min<double>(samples, std::max(0.0, (discardBefore - pts) * audio.nSamplesPerSec));
        if (skip == samples) return S_FALSE;
        pts = std::max(0.0, pts + (double)skip / audio.nSamplesPerSec);
        *timestamp = (LONGLONG)(pts * 10000000.0);
        return MakeSample(converted.data() + (size_t)skip * audio.nBlockAlign, (DWORD)((samples - skip) * audio.nBlockAlign), pts,
            (double)(samples - skip) / audio.nSamplesPerSec, output);
    }
    HRESULT AudioSample(LONGLONG* timestamp, IMFSample** output) {
        if(firstAudio) {
            const auto* codec=codecs[1];
            const bool atmos=(codec->codec_id==AV_CODEC_ID_EAC3 && codec->profile==AV_PROFILE_EAC3_DDP_ATMOS) ||
                (codec->codec_id==AV_CODEC_ID_TRUEHD && codec->profile==AV_PROFILE_TRUEHD_ATMOS);
            LogMsg("Audio decode codec=%s profile=%d input_channels=%d output_channels=%d atmos_metadata=%d output=PCM (no object rendering/passthrough)",
                codec->codec->name,codec->profile,frame->ch_layout.nb_channels,audio.nChannels,(int)atmos);
            firstAudio=false;
        }
        if (frame->sample_rate < 8000 || frame->sample_rate > 192000 || frame->ch_layout.nb_channels < 1 ||
            frame->ch_layout.nb_channels > 32 || frame->nb_samples < 1 || frame->nb_samples > 1048576) return E_INVALIDARG;
        if (!resampler || inputRate != frame->sample_rate || inputSample != frame->format ||
            lib.av_channel_layout_compare(&inputLayout, &frame->ch_layout)) {
            lib.swr_free(&resampler); lib.av_channel_layout_uninit(&inputLayout);
            if (lib.av_channel_layout_copy(&inputLayout, &frame->ch_layout) < 0) return E_OUTOFMEMORY;
            inputRate = frame->sample_rate; inputSample = (AVSampleFormat)frame->format;
            int error = lib.swr_alloc_set_opts2(&resampler, &outputLayout, outputSample, audio.nSamplesPerSec, &inputLayout, inputSample, inputRate, 0, nullptr);
            if (error >= 0) error = lib.swr_init(resampler);
            if (error < 0) { lib.Error("initialize audio conversion", error); return E_FAIL; }
        }
        const double pts = FrameTime(1) - (double)lib.swr_get_delay(resampler, inputRate) / inputRate;
        const int capacity = lib.swr_get_out_samples(resampler, frame->nb_samples);
        if (capacity < 0 || capacity > 2097152) return E_INVALIDARG;
        converted.resize((size_t)capacity * audio.nChannels * 4);
        BYTE* planes[]{converted.data()};
        const int samples = lib.swr_convert(resampler, planes, capacity, (const uint8_t**)frame->extended_data, frame->nb_samples);
        if (samples < 0) { lib.Error("convert audio", samples); return E_FAIL; }
        return samples ? PcmSample(samples, pts, timestamp, output) : S_FALSE;
    }
    bool Seek(double seconds) {
        if(live) return false;
        deadline=QpcNow()+20;
        const int kind = codecs[0] ? 0 : 1;
        const AVStream* selected = format->streams[streams[kind]];
        const double ticksPerSecond = 1 / av_q2d(selected->time_base);
        // Generic MPEG indices can land inside a PES/GOP. Decode a short
        // preroll before the requested point; discarded frames skip RGB work.
        const double preroll = kind == 0 ? 1.0 : 0.0;
        const int64_t target = (int64_t)std::llround((std::max(0.0, seconds) + origin - preroll) * ticksPerSecond);
        int64_t latest = target;
        // FLV and other muxers may start video after the first audio packet.
        // Seeking to zero must allow that first keyframe, while later seeks
        // still constrain the decoder to a keyframe at/before the target.
        if (selected->start_time != AV_NOPTS_VALUE) latest = std::max(latest, selected->start_time);
        // Seek against the video keyframe index rather than whichever stream
        // the demuxer selects globally (an early MP2 packet can skip that I-frame).
        int error = lib.avformat_seek_file(format, streams[kind], INT64_MIN, target, latest, AVSEEK_FLAG_BACKWARD);
        if (error < 0) { lib.Error("seek", error); return false; }
        for (auto* codec : codecs) if (codec) lib.avcodec_flush_buffers(codec);
        lib.av_packet_unref(packet); lib.av_frame_unref(frame);
        lib.swr_free(&resampler); inputRate = 0;
        pending = -1; packetPending = demuxEof = false;
        for (int i = 0; i < 2; ++i) sentDrain[i] = drained[i] = ended[i] = false;
        resamplerDrained = false; decodeErrors = 0; observedEnd = 0; videoNext = audioNext = discardBefore = std::max(0.0, seconds);
        return true;
    }
    HRESULT Read(DWORD* stream, DWORD* flags, LONGLONG* timestamp, IMFSample** output) {
        deadline=QpcNow()+20;
        *flags = 0; *timestamp = 0; *output = nullptr;
        for (;;) {
            if (pending >= 0) {
                const int kind = pending;
                int error = lib.avcodec_receive_frame(codecs[kind], frame);
                if (error >= 0) {
                    *stream = kind;
                    HRESULT hr = kind ? AudioSample(timestamp, output) : VideoSample(flags, timestamp, output);
                    lib.av_frame_unref(frame);
                    if (hr == S_FALSE) continue;
                    return hr;
                }
                if (error == AVERROR_EOF || (error == AVERROR(EAGAIN) && sentDrain[kind])) drained[kind] = true;
                else if (error != AVERROR(EAGAIN)) { lib.Error("receive frame", error); return E_FAIL; }
                pending = -1;
                if (packetPending) {
                    error = lib.avcodec_send_packet(codecs[kind], packet);
                    if (error < 0) { lib.Error("retry packet", error); return E_FAIL; }
                    lib.av_packet_unref(packet); packetPending = false; pending = kind; continue;
                }
            }
            if (demuxEof) {
                bool draining = false;
                for (int kind = 0; kind < 2; ++kind) if (codecs[kind] && !drained[kind]) {
                    if (!sentDrain[kind]) {
                        int error = lib.avcodec_send_packet(codecs[kind], nullptr);
                        if (error < 0 && error != AVERROR_EOF) { lib.Error("drain decoder", error); return E_FAIL; }
                        sentDrain[kind] = true;
                    }
                    pending = kind; draining = true; break;
                }
                if (draining) continue;
                if (resampler && !resamplerDrained) {
                    converted.resize((size_t)8192 * audio.nChannels * 4);
                    BYTE* planes[]{converted.data()};
                    const int samples = lib.swr_convert(resampler, planes, 8192, nullptr, 0);
                    if (samples < 0) return E_FAIL;
                    if (samples) { *stream = 1; HRESULT hr = PcmSample(samples, audioNext, timestamp, output); if (hr == S_FALSE) continue; return hr; }
                    resamplerDrained = true;
                }
                // Container duration can include a nonzero mux timestamp origin.
                // Once all delayed frames and audio samples are drained, use the
                // observed media endpoint rather than waiting on header padding.
                if (!live && observedEnd > 0) duration = observedEnd;
                for (int kind = 0; kind < 2; ++kind) if (codecs[kind] && !ended[kind]) {
                    ended[kind] = true; *stream = kind; *flags = MF_SOURCE_READERF_ENDOFSTREAM; return S_OK;
                }
                return S_FALSE;
            }
            const int error = lib.av_read_frame(format, packet);
            if (error == AVERROR_EOF) { demuxEof = true; continue; }
            if (error < 0) { lib.Error("read local packet", error); return E_FAIL; }
            int kind = packet->stream_index == streams[0] ? 0 : packet->stream_index == streams[1] ? 1 : -1;
            if (kind < 0 || !codecs[kind]) { lib.av_packet_unref(packet); continue; }
            const int sent = lib.avcodec_send_packet(codecs[kind], packet);
            if (sent == AVERROR(EAGAIN)) { packetPending = true; pending = kind; continue; }
            lib.av_packet_unref(packet);
            if (sent < 0) {
                if (sent == AVERROR_INVALIDDATA && ++decodeErrors <= 16) continue;
                lib.Error("decode packet", sent); return E_FAIL;
            }
            pending = kind;
        }
    }
};

FfmpegReader::FfmpegReader() = default;
FfmpegReader::~FfmpegReader() = default;
bool FfmpegReader::Open(const std::wstring& path, const WAVEFORMATEX* audio, bool audioOnly, const std::shared_ptr<std::atomic<bool>>& cancel) {
    impl_ = std::make_unique<Impl>();
    impl_->cancel=cancel;
    if (impl_->Open(path, audio, audioOnly)) return true;
    impl_.reset(); return false;
}
bool FfmpegReader::ConfigureAudio(const WAVEFORMATEX* audio) {return impl_ && impl_->ConfigureAudio(audio);}
void FfmpegReader::Interrupt(bool cancel) {if(impl_) impl_->interrupted.store(cancel);}
bool FfmpegReader::Live() const {return impl_ && impl_->live;}
bool FfmpegReader::Seek(double seconds) { return impl_ && impl_->Seek(seconds); }
HRESULT FfmpegReader::Read(DWORD* stream, DWORD* flags, LONGLONG* time, IMFSample** sample) {
    return impl_ ? impl_->Read(stream, flags, time, sample) : E_UNEXPECTED;
}
bool FfmpegReader::HasVideo() const { return impl_ && impl_->codecs[0]; }
bool FfmpegReader::HasAudio() const { return impl_ && impl_->streams[1] >= 0; }
bool FfmpegReader::DecodesAudio() const { return impl_ && impl_->codecs[1]; }
double FfmpegReader::Duration() const { return impl_ ? impl_->duration : 0; }
FfmpegReader::VideoFormat FfmpegReader::Video() const { return impl_ ? impl_->video : VideoFormat{}; }
void FfmpegReader::SetNativeVideo(bool enabled) {if(impl_)impl_->nativeVideo=enabled;}
const AVFrame* FfmpegReader::NativeVideo() const {return impl_?impl_->nativeFrame:nullptr;}
bool FfmpegReader::SetHardwareDecoding(bool enabled) {
    if(!impl_ || !impl_->codecs[0])return false;
    impl_->lib.avcodec_free_context(&impl_->codecs[0]);impl_->preferHardware=enabled;impl_->hardwareUsed=false;
    if(!impl_->OpenCodec(0)) {
        impl_->lib.avcodec_free_context(&impl_->codecs[0]);impl_->preferHardware=false;
        if(!impl_->OpenCodec(0))return false;
    }
    return !enabled || impl_->codecs[0]->hw_device_ctx;
}
bool FfmpegReader::HardwareDecoding() const {return impl_ && impl_->hardwareUsed.load();}
bool FfmpegReader::SetOutputChannelMask(uint64_t mask) {
    if(!impl_ || !mask || !impl_->codecs[1])return false;
    AVChannelLayout layout{};
    if(impl_->lib.av_channel_layout_from_mask(&layout,mask)<0)return false;
    if(layout.nb_channels!=impl_->audio.nChannels) {impl_->lib.av_channel_layout_uninit(&layout);return false;}
    impl_->lib.av_channel_layout_uninit(&impl_->outputLayout);impl_->outputLayout=layout;
    impl_->lib.swr_free(&impl_->resampler);impl_->inputRate=0;return true;
}
