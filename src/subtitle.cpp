#include "subtitle.h"
#include <cstdlib>
#include <share.h>

static std::wstring TrimW(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool ReadTextFile(const std::wstring& file, std::wstring& out) {
    FILE* f = _wfsopen(file.c_str(), L"rb", _SH_DENYNO);
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0 || len > 32 * 1024 * 1024) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    std::string raw;
    if (len > 0) {
        raw.resize((size_t)len);
        size_t got = fread(&raw[0], 1, (size_t)len, f);
        raw.resize(got);
    }
    fclose(f);
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        out = Utf8ToW(raw.substr(3));
        return true;
    }
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        if (raw.size() % 2) return false;
        out.resize((raw.size() - 2) / 2);
        memcpy(out.data(), raw.data() + 2, raw.size() - 2);
        return true;
    }
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFE && (unsigned char)raw[1] == 0xFF) {
        if (raw.size() % 2) return false;
        out.resize((raw.size() - 2) / 2);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = (wchar_t)(((unsigned char)raw[2 + i * 2] << 8) | (unsigned char)raw[3 + i * 2]);
        return true;
    }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw.data(), (int)raw.size(), nullptr, 0);
    if (n > 0 || raw.empty()) {
        out = Utf8ToW(raw);
        return true;
    }
    n = MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), &w[0], n);
    out.swap(w);
    return true;
}

static void SplitLines(const std::wstring& text, std::vector<std::wstring>& lines) {
    lines.clear();
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find(L'\n', pos);
        std::wstring line = (e == std::wstring::npos) ? text.substr(pos) : text.substr(pos, e - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(line);
        if (e == std::wstring::npos) break;
        pos = e + 1;
    }
}

static bool ParseTimestamp(const std::wstring& in, double& out) {
    std::wstring s = TrimW(in);
    if (s.empty()) return false;
    size_t i = s.find(L"-->");
    if (i != std::wstring::npos) s = TrimW(s.substr(0, i));
    // strip trailing settings (VTT)
    size_t sp = s.find_first_of(L" \t");
    if (sp != std::wstring::npos) s = s.substr(0, sp);
    for (auto& c : s) if (c == L',') c = L'.';
    if (s.find_first_not_of(L"0123456789:.") != std::wstring::npos) return false;
    size_t c1 = s.find(L':');
    if (c1 == std::wstring::npos) return false;
    size_t c2 = s.find(L':', c1 + 1);
    std::wstring hours = c2 == std::wstring::npos ? L"0" : s.substr(0, c1);
    std::wstring minutes = c2 == std::wstring::npos ? s.substr(0, c1) : s.substr(c1 + 1, c2 - c1 - 1);
    std::wstring seconds = s.substr((c2 == std::wstring::npos ? c1 : c2) + 1);
    if (hours.empty() || minutes.empty() || seconds.empty() ||
        hours.find_first_not_of(L"0123456789") != std::wstring::npos ||
        minutes.find_first_not_of(L"0123456789") != std::wstring::npos) return false;
    wchar_t* end = nullptr;
    double h = wcstod(hours.c_str(), nullptr), m = wcstod(minutes.c_str(), nullptr);
    double sec = wcstod(seconds.c_str(), &end);
    if (end == seconds.c_str() || *end || !std::isfinite(sec) || sec < 0 || sec >= 60 ||
        h > 9999 || m > 599999 || (c2 != std::wstring::npos && m >= 60)) return false;
    out = h * 3600.0 + m * 60.0 + sec;
    return true;
}

static std::wstring CleanText(const std::wstring& in, bool ass) {
    std::wstring out;
    out.reserve(in.size());
    bool inTag = false;
    for (size_t i = 0; i < in.size(); ++i) {
        wchar_t c = in[i];
        if (!ass && c == L'<') { inTag = true; continue; }
        if (!ass && c == L'>') { inTag = false; continue; }
        if (ass && c == L'{') { inTag = true; continue; }
        if (ass && c == L'}') { inTag = false; continue; }
        if (inTag) continue;
        if (ass && c == L'\\' && i + 1 < in.size()) {
            wchar_t n = in[i + 1];
            if (n == L'N' || n == L'n') { out += L'\n'; ++i; continue; }
            if (n == L'h') { out += L' '; ++i; continue; }
        }
        out += c;
    }
    return out;
}

bool SubtitleTrack::Load(const std::wstring& file) {
    std::wstring text;
    if (!ReadTextFile(file, text)) return false;
    std::vector<std::wstring> lines;
    SplitLines(text, lines);
    std::wstring ext = LowerExt(file);
    std::vector<SubCue> parsed;

    if (ext == L".ass" || ext == L".ssa") {
        bool inEvents = false;
        for (const auto& raw : lines) {
            std::wstring line = TrimW(raw);
            if (line.empty()) continue;
            if (line[0] == L'[') { inEvents = (_wcsicmp(line.c_str(), L"[Events]") == 0); continue; }
            if (!inEvents) continue;
            if (line.rfind(L"Dialogue:", 0) != 0) continue;
            std::wstring body = line.substr(9);
            std::vector<std::wstring> parts;
            size_t pos = 0;
            for (int k = 0; k < 9; ++k) {
                size_t c = body.find(L',', pos);
                if (c == std::wstring::npos) break;
                parts.push_back(body.substr(pos, c - pos));
                pos = c + 1;
            }
            if (parts.size() < 9) continue;
            SubCue cue;
            if (!ParseTimestamp(parts[1], cue.start)) continue;
            if (!ParseTimestamp(parts[2], cue.end)) continue;
            cue.text = CleanText(body.substr(pos), true);
            cue.text = TrimW(cue.text);
            if (!cue.text.empty() && cue.end > cue.start) parsed.push_back(cue);
        }
    } else {
        SubCue cur;
        bool have = false;
        for (const auto& raw : lines) {
            std::wstring line = TrimW(raw);
            if (line.empty()) {
                if (have && !cur.text.empty() && cur.end > cur.start) { parsed.push_back(cur); }
                cur = SubCue(); have = false;
                continue;
            }
            if (line.rfind(L"WEBVTT", 0) == 0) continue;
            if (line.find(L"-->") != std::wstring::npos) {
                if (have && !cur.text.empty() && cur.end > cur.start) parsed.push_back(cur);
                have = false;
                size_t a = line.find(L"-->");
                double s = 0, e = 0;
                if (ParseTimestamp(line.substr(0, a), s)) {
                    std::wstring rest = line.substr(a + 3);
                    if (ParseTimestamp(rest, e)) {
                        cur.start = s;
                        cur.end = e;
                        have = true;
                        cur.text.clear();
                    }
                }
                continue;
            }
            if (have) {
                if (!cur.text.empty()) cur.text += L'\n';
                cur.text += CleanText(line, false);
            }
        }
        if (have && !cur.text.empty() && cur.end > cur.start) parsed.push_back(cur);
    }

    for (auto& c : parsed) {
        c.text = TrimW(c.text);
    }
    if (parsed.empty()) return false;
    cues = std::move(parsed);
    std::stable_sort(cues.begin(), cues.end(), [](const SubCue& a, const SubCue& b) { return a.start < b.start; });
    Reindex();
    path = file;
    return !cues.empty();
}

void SubtitleTrack::Reindex() {
    latestEnd_.resize(cues.size());
    double end = 0;
    for (size_t i = 0; i < cues.size(); ++i) latestEnd_[i] = end = std::max(end, cues[i].end);
}

void SubtitleTrack::Append(std::vector<SubCue>& incoming) {
    bool indexed=latestEnd_.size()==cues.size();
    const auto repeated=[](const SubCue& a,const SubCue& b){
        const double overlap=std::min(a.end,b.end)-std::max(a.start,b.start);
        return a.text==b.text && std::fabs(a.start-b.start)<0.15 &&
            overlap>=0.8*std::min(a.end-a.start,b.end-b.start);
    };
    for (auto& cue : incoming) {
        if (cue.text.empty() || !std::isfinite(cue.start) || !std::isfinite(cue.end) || cue.start < 0 || cue.end <= cue.start) continue;
        if(indexed && !cues.empty() && repeated(cues.back(),cue)) {
            cues.back().end=std::max(cues.back().end,cue.end);
            latestEnd_.back()=std::max(latestEnd_.back(),cues.back().end);
            continue;
        }
        if(indexed && (cues.empty() || cue.start>cues.back().start)) {
            const double end=std::max(latestEnd_.empty()?0:latestEnd_.back(),cue.end);
            cues.push_back(std::move(cue)); latestEnd_.push_back(end);
            continue;
        }
        auto pos = std::lower_bound(cues.begin(), cues.end(), cue.start, [](const SubCue& c, double t) { return c.start < t; });
        // Preserve order and recognize replays on either side of the timestamp.
        if(pos!=cues.begin() && repeated(*std::prev(pos),cue))--pos;
        if (pos != cues.end() && repeated(*pos,cue)) pos->end=std::max(pos->end,cue.end);
        else cues.insert(pos, std::move(cue));
        indexed=false;
    }
    incoming.clear();
    if(!indexed) Reindex();
}

std::vector<SubCue> BilingualCaptions(const std::vector<SubCue>& originals,const std::vector<SubCue>& translated) {
    auto result=originals;
    for(auto& cue:result){
        auto it=std::lower_bound(translated.begin(),translated.end(),cue.start-0.15,
            [](const SubCue& c,double t){return c.start<t;});
        const auto suffix=L"\n"+cue.text;
        for(;it!=translated.end() && it->start<=cue.start+0.15;++it){
            if(std::fabs(it->end-cue.end)<=0.15 && it->text.size()>suffix.size() &&
                it->text.compare(it->text.size()-suffix.size(),suffix.size(),suffix)==0){cue.text=it->text;break;}
        }
    }
    return result;
}

const std::wstring* SubtitleTrack::At(double t) const {
    if (cues.empty() || !std::isfinite(t)) return nullptr;
    auto it = std::upper_bound(cues.begin(), cues.end(), t,
        [](double v, const SubCue& c) { return v < c.start; });
    while (it != cues.begin()) {
        --it;
        if (t >= it->start && t <= it->end) return &it->text;
        if (t > it->end && t - it->end < 0.12) return &it->text;
        const size_t i = (size_t)(it - cues.begin());
        if (latestEnd_.size() == cues.size() && (i == 0 || latestEnd_[i - 1] + 0.12 < t)) break;
    }
    return nullptr;
}

static std::wstring SrtTime(double t) {
    if (!std::isfinite(t) || t < 0) t = 0;
    long long ms = (long long)(std::min(t, 35999999.0) * 1000.0 + 0.5);
    int h = (int)(ms / 3600000); ms %= 3600000;
    int m = ms / 60000; ms %= 60000;
    int s = ms / 1000; ms %= 1000;
    wchar_t buf[64];
    swprintf(buf, 64, L"%02d:%02d:%02d,%03d", h, m, s, ms);
    return buf;
}

bool SaveSrt(const std::wstring& file, const std::vector<SubCue>& cues) {
    std::string out;
    out += "\xEF\xBB\xBF";
    int idx = 1;
    for (const auto& c : cues) {
        if (c.text.empty() || !std::isfinite(c.start) || !std::isfinite(c.end) || c.end <= c.start) continue;
        std::wstring block = std::to_wstring(idx++) + L"\r\n" + SrtTime(c.start) + L" --> " + SrtTime(c.end) + L"\r\n" + c.text + L"\r\n\r\n";
        out += WToUtf8(block);
    }
    std::wstring temp = file + L".lumen-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, out.data(), (DWORD)out.size(), &written, nullptr) && written == out.size();
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    if (ok) ok = MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

std::wstring SubtitleSidecar(const std::wstring& mediaPath) {
    size_t slash = mediaPath.find_last_of(L"\\/");
    size_t dot = mediaPath.find_last_of(L'.');
    std::wstring base = mediaPath;
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) base = mediaPath.substr(0, dot);
    const wchar_t* exts[] = { L".srt", L".vtt", L".ass", L".ssa", L".smi", L".sami" };
    for (auto e : exts) {
        std::wstring p = base + e;
        if (FileExists(p)) return p;
    }
    return L"";
}
