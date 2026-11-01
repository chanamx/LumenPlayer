#pragma once
#include "common.h"
#include "subtitle.h"

// Offline streaming captions. Existing Chinese plus selectable Moonshine models.
class LiveCaptions {
public:

    struct Impl;
    LiveCaptions();
    ~LiveCaptions();

    bool Probe();
    bool Configure(const std::string& modelId,const std::wstring& modelDirectory);
    const std::string& ModelId() const { return selectedModel_; }
    std::wstring ModelName() const;
    uint64_t TimelineGeneration();
    bool Available() const { return !dllPath_.empty() && !packDir_.empty(); }
    const std::wstring& DllPath() const { return dllPath_; }
    const std::wstring& PackDir() const { return packDir_; }
    const std::wstring& ModelPath() const { return modelPath_; }
    const std::wstring& Locale() const { return locale_; }

    bool Start(const std::wstring& preferredLocale = L"cmn-Hans-CN");
    void RequestStop(); // Stop input immediately; Stop() still drains and joins.
    void Stop();
    bool Running() const;

    // Audio thread: interleaved float mix samples + media timestamp of this chunk.
    void PushMix(const float* interleaved, int frames, int rate, int channels, double mediaPts);
    void ResetTimeline(double mediaPts);
    void FinishInput(double mediaPts);

    // UI thread
    std::wstring Partial();
    void TakeFinals(std::vector<SubCue>& out,uint64_t* generation=nullptr);
    size_t FinalCount();
    void ClearResults();

private:
    bool StartZipformer();
    bool StartMoonshine();

    Impl* impl_ = nullptr;

    std::atomic<bool> active_{false};
    std::wstring dllPath_;
    std::wstring packDir_;
    std::wstring modelPath_;
    std::wstring locale_;
    std::string selectedModel_="zipformer-zh";
    std::wstring configuredDirectory_;
};
