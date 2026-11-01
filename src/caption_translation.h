#pragma once
#include "caption_models.h"
#include "subtitle.h"
#include <memory>

class CaptionTranslation {
public:
    struct Result { uint64_t generation=0; SubCue source; std::wstring translation; };
    CaptionTranslation();
    ~CaptionTranslation();
    bool Start(const CaptionModel& model,const std::wstring& directory,const std::string& sourceLanguage,const std::string& targetLanguage);
    void RequestStop();
    void Stop();
    void Reset(uint64_t generation);
    void Submit(const SubCue& cue,uint64_t generation);
    void TakeResults(std::vector<Result>& results);
    std::wstring Status();
    bool Active() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
