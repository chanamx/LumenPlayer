#pragma once
#include "common.h"

struct SubCue {
    double start = 0;
    double end = 0;
    std::wstring text;
};

class SubtitleTrack {
public:
    std::vector<SubCue> cues;
    std::wstring path;

    bool Load(const std::wstring& file);
    void Clear() { std::vector<SubCue>().swap(cues); path.clear(); std::vector<double>().swap(latestEnd_); }
    void Append(std::vector<SubCue>& incoming);
    // Returns the text of the cue active at t (nullptr if none)
    const std::wstring* At(double t) const;
    size_t Count() const { return cues.size(); }
private:
    void Reindex();
    std::vector<double> latestEnd_;
};

bool SaveSrt(const std::wstring& file, const std::vector<SubCue>& cues);
std::vector<SubCue> BilingualCaptions(const std::vector<SubCue>& originals, const std::vector<SubCue>& translated);
std::wstring SubtitleSidecar(const std::wstring& mediaPath);
