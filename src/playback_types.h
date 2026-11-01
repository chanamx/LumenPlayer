#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Settings describe user preferences; CoreStatus describes the actual output.
// Track IDs are meaningful only for the currently loaded file.
struct CoreOptions {
    bool preferHardware = true;
    bool forceSdr = false;
    bool pitchCorrection = true;
    bool exclusiveAudio = false;
    bool passthrough = false;
    bool dynamicPeak = true;
    std::string audioDevice = "auto";
    std::string audioLanguages;
    std::string subtitleLanguages;
    std::string toneMapping = "auto";
    std::string gamutMapping = "auto";
    double targetPeak = 0;       // 0 selects the display's automatic peak.
    double referenceWhite = 0;   // 0 selects mpv's reference-white policy.
    double peakDecay = 20;
    std::wstring shaderPath;
};

struct MediaTrack {
    int64_t id = -1;
    std::string type;            // "audio", "sub", or "video".
    std::string language;
    std::string codec;
    std::wstring title;
    bool selected = false;
    bool defaultTrack = false;
    bool forced = false;
    bool external = false;
    int channels = 0;
    int sampleRate = 0;
};

struct AudioDevice {
    std::string id;
    std::wstring description;
};

// List snapshots are separate so frequent timeline refreshes copy no lists.
struct CorePlaybackState {
    bool available = false;
    bool loading = false;
    bool ready = false;
    bool failed = false;
    bool eof = false;
    bool paused = true;
    bool seeking = false;
    bool seekable = false;
    bool buffering = false;
    bool hasAudio = false;
    bool hasVideo = false;
    bool hardware = false;
    bool supportsPcmTap = false;
    bool passthroughActive = false;
    bool audioLost = false;
    bool hdrInput = false;
    bool hdrOutput = false;
    bool shaderError = false;
    int width = 0;
    int height = 0;
    int inputDepth = 0;
    int outputDepth = 0;
    int audioChannels = 0;
    int audioSampleRate = 0;
    double position = 0;
    double duration = 0;
    double rate = 1;
    double abA = -1;
    double abB = -1;
    int64_t audioTrack = -1;
    int64_t subtitleTrack = -1;
    uint64_t fileGeneration = 0;
    uint64_t seekGeneration = 0;
    std::string hardwareDecoder;
    std::string videoCodec;
    std::string videoOutput;
    std::string audioOutput;
    std::string audioCodec;
    std::string audioDevice;
    std::string audioFormat;
    std::string actualInfo;
    std::wstring error;
    std::wstring audioFallbackReason;
    std::wstring shaderWarning;
};

struct CoreStatus : CorePlaybackState {
    std::vector<MediaTrack> tracks;
    std::vector<AudioDevice> audioDevices;
    std::vector<std::string> protocols;
};
