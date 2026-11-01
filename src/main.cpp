#include "common.h"
#include "gfx.h"
#include "player.h"
#include "subtitle.h"
#include "captions.h"
#include "caption_models.h"
#include "caption_translation.h"
#include "model_dialog.h"
#include "playlist.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <windowsx.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <cstdlib>
#include <exception>
#include <map>
#include <memory>
#include <climits>

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static HINSTANCE g_inst = nullptr;
static HWND g_main = nullptr;
static HWND g_hwVideo = nullptr;
static HWND g_swVideo = nullptr;
static Gfx g_gfx;
static LayeredWindow g_overlay;
static Player g_player;
static LiveCaptions g_captions;
static CaptionModelStore g_modelStore;
static CaptionTranslation g_translation;
static SubtitleTrack g_translated;
static SubCue g_translationDisplay;
static std::wstring g_translationSource;
static uint64_t g_translationGeneration=0,g_lastAsrGeneration=UINT64_MAX;
static std::string g_activeCaptionModel,g_activeTranslationModel;
static SubtitleTrack g_subs;
static SubtitleTrack g_live;
static Playlists g_lists;
static unsigned g_queueFolder = UINT_MAX;
static bool g_queueEnded = false;

static std::wstring g_file;
static bool g_hasMedia = false;
static bool g_coreAwait = false;
static bool g_coreHadAudio = false;
static std::wstring g_coreSidecar;
static bool g_fullscreen = false;
static bool g_maximized = false;
static WINDOWPLACEMENT g_wpPrev = { sizeof(WINDOWPLACEMENT) };
static float g_u = 1.0f;
static bool g_opening = false;
static std::thread g_captionStart;
static std::atomic<bool> g_captionStarting{false};
static std::atomic<bool> g_captionCancel{false};
static std::atomic<int> g_captionResult{0};
static std::thread g_captionStop;
static std::atomic<bool> g_captionStopped{false};
static bool g_captionStopping=false, g_captionSavePending=false;
static uint64_t g_captionSession=0, g_captionStopSession=0;
struct SourceRequest { std::wstring path; bool play=true, importList=true; unsigned folder=UINT_MAX; int index=-1; };
static std::deque<SourceRequest> g_sourceQueue;
static std::thread g_sourceWorker;
static std::shared_ptr<std::atomic<bool>> g_sourceCancel;
static std::atomic<bool> g_sourceDone{false};
static SourceRequest g_sourceRequest;
static PlaylistImport g_sourceList;
static std::unique_ptr<FfmpegReader> g_sourceReader;
static std::wstring g_sourceError;

struct Config {
    int hw = 1;
    int subs = 1;
    int captions = 0;
    bool rightSpeed = true;
    float holdRate = 2.0f;
    float subScale = 1.0f;
    float volume = 0.85f;
    float playlistWidth = 320.0f;
    std::wstring shaderPath;
    bool forceSdr = false;
    CaptionSelection captionSelection;
};
static Config g_cfg;

static std::wstring ShaderPathHex(const std::wstring& path) {
    const wchar_t* digits=L"0123456789abcdef";std::wstring value;
    for(unsigned char c:WToUtf8(path)) {value+=digits[c>>4];value+=digits[c&15];}return value;
}
static std::wstring ShaderPathFromHex(const std::wstring& value) {
    if(value.size()%2)return L"";std::string bytes;
    auto digit=[](wchar_t c)->int {if(c>=L'0'&&c<=L'9')return c-L'0';if(c>=L'a'&&c<=L'f')return c-L'a'+10;if(c>=L'A'&&c<=L'F')return c-L'A'+10;return -1;};
    for(size_t i=0;i<value.size();i+=2) {int a=digit(value[i]),b=digit(value[i+1]);if(a<0||b<0)return L"";bytes+=(char)(a*16+b);}
    return bytes.find('\0')==std::string::npos?Utf8ToW(bytes):L"";
}

struct UIState {
    bool settings = false;
    bool playlist = false;
    float listScroll = 0;
    float listScrollMax = 0;
    int listSelection = 0;
    bool listScrollDrag = false;
    float listDragY = 0, listDragScroll = 0;
    bool listResizeDrag = false, listResizeMoved = false, viewportDirty = false;
    float listResizeX = 0, listResizeWidth = 0;
    bool subsOn = true;
    bool captionsOn = false;
    float subScale = 1.0f;
    float volume = 0.85f;
    float restoreVolume = 0.85f;
    bool seekDrag = false;
    bool volDrag = false;
    bool controlsVisible = true;
    double lastActivity = 0;
    bool pressInVideo = false;
    double pressTime = 0;
    POINT pressPt = { 0, 0 };
    bool ffActive = false;
    bool ffScrub = false;
    double lastScrub = 0;
    double ffRestoreRate = 1;
    bool pendingClick = false;
    double pendingClickTime = 0;
    bool suppressClick = false;
    std::wstring toast;
    double toastTime = 0;
    bool hoverSeek = false;
    bool subDrag = false;
    bool rateDrag = false;
    bool rightFF = false;
    bool rightKeyDown = false;
    bool rightKeyUsed = false;
    double rightKeyTime = 0;
    bool seekWasPlaying = false;
    double seekSubmitted = -1;
    double seekRequestTime = 0;
    bool dirty = true;
    bool help = false;
    bool ffWasPlaying = false;
    bool clickWasPlaying = false;
    bool keyboardFocus = false;
    int focus = -1;
    float settingsScroll = 0;
    float settingsScrollMax = 0;
    float seekPreview = 0;
    float subtitleTop = 0;
    POINT mouse = {-100, -100};
    double hoverTime = 0;
};
static UIState g_ui;

struct Rects {
    D2D1_RECT_F title{};
    D2D1_RECT_F video{};
    D2D1_RECT_F bar{};
    D2D1_RECT_F panel{};
    D2D1_RECT_F play{}, back{}, fwd{}, prev{}, next{}, list{}, more{}, seek{}, seekHit{}, volIcon{}, volTrack{}, cc{}, gear{}, fs{};
    D2D1_RECT_F listPanel{}, listClose{}, listReset{}, listDivider{}, listFolder{}, listAdd{}, listImport{}, listView{};
    D2D1_RECT_F tbMin{}, tbMax{}, tbClose{};
    D2D1_RECT_F openBtn{};
    D2D1_RECT_F sHw{}, sVideo{}, sPlayback{}, sVideoStatus{}, sSubs{}, sImport{}, sSubSize{}, sCaptions{}, sCaptionsSave{}, sModels{}, sClose{};
    D2D1_RECT_F sTitle{}, sLabelDecode{}, sStatusHw{}, sLabelSubs{}, sLabelCaptions{}, sStatusCaptions{}, sHint{};
    D2D1_RECT_F sLabelSpeed{}, sRightSpeed{}, sHoldRate{};
    D2D1_RECT_F settingsView{}, topOpen{}, time{}, badge{}, helpClose{};
};
static Rects g_r;
static void CancelGesture();
static void ShowCaptionModels();
static void RequestSource(const std::wstring& path,bool play=true,unsigned folder=UINT_MAX,int index=-1,bool importList=true);
static void CancelSources(bool wait=false);
static void PollSources();
static void OpenNetworkDialog(bool play=true);
static void OpenMediaMenu(bool play=true);
static void CoreSettingsMenu(bool videoOnly=false);

// ---------------------------------------------------------------------------
// Drawing helpers (software D2D)
// ---------------------------------------------------------------------------
static D2D1_COLOR_F C(int r, int g, int b, float a = 1.0f) {
    D2D1_COLOR_F c;
    c.r = r / 255.0f; c.g = g / 255.0f; c.b = b / 255.0f; c.a = a;
    return c;
}
static const D2D1_COLOR_F kAccent = { 0.357f, 0.549f, 1.0f, 1.0f };
static const D2D1_COLOR_F kText = { 0.925f, 0.933f, 0.957f, 1.0f };
static const D2D1_COLOR_F kDim = { 0.541f, 0.565f, 0.627f, 1.0f };

static Com<ID2D1SolidColorBrush> g_brush;
static ID2D1SolidColorBrush* Brush(ID2D1RenderTarget* rt, D2D1_COLOR_F c) {
    if (!g_brush && FAILED(rt->CreateSolidColorBrush(c, g_brush.put()))) return nullptr;
    g_brush->SetColor(c);
    return g_brush.get();
}

static void FillRect(ID2D1RenderTarget* rt, D2D1_RECT_F r, D2D1_COLOR_F c) {
    auto* b = Brush(rt, c);
    if (b) rt->FillRectangle(r, b);
}
static void FillRound(ID2D1RenderTarget* rt, D2D1_RECT_F r, float rad, D2D1_COLOR_F c) {
    auto* b = Brush(rt, c);
    if (b) rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), b);
}
static void StrokeRound(ID2D1RenderTarget* rt, D2D1_RECT_F r, float rad, D2D1_COLOR_F c, float w = 1.0f) {
    auto* b = Brush(rt, c);
    if (b) rt->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), b, w);
}

static std::map<std::wstring, Com<IDWriteTextFormat>> g_tf;
// Keeps string storage alive until EndDraw: D2D may defer text shaping.
static std::deque<std::wstring> g_textKeep;
static IDWriteTextFormat* TF(float size, int weight, int align, bool icon, bool wrap) {
    wchar_t key[160];
    swprintf(key, 160, L"%d_%d_%d_%d_%d", (int)(size * 10), weight, align, (int)icon, (int)wrap);
    auto it = g_tf.find(key);
    if (it != g_tf.end()) return it->second.get();
    if (g_tf.size() >= 96) g_tf.clear();
    Com<IDWriteTextFormat> f;
    HRESULT hr = g_gfx.DWrite()->CreateTextFormat(
        icon ? L"Segoe MDL2 Assets" : L"Microsoft YaHei UI", nullptr,
        (DWRITE_FONT_WEIGHT)weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"zh-CN", f.put());
    if (FAILED(hr)) return nullptr;
    f->SetTextAlignment(align == 0 ? DWRITE_TEXT_ALIGNMENT_LEADING : (align == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_TRAILING));
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (!wrap && !icon) {
        Com<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(g_gfx.DWrite()->CreateEllipsisTrimmingSign(f.get(), ellipsis.put()))) {
            DWRITE_TRIMMING trim = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            f->SetTrimming(&trim, ellipsis.get());
        }
    }
    g_tf[key] = f;
    return f.get();
}

static void Text(ID2D1RenderTarget* rt, const std::wstring& s, D2D1_RECT_F r, float size,
    D2D1_COLOR_F c, int align = 0, int weight = 400, bool icon = false, bool wrap = false) {
    if (s.empty()) return;
    auto* f = TF(size, weight, align, icon, wrap);
    auto* b = Brush(rt, c);
    if (!f || !b) return;
    g_textKeep.push_back(s);
    const std::wstring& kept = g_textKeep.back();
    rt->DrawText(kept.c_str(), (UINT32)kept.size(), f, r, b, D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}

static void Toggle(ID2D1RenderTarget* rt, D2D1_RECT_F r, bool on) {
    float h = r.bottom - r.top;
    float w = h * 1.9f;
    D2D1_RECT_F pill = { r.right - w, r.top + (h - h * 0.58f) / 2, r.right, r.top + (h + h * 0.58f) / 2 };
    FillRound(rt, pill, (pill.bottom - pill.top) / 2, on ? kAccent : C(60, 64, 76));
    float knob = (pill.bottom - pill.top) - 4.0f;
    float kx = on ? pill.right - knob - 2.0f : pill.left + 2.0f;
    FillRound(rt, D2D1::RectF(kx, pill.top + 2.0f, kx + knob, pill.top + 2.0f + knob), knob / 2, C(255, 255, 255));
}

static void Toast(const std::wstring& s) {
    g_ui.toast = s;
    g_ui.toastTime = QpcNow();
    g_ui.dirty = true;
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
static std::wstring ConfigPath() {
#ifdef LUMEN_TEST
    return PathJoin(ExeDir(), L"..\\test\\results\\test-settings.ini");
#else
    return PathJoin(ExeDir(), L"LumenPlayer.ini");
#endif
}

static std::wstring PlaylistPath() {
#ifdef LUMEN_TEST
    return PathJoin(ExeDir(), L"..\\test\\results\\test-playlists.dat");
#else
    return PathJoin(ExeDir(), L"LumenPlaylists.dat");
#endif
}

static void SavePlaylists() {
    if (!g_lists.Save(PlaylistPath())) Toast(L"播放列表保存失败，本次仍可使用；已有数据未被覆盖");
}

static int QueueIndex() {
    static int hint=-1;
    const auto* folder=g_lists.Find(g_queueFolder);
    if(!folder)return -1;
    // A hint is usable only while the actual entry still matches this media.
    if(hint>=0 && hint<(int)folder->files.size() && MediaEqual(folder->files[hint],g_file))return hint;
    return hint=g_lists.IndexOf(g_queueFolder,g_file);
}
static bool CanStepPlaylist(int direction) {
    const auto* folder = g_lists.Find(g_queueFolder);
    const int index = QueueIndex();
    return folder && index >= 0 && index + direction >= 0 && index + direction < (int)folder->files.size();
}

static void LoadConfig() {
    std::wstring p = ConfigPath();
    g_cfg.hw = GetPrivateProfileIntW(L"player", L"hardware", 1, p.c_str());
    g_cfg.subs = GetPrivateProfileIntW(L"player", L"subtitles", 1, p.c_str());
    // Keep the old enabled key so existing caption preferences survive.
    g_cfg.captions = GetPrivateProfileIntW(L"player", L"soda", 0, p.c_str());
    wchar_t model[128]{};
    GetPrivateProfileStringW(L"captions",L"recognition",L"zipformer-zh",model,128,p.c_str());g_cfg.captionSelection.recognition=WToUtf8(model);
    const auto* selected=FindCaptionModel(g_cfg.captionSelection.recognition);
    if(!selected||selected->Translation())g_cfg.captionSelection.recognition="zipformer-zh";
    GetPrivateProfileStringW(L"captions",L"translation",L"",model,128,p.c_str());g_cfg.captionSelection.translation=WToUtf8(model);
    selected=FindCaptionModel(g_cfg.captionSelection.translation);if(!selected||!selected->Translation())g_cfg.captionSelection.translation.clear();
    GetPrivateProfileStringW(L"captions",L"target",L"zh",model,128,p.c_str());g_cfg.captionSelection.target=WToUtf8(model);
    if(g_cfg.captionSelection.target!="zh"&&g_cfg.captionSelection.target!="en"&&g_cfg.captionSelection.target!="ja"&&g_cfg.captionSelection.target!="de"&&g_cfg.captionSelection.target!="fr")g_cfg.captionSelection.target="zh";
    g_cfg.rightSpeed = GetPrivateProfileIntW(L"player", L"rightspeed", 1, p.c_str()) != 0;
    g_cfg.forceSdr=GetPrivateProfileIntW(L"video",L"force_sdr",0,p.c_str())!=0;
    wchar_t shader[65536]{};GetPrivateProfileStringW(L"video",L"glsl_shader_utf8_hex",L"",shader,65536,p.c_str());g_cfg.shaderPath=ShaderPathFromHex(shader);
    g_player.ConfigureVideo(g_cfg.shaderPath,g_cfg.forceSdr);
    auto core=g_player.PlaybackOptions();
    core.preferHardware=g_cfg.hw!=0;
    auto textSetting=[&](const wchar_t* key,const wchar_t* fallback){wchar_t value[4096]{};GetPrivateProfileStringW(L"playback",key,fallback,value,4096,p.c_str());return std::wstring(value);};
    core.audioDevice=WToUtf8(ShaderPathFromHex(textSetting(L"audio_device_hex",L"")));if(core.audioDevice.empty())core.audioDevice="auto";
    core.audioLanguages=WToUtf8(textSetting(L"audio_languages",L""));core.subtitleLanguages=WToUtf8(textSetting(L"subtitle_languages",L""));
    core.exclusiveAudio=GetPrivateProfileIntW(L"playback",L"exclusive",0,p.c_str())!=0;
    core.passthrough=GetPrivateProfileIntW(L"playback",L"passthrough",0,p.c_str())!=0;
    core.pitchCorrection=GetPrivateProfileIntW(L"playback",L"pitch_correction",1,p.c_str())!=0;
    core.dynamicPeak=GetPrivateProfileIntW(L"playback",L"dynamic_peak",1,p.c_str())!=0;
    core.toneMapping=WToUtf8(textSetting(L"tone_mapping",L"auto"));core.gamutMapping=WToUtf8(textSetting(L"gamut_mapping",L"auto"));
    core.targetPeak=Clampd(_wtof(textSetting(L"target_peak",L"0").c_str()),0,10000);
    core.referenceWhite=Clampd(_wtof(textSetting(L"reference_white",L"0").c_str()),0,1000);
    core.peakDecay=Clampd(_wtof(textSetting(L"peak_decay",L"20").c_str()),1,1000);
    g_player.ConfigurePlayback(core);
#ifdef LUMEN_TEST
    const int defaultCore=0;
#else
    const int defaultCore=1;
#endif
    g_player.UseModernCore(GetPrivateProfileIntW(L"playback",L"modern_core",defaultCore,p.c_str())!=0);
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(L"player", L"holdrate", L"2.0", buf, 64, p.c_str());
    const float rate = (float)_wtof(buf);
    g_cfg.holdRate = std::isfinite(rate) ? Clampf(std::round(rate * 4) / 4, 1.25f, 8.0f) : 2.0f;
    GetPrivateProfileStringW(L"player", L"playlistwidth", L"320", buf, 64, p.c_str());
    const float width = (float)_wtof(buf);
    g_cfg.playlistWidth = std::isfinite(width) ? Clampf(width, 240, 10000) : 320;
    GetPrivateProfileStringW(L"player", L"subscale", L"1.0", buf, 64, p.c_str());
    g_cfg.subScale = Clampf((float)_wtof(buf), 0.6f, 2.2f);
    GetPrivateProfileStringW(L"player", L"volume", L"0.85", buf, 64, p.c_str());
    g_cfg.volume = Clampf((float)_wtof(buf), 0.0f, 1.0f);
    g_ui.subsOn = g_cfg.subs != 0;
    g_ui.subScale = g_cfg.subScale;
    g_ui.volume = g_cfg.volume;
    g_ui.restoreVolume = g_ui.volume > 0.001f ? g_ui.volume : 0.85f;
    g_ui.captionsOn = g_cfg.captions != 0;
}

static void SaveConfig() {
    std::wstring p = ConfigPath();
    WritePrivateProfileStringW(L"player", L"hardware", g_cfg.hw ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"player", L"subtitles", g_ui.subsOn ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"player", L"soda", g_ui.captionsOn ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"captions",L"recognition",Utf8ToW(g_cfg.captionSelection.recognition).c_str(),p.c_str());
    WritePrivateProfileStringW(L"captions",L"translation",Utf8ToW(g_cfg.captionSelection.translation).c_str(),p.c_str());
    WritePrivateProfileStringW(L"captions",L"target",Utf8ToW(g_cfg.captionSelection.target).c_str(),p.c_str());
    WritePrivateProfileStringW(L"player", L"rightspeed", g_cfg.rightSpeed ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"video",L"force_sdr",g_cfg.forceSdr?L"1":L"0",p.c_str());
    // ASCII storage preserves emoji and paths outside the system ANSI codepage.
    WritePrivateProfileStringW(L"video",L"glsl_shader_utf8_hex",ShaderPathHex(g_cfg.shaderPath).c_str(),p.c_str());
    wchar_t buf[64];
    auto core=g_player.PlaybackOptions();
    auto save=[&](const wchar_t* key,const std::wstring& value){WritePrivateProfileStringW(L"playback",key,value.c_str(),p.c_str());};
    save(L"modern_core",g_player.ModernCoreEnabled()?L"1":L"0");
    save(L"audio_device_hex",ShaderPathHex(Utf8ToW(core.audioDevice)));
    save(L"audio_languages",Utf8ToW(core.audioLanguages));save(L"subtitle_languages",Utf8ToW(core.subtitleLanguages));
    save(L"exclusive",core.exclusiveAudio?L"1":L"0");save(L"passthrough",core.passthrough?L"1":L"0");
    save(L"pitch_correction",core.pitchCorrection?L"1":L"0");save(L"dynamic_peak",core.dynamicPeak?L"1":L"0");
    save(L"tone_mapping",Utf8ToW(core.toneMapping));save(L"gamut_mapping",Utf8ToW(core.gamutMapping));
    for(auto setting:{std::make_pair(L"target_peak",core.targetPeak),std::make_pair(L"reference_white",core.referenceWhite),std::make_pair(L"peak_decay",core.peakDecay)}) {
        swprintf(buf,64,L"%.3f",setting.second);save(setting.first,buf);
    }
    swprintf(buf, 64, L"%.2f", g_cfg.holdRate);
    WritePrivateProfileStringW(L"player", L"holdrate", buf, p.c_str());
    swprintf(buf, 64, L"%.2f", g_cfg.playlistWidth);
    WritePrivateProfileStringW(L"player", L"playlistwidth", buf, p.c_str());
    swprintf(buf, 64, L"%.2f", g_ui.subScale);
    WritePrivateProfileStringW(L"player", L"subscale", buf, p.c_str());
    swprintf(buf, 64, L"%.2f", g_ui.volume);
    WritePrivateProfileStringW(L"player", L"volume", buf, p.c_str());
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static float S(float v) { return v * g_u; }
static D2D1_RECT_F RectF(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }

static float PlaylistWidth(float width) {
    const float maximum = std::max(0.0f, width - S(320));
    return Clampf(S(g_cfg.playlistWidth), std::min(S(240), maximum), maximum);
}

static float PlaybackWidth(float width) { return g_ui.playlist ? width - PlaylistWidth(width) : width; }

static void ComputeLayout(int cw, int ch) {
    float W = (float)cw, H = (float)ch;
    const float mediaW = PlaybackWidth(W);
    float titleH = g_fullscreen ? 0.0f : S(44);
    g_r.title = RectF(0, 0, W, titleH);
    g_r.video = RectF(0, titleH, mediaW, H - titleH);

    const bool tiny = mediaW < S(500);
    float barH = S(tiny ? 112 : 88);
    g_r.bar = RectF(0, H - barH, mediaW, barH);

    float seekY = H - barH + S(tiny ? 42 : 22);
    const float inset = S(tiny ? 18 : 26);
    g_r.seek = RectF(inset, seekY - S(9), mediaW - 2*inset, S(18));
    g_r.seekHit = D2D1::RectF(g_r.seek.left, g_r.seek.top - S(10), g_r.seek.right, g_r.seek.bottom + S(10));

    float rowCY = H - S(tiny ? 28 : 34);
    const bool compact = mediaW < S(760);
    g_r.prev = RectF(S(tiny ? 12 : compact ? 18 : 24), rowCY - S(16), S(30), S(34));
    g_r.play = RectF(S(tiny ? 48 : compact ? 54 : 62), rowCY - S(18), S(40), S(40));
    g_r.next = RectF(S(tiny ? 94 : compact ? 100 : 110), rowCY - S(16), S(30), S(34));
    g_r.back = compact ? RectF(0, 0, 0, 0) : RectF(S(148), rowCY - S(16), S(32), S(34));
    g_r.fwd = compact ? RectF(0, 0, 0, 0) : RectF(S(184), rowCY - S(16), S(32), S(34));
    g_r.seekHit.bottom = std::min(g_r.seekHit.bottom, g_r.play.top - S(2));

    float right = mediaW - S(tiny ? 12 : 24);
    g_r.fs = RectF(right - S(34), rowCY - S(17), S(34), S(34)); right -= S(42);
    g_r.gear = tiny ? RectF(0,0,0,0) : RectF(right - S(34), rowCY - S(17), S(34), S(34));
    g_r.more = tiny ? RectF(right - S(34), rowCY - S(17), S(34), S(34)) : RectF(0,0,0,0);
    right -= S(42);
    g_r.cc = mediaW >= S(560) ? RectF(right - S(38), rowCY - S(17), S(38), S(34)) : RectF(0,0,0,0);
    if (mediaW >= S(560)) right -= S(48);
    g_r.list = RectF(right - S(34), rowCY - S(17), S(34), S(34)); right -= S(42);
    const float volumeW = S(compact ? 48 : 78);
    g_r.volTrack = mediaW >= S(560) ? RectF(right - volumeW, rowCY - S(15), volumeW, S(30)) : RectF(0,0,0,0);
    if (mediaW >= S(560)) right -= volumeW + S(14);
    g_r.volIcon = RectF(right - S(30), rowCY - S(17), S(30), S(34));
    const float timeX = S(compact ? 140 : 228);
    g_r.time = tiny ? RectF(S(18), g_r.bar.top+S(3), mediaW-S(36), S(20)) : RectF(timeX, g_r.play.top, std::max(0.0f, g_r.volIcon.left - S(12) - timeX), S(40));
    g_r.badge = mediaW >= S(920) ? RectF(g_r.time.right - S(54), g_r.play.top + S(7), S(46), S(22)) : RectF(0, 0, 0, 0);
    if (mediaW >= S(920)) g_r.time.right -= S(62);

    float tbW = S(46);
    g_r.tbClose = RectF(W - tbW, 0, tbW, titleH);
    g_r.tbMax = RectF(W - tbW * 2, 0, tbW, titleH);
    g_r.tbMin = RectF(W - tbW * 3, 0, tbW, titleH);
    g_r.topOpen = RectF(g_r.tbMin.left - S(42), S(5), S(34), S(34));

    float pw = std::min(S(348), W);
    float ptop = titleH;
    g_r.panel = RectF(W - pw, ptop, pw, H - ptop);
    float px = W - pw + S(22);
    float y = ptop + S(14);
    g_r.sTitle = RectF(px, y, pw - S(44), S(28));
    g_r.sClose = RectF(W - S(46), y - S(2), S(30), S(30));
    g_r.settingsView = D2D1::RectF(W - pw + S(1), ptop + S(58), W, H - S(44));
    g_ui.settingsScrollMax = std::max(0.0f, S(696) - (g_r.settingsView.bottom - g_r.settingsView.top));
    g_ui.settingsScroll = Clampf(g_ui.settingsScroll, 0, g_ui.settingsScrollMax);
    y = g_r.settingsView.top + S(8) - g_ui.settingsScroll;

    g_r.sLabelDecode = RectF(px, y, pw - S(44), S(20)); y += S(24);
    g_r.sHw = RectF(px, y, pw - S(44), S(32)); y += S(36);
    g_r.sStatusHw = RectF(px, y, pw - S(44), S(30)); y += S(38);
    g_r.sVideo = RectF(px,y,pw-S(44),S(32));y+=S(36);
    g_r.sVideoStatus=RectF(px,y,pw-S(44),S(32));y+=S(44);
    g_r.sPlayback=RectF(px,y,pw-S(44),S(32));y+=S(40);

    g_r.sLabelSpeed = RectF(px, y, pw - S(44), S(20)); y += S(24);
    g_r.sRightSpeed = RectF(px, y, pw - S(44), S(32)); y += S(36);
    g_r.sHoldRate = RectF(px, y, pw - S(44), S(32)); y += S(44);

    g_r.sLabelSubs = RectF(px, y, pw - S(44), S(20)); y += S(24);
    g_r.sImport = RectF(px, y, S(140), S(32)); y += S(40);
    g_r.sSubs = RectF(px, y, pw - S(44), S(30)); y += S(34);
    g_r.sSubSize = RectF(px, y, pw - S(44), S(32)); y += S(44);

    g_r.sLabelCaptions = RectF(px, y, pw - S(44), S(20)); y += S(24);
    g_r.sCaptions = RectF(px, y, pw - S(44), S(32)); y += S(36);
    g_r.sModels = RectF(px,y,pw-S(44),S(32));y+=S(40);
    g_r.sStatusCaptions = RectF(px, y, pw - S(44), S(30)); y += S(38);
    g_r.sCaptionsSave = RectF(px, y, S(160), S(32)); y += S(38);
    g_r.sHint = RectF(px, y, pw - S(44), S(48));

    g_r.openBtn = RectF(mediaW / 2 - S(88), std::min(H / 2 + S(46), g_r.bar.top - S(58)), S(176), S(46));
    pw = PlaylistWidth(W); px = W - pw + S(22);
    g_r.listPanel = RectF(W - pw, ptop, pw, H - ptop);
    g_r.listDivider = RectF(g_r.listPanel.left - S(4), ptop, S(8), H - ptop);
    g_r.listClose = RectF(W - S(46), ptop + S(12), S(30), S(30));
    g_r.listReset = RectF(W - S(98), ptop + S(12), S(44), S(30));
    g_r.listFolder = RectF(px, ptop + S(54), pw - S(44), S(32));
    g_r.listAdd = RectF(px, ptop + S(94), (pw - S(54)) / 2, S(30));
    g_r.listImport = RectF(g_r.listAdd.right + S(10), g_r.listAdd.top, (pw - S(54)) / 2, S(30));
    g_r.listView = D2D1::RectF(px - S(10), ptop + S(132), W - S(10), std::max(ptop + S(132), H - S(28)));
    g_ui.listScrollMax = std::max(0.0f, S(56) * (float)g_lists.Active().files.size() - (g_r.listView.bottom - g_r.listView.top));
    g_ui.listScroll = Clampf(g_ui.listScroll, 0, g_ui.listScrollMax);
    g_r.helpClose = RectF(std::min(W - S(24), W / 2 + S(230)) - S(30),
        std::max(g_r.video.top + S(12), H / 2 - S(220)), S(30), S(30));
}

static bool InRect(D2D1_RECT_F r, float x, float y) {
    return r.right > r.left && r.bottom > r.top && x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

struct Control { D2D1_RECT_F rect; const wchar_t* label; bool enabled = true; int slider = 0; };
static std::vector<Control> Controls() {
    if (g_ui.help) return {{g_r.helpClose, L"关闭快捷键帮助 (Esc)"}};
    if (g_ui.settings) return {
        {g_r.sClose, L"关闭设置 (Esc)"}, {g_r.sHw, L"硬解优先 / 软件解码 · 立即切换当前视频"},
        {g_r.sVideo,L"画质与 GLSL 着色器 · HDR / SDR 输出"},
        {g_r.sPlayback,L"音轨、字幕轨、音频输出与播放控制"},
        {g_r.sRightSpeed, L"按住键盘 → 倍速 · 松开恢复"},
        {g_r.sHoldRate, L"长按倍率 · 画面 / → · 1.25 至 8 倍 · 方向键调整", true, 4},
        {g_r.sImport, L"导入 SRT / VTT / ASS / SSA 字幕"}, {g_r.sSubs, L"显示 / 隐藏字幕 (S)"},
        {g_r.sSubSize, L"字幕大小 · 拖动或方向键调整", true, 3},
        {g_r.sCaptions, L"开启 / 关闭实时字幕 (C)"},
        {g_r.sModels,L"模型与语言 · 安装、选择或删除字幕模型"},
        {g_r.sCaptionsSave, L"导出已生成的字幕为 SRT", g_live.Count() > 0}};
    const bool showPlayback=g_ui.controlsVisible || !g_player.IsPlaying();
    std::vector<Control> controls;
    if(g_ui.playlist || !g_fullscreen || !g_hasMedia || showPlayback)controls.reserve(24);
    if (g_ui.playlist) controls = {
        {g_r.listClose, L"关闭播放列表 (Esc / P)"}, {g_r.listFolder, L"选择 / 新建 / 管理列表文件夹"},
        {g_r.listAdd, L"添加媒体文件 / 网络地址 / M3U8 清单"}, {g_r.listImport, L"导入磁盘文件夹中的媒体"},
        {g_r.listView, L"↑↓ 选择 · 回车播放 · Delete 移除条目", !g_lists.Active().files.empty(), 5},
        {g_r.listReset, L"重置播放列表宽度为 320"},
        {g_r.listDivider, L"拖动调整宽度 · 双击 / Home 重置 · ←→ 调宽", true, 6}};
    if (!g_fullscreen) controls.push_back({g_r.topOpen, L"打开文件 / 网络地址 (O / Ctrl+U)"});
    if (!g_hasMedia) controls.push_back({g_r.openBtn, L"打开视频 / 音频文件 (O)"});
    if (showPlayback) {
        controls.insert(controls.end(), {
            {g_r.play, g_player.WantsToPlay() ? L"暂停 (空格)" : L"播放 (空格)", g_hasMedia},
            {g_r.prev, L"上一个视频 (PageUp)", CanStepPlaylist(-1)},
            {g_r.next, L"下一个视频 (PageDown)", CanStepPlaylist(1)},
            {g_r.back, L"后退 10 秒 (← 后退 5 秒)", g_player.CanSeek()},
            {g_r.fwd, L"前进 10 秒 (→ 前进 5 秒)", g_player.CanSeek()},
            {g_r.seekHit, g_player.Live()?L"直播源不支持进度定位":L"拖动定位 · 画面与声音预览", g_player.CanSeek(), 1},
            {g_r.volIcon, L"静音 / 恢复之前的音量 (M)"},
            {g_r.volTrack, L"音量 · 拖动 / 滚轮 / ↑↓", true, 2},
            {g_r.cc, L"显示 / 隐藏字幕 (S)"}, {g_r.gear, L"设置 (G)"},
            {g_r.list, L"播放列表 (P)"},
            {g_r.more, L"更多播放操作 · 字幕 / 音量 / 设置"},
            {g_r.fs, L"全屏 / 退出全屏 (F / F11)"}});
    }
    return controls;
}

static bool ControlVisible(const Control& control) {
    if (control.rect.right <= control.rect.left || control.rect.bottom <= control.rect.top) return false;
    if (!g_ui.settings || g_ui.help || (control.rect.left == g_r.sClose.left && control.rect.top == g_r.sClose.top)) return true;
    return control.rect.top >= g_r.settingsView.top && control.rect.bottom <= g_r.settingsView.bottom;
}

static void SetVolume(float volume, bool persist = false) {
    g_ui.volume = Clampf(volume, 0, 1);
    if (g_ui.volume > 0.001f) g_ui.restoreVolume = g_ui.volume;
    g_player.SetVolume(g_ui.volume);
    if (persist) SaveConfig();
    g_ui.dirty = true;
}

static void ToggleMute() {
    SetVolume(g_ui.volume > 0.001f ? 0 : g_ui.restoreVolume, true);
    Toast(g_ui.volume > 0 ? L"已恢复音量" : L"已静音");
}

static void ToggleSettings() {
    CancelGesture();
    g_ui.settings = !g_ui.settings;
    if (g_ui.settings) g_ui.playlist = false;
    g_ui.viewportDirty = true;
    g_ui.focus = -1;
    g_ui.keyboardFocus = false;
    g_ui.dirty = true;
}

static void TogglePlaylist() {
    CancelGesture();
    g_ui.playlist = !g_ui.playlist;
    if (g_ui.playlist) { g_ui.settings = false; g_ui.help = false; }
    g_ui.focus = -1; g_ui.keyboardFocus = false;
    g_ui.controlsVisible = true; g_ui.dirty = true;
    g_ui.viewportDirty = true;
    ComputeLayout(g_overlay.Width(), g_overlay.Height());
}

static void ChangePlaylistWidth(float width, bool persist) {
    const float maximum = std::max(240.0f, g_overlay.Width()/g_u - 320);
    g_cfg.playlistWidth = Clampf(width, 240, maximum);
    g_ui.viewportDirty = g_ui.dirty = true;
    ComputeLayout(g_overlay.Width(), g_overlay.Height());
    if (persist) SaveConfig();
}

static void ResetPlaylistWidth() {
    CancelGesture();
    g_cfg.playlistWidth = 320;
    g_ui.viewportDirty = g_ui.dirty = true;
    ComputeLayout(g_overlay.Width(), g_overlay.Height());
    SaveConfig();
}

static void PlaybackMenu() {
    CancelGesture();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_ui.subsOn ? MF_CHECKED : 0), 1, L"显示字幕 (S)");
    AppendMenuW(menu, MF_STRING | (g_ui.volume >= 1 ? MF_GRAYED : 0), 2, L"音量增加 10%");
    AppendMenuW(menu, MF_STRING | (g_ui.volume <= 0 ? MF_GRAYED : 0), 3, L"音量减少 10%");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"设置 (G)");
    AppendMenuW(menu, MF_STRING, 5, L"打开网络地址 (Ctrl+U)…");
    AppendMenuW(menu, MF_STRING, 6, L"音轨 / 字幕 / 音频 / 播放控制… (T)");
    POINT point = {(LONG)g_r.more.right, (LONG)g_r.more.top}; ClientToScreen(g_overlay.Hwnd(), &point);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_BOTTOMALIGN, point.x, point.y, 0, g_main, nullptr);
    DestroyMenu(menu);
    if (command == 1) { g_ui.subsOn = !g_ui.subsOn; SaveConfig(); }
    if (command == 2) SetVolume(g_ui.volume + .1f, true);
    if (command == 3) SetVolume(g_ui.volume - .1f, true);
    if (command == 4) ToggleSettings();
    if (command == 5) OpenNetworkDialog();
    if (command == 6) CoreSettingsMenu();
    g_ui.dirty = true;
}

// ---------------------------------------------------------------------------
// Media actions
// ---------------------------------------------------------------------------
static void UpdateTitle() {
    if (g_file.empty()) { SetWindowTextW(g_main, L"Lumen Player"); return; }
    std::wstring name = MediaName(g_file);
    SetWindowTextW(g_main, (name + L" — Lumen").c_str());
}

static void UpdateVideoWindows() {
    if (!g_main) return;
    RECT client; GetClientRect(g_main, &client);
    const int top = g_fullscreen ? 0 : (int)S(44);
    const int width = std::max(1, (int)PlaybackWidth((float)client.right));
    if (g_hwVideo) SetWindowPos(g_hwVideo, nullptr, 0, top, width, std::max(1L, client.bottom - top), SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_swVideo) SetWindowPos(g_swVideo, nullptr, 0, top, width, std::max(1L, client.bottom - top), SWP_NOZORDER | SWP_NOACTIVATE);
    bool hw = g_hasMedia && g_player.UsesSystemWindow();
    if (g_hwVideo) ShowWindow(g_hwVideo, hw ? SW_SHOW : SW_HIDE);
    if (g_swVideo) ShowWindow(g_swVideo, (g_hasMedia && !hw) ? SW_SHOW : SW_HIDE);
    if (g_overlay.Hwnd()) SetWindowPos(g_overlay.Hwnd(), HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (!hw) g_player.Repaint();
    g_ui.viewportDirty = false;
}

static void ResetTranslationView(){g_translation.Reset(++g_translationGeneration);g_lastAsrGeneration=UINT64_MAX;g_translationDisplay={};g_translationSource.clear();}
static void CollectTranslations(){
    std::vector<CaptionTranslation::Result> results;g_translation.TakeResults(results);std::vector<SubCue> exported;
    for(auto& result:results)if(result.generation==g_translationGeneration){
        exported.push_back({result.source.start,result.source.end,result.translation+L"\n"+result.source.text});
        const double now=g_player.Position(),expiry=result.source.end+5.0;
        if(now>=result.source.start && now<expiry){
            g_translationDisplay={result.source.start,expiry,std::move(result.translation)};
            g_translationSource=result.source.text;
        }
    }
    g_translated.Append(exported);
}
static void CollectCaptionFinals(){
    std::vector<SubCue> finals;uint64_t generation=0;g_captions.TakeFinals(finals,&generation);
    if(generation!=g_lastAsrGeneration){
        g_translation.Reset(++g_translationGeneration);g_lastAsrGeneration=generation;
        g_translationDisplay={};g_translationSource.clear();
    }
    for(const auto& cue:finals)g_translation.Submit(cue,g_translationGeneration);
    if(!finals.empty())g_ui.dirty=true;
    g_live.Append(finals);
}
static void ReleaseCaptionUses(){
    if(!g_activeCaptionModel.empty())g_modelStore.SetInUse(g_activeCaptionModel,false);
    if(!g_activeTranslationModel.empty())g_modelStore.SetInUse(g_activeTranslationModel,false);
    g_activeCaptionModel.clear();g_activeTranslationModel.clear();
}
static void ConfigureCaptionChoice(){
    if(g_captionStarting.load()||g_captionStopping||g_captions.Running())return;
    const auto* model=FindCaptionModel(g_cfg.captionSelection.recognition);if(!model)return;
    auto directory=g_modelStore.InstalledPath(*model);
    if(directory.empty())directory=PathJoin(g_modelStore.Root(),Utf8ToW(model->id));
    g_captions.Configure(model->id,directory);
}
static void FinishCaptionStop() {
    if(g_captionStop.joinable())g_captionStop.join();
    g_captionStopping=false;g_captionStopped.store(false);g_captionSavePending=false;
    g_captionStarting.store(false);g_captionResult.store(0);
    std::vector<SubCue> finals;g_captions.TakeFinals(finals);
    if(g_captionStopSession==g_captionSession)g_live.Append(finals);
    CollectTranslations();ReleaseCaptionUses();ConfigureCaptionChoice();
}

static void StopCaptions() {
    g_captionCancel.store(true);
    g_captions.RequestStop();g_translation.RequestStop();
    g_player.StopTap();
    if(g_captionStopping){FinishCaptionStop();return;}
    if (g_captionStart.joinable()) g_captionStart.join();
    g_captionStarting.store(false);
    g_captionResult.store(0);
    std::vector<SubCue> finals;
    g_captions.Stop();
    g_translation.Stop();
    g_captions.TakeFinals(finals);
    g_live.Append(finals);
    CollectTranslations();ReleaseCaptionUses();
}

static void StopCaptionsInBackground() {
    g_captionCancel.store(true);
    g_translation.RequestStop();
    if(g_captionStopping)return;
    g_player.StopTap();
    g_captions.RequestStop();
    g_captionStopSession=g_captionSession;g_captionStopped.store(false);g_captionStopping=true;
    try {
        g_captionStop=std::thread([] {
            if(g_captionStart.joinable())g_captionStart.join();
            g_captions.Stop();
            g_translation.Stop();
            g_captionStopped.store(true);
        });
    } catch(const std::exception& error) {
        LogMsg("Caption stop thread failed: %s",error.what());
        g_captionStopping=false;StopCaptions();
    }
}

static void ShowCaptionModels(){
    CaptionSelection selection=g_cfg.captionSelection;
    const BOOL overlayEnabled=IsWindowEnabled(g_overlay.Hwnd());EnableWindow(g_overlay.Hwnd(),FALSE);
    ShowCaptionModelDialog(g_main,g_modelStore,selection,[](const CaptionSelection& next){
        const bool changed=next.recognition!=g_cfg.captionSelection.recognition||next.translation!=g_cfg.captionSelection.translation||next.target!=g_cfg.captionSelection.target;
        g_cfg.captionSelection=next;SaveConfig();
        if(changed){ResetTranslationView();if(g_captions.Running()||g_captionStarting.load())StopCaptionsInBackground();else ConfigureCaptionChoice();}
    },[](const std::string& id){
        if(id==g_cfg.captionSelection.recognition)g_ui.captionsOn=false;
        if(id==g_cfg.captionSelection.translation)g_cfg.captionSelection.translation.clear();
        SaveConfig();
        if(id==g_activeCaptionModel||id==g_activeTranslationModel){if(!g_captionStopping)StopCaptionsInBackground();return false;}
        return true;
    });
    if(overlayEnabled)EnableWindow(g_overlay.Hwnd(),TRUE);
    ConfigureCaptionChoice();SaveConfig();g_ui.dirty=true;SetFocus(g_overlay.Hwnd());
}

static void DiscardUnsavedCaptions() {
    StopCaptions();
    LogMsg("Discarding unsaved caption session: %zu cues (RAM only)", g_live.Count());
    ++g_captionSession;g_captionSavePending=false;
    g_live.Clear(); g_captions.ClearResults();
    g_translated.Clear();ResetTranslationView();
}

static void StartCaptionTap() {
    g_player.StartTap();
    if (g_player.TapRunning()) Toast(L"实时字幕已就绪");
    else { StopCaptions(); g_ui.captionsOn = false; Toast(L"该媒体音轨无法用于实时字幕，继续播放"); SaveConfig(); }
    g_player.HoldPlayback(false);
}

static void StartCaptionsIfNeeded(bool waitForModel = true) {
    if (!g_ui.captionsOn || !g_hasMedia) return;
    if(g_captionStopping){if(waitForModel && g_player.HasAudio())g_player.HoldPlayback(true);return;}
    if (!g_player.HasAudio()) {
        if (!g_captionStarting.load()) StopCaptions();
        g_player.HoldPlayback(false);
        Toast(L"该视频没有音轨，直接播放");
        return;
    }
    if (waitForModel) g_player.HoldPlayback(true);
    if (g_captionStarting.load()) return;
    if (g_captions.Running()) { StartCaptionTap(); return; }
    ConfigureCaptionChoice();
    if (!g_captions.Available()) {
        if (!g_captions.Probe()) {
            Toast(L"请在设置的“模型与语言”中先安装识别模型");
            g_ui.captionsOn = false;
            g_player.HoldPlayback(false);
            SaveConfig();
            return;
        }
    }
    if (g_captionStart.joinable()) g_captionStart.join();
    const auto selection=g_cfg.captionSelection;
    const auto* asr=FindCaptionModel(selection.recognition);
    if(!asr)return;
    g_activeCaptionModel=selection.recognition;g_modelStore.SetInUse(g_activeCaptionModel,true);
    const auto* mt=FindCaptionModel(selection.translation);
    if(mt&&mt->SupportsTranslation(asr->language,selection.target)){g_activeTranslationModel=selection.translation;g_modelStore.SetInUse(g_activeTranslationModel,true);}
    ResetTranslationView();
    g_captionStarting.store(true);
    g_captionCancel.store(false);
    try {
        g_captionStart = std::thread([selection] {
            const auto* source=FindCaptionModel(selection.recognition);std::wstring directory,error;
            if(!source||!g_modelStore.VerifyInstalled(*source,directory,error,&g_captionCancel)){
                g_captionResult.store(g_captionCancel.load()?0:-1);return;
            }
            if(g_captionCancel.load())return;
            const bool ok=g_captions.Start();
            if(ok && !g_captionCancel.load()){const auto* translation=FindCaptionModel(selection.translation);
                if(translation&&source->language!=selection.target){
                    directory=g_modelStore.InstalledPath(*translation);
                    if(!directory.empty())g_translation.Start(*translation,directory,source->language,selection.target);
                }
            }
            if(!g_captionCancel.load())g_captionResult.store(ok?1:-1);
        });
    } catch (const std::exception& error) {
        LogMsg("Caption loader thread failed: %s", error.what());
        g_captionResult.store(-1);
    }
    Toast(waitForModel ? L"正在加载字幕模型，就绪后播放" : L"字幕模型后台加载中，播放状态由你控制");
}

static bool OpenFile(const std::wstring& path,std::unique_ptr<FfmpegReader> prepared = {}) {
    if (path.empty() || g_opening) return false;
    if (!MediaAvailable(path)) { Toast(L"文件不存在"); return false; }

    CancelGesture();
    g_queueFolder = UINT_MAX; g_queueEnded = false;
    g_opening = true;
    g_ui.dirty = true;
    g_ui.pendingClick = false;
    g_ui.pressInVideo = false;
    g_ui.ffActive = false;
    g_ui.seekDrag = g_ui.volDrag = g_ui.subDrag = g_ui.rateDrag = false;
    // A model loading for the previous file can serve the new file as soon as
    // it completes. Detach only the old tap; never wait for model load here.
    if (g_captionStopping || g_captionStarting.load() || (g_ui.captionsOn && g_captions.Running())) {
        g_player.StopTap();
        if(!g_captionStopping){g_captions.ResetTimeline(0);g_captions.ClearResults();}
    } else StopCaptions();
    g_player.Close();
    g_coreAwait=false;g_coreHadAudio=false;g_coreSidecar.clear();
    g_hasMedia = false;
    g_subs.Clear();
    ++g_captionSession;g_captionSavePending=false;g_live.Clear();g_translated.Clear();ResetTranslationView();

    if (!g_player.Open(path, g_cfg.hw != 0, false,std::move(prepared))) {
        Toast(g_player.AudioUnavailable()
            ? L"系统音频设备不可用，无法播放纯音频文件"
            : L"无法打开该媒体文件");
        g_file.clear();
        UpdateTitle();
        UpdateVideoWindows();
        g_opening = false;
        return false;
    }
    g_file = path;
    g_hasMedia = true;
    g_player.SubtitlePresentation(g_ui.subsOn && !g_ui.captionsOn,g_ui.subScale);
    g_player.SetVolume(g_ui.volume);
    g_player.SetMuted(false);
    g_ui.controlsVisible = true;
    g_ui.lastActivity = QpcNow();

    std::wstring side = IsNetworkMedia(path)?L"":SubtitleSidecar(path);
    if(g_player.CoreActive())g_coreSidecar=side;
    else if (!side.empty() && g_subs.Load(side)) {
        Toast(L"已加载字幕：" + side.substr(side.find_last_of(L"\\/") + 1));
    }
    UpdateTitle();
    UpdateVideoWindows();
    g_coreAwait=g_player.CoreActive();
    g_player.HoldPlayback(g_ui.captionsOn && (g_coreAwait || g_player.HasAudio()));
    g_player.Play();
    if (g_player.PlaybackHeld()) LogMsg("Caption playback waiting at %.3fs", g_player.Position());
    if(!g_coreAwait)StartCaptionsIfNeeded();

    if (g_player.AudioUnavailable()) {
        Toast(L"音轨或音频设备不可用，已静音播放");
    } else if (g_player.HasVideo() && g_player.HwAvailable() && !g_player.IsHardware()) {
        Toast(L"硬件解码不可用，已自动切换为软件解码");
    }
    SaveConfig();
    g_opening = false;
    return true;
}

static void RevealListSelection() {
    const auto& files = g_lists.Active().files;
    g_ui.listSelection = files.empty() ? 0 : std::max(0, std::min(g_ui.listSelection, (int)files.size() - 1));
    const float top = g_ui.listSelection * S(56);
    const float height = g_r.listView.bottom - g_r.listView.top;
    if (top < g_ui.listScroll) g_ui.listScroll = top;
    if (top + S(56) > g_ui.listScroll + height) g_ui.listScroll = top + S(56) - height;
    g_ui.dirty = true;
}

static bool PlayListItem(unsigned folderId, int index,bool supersede=true) {
    const auto* folder = g_lists.Find(folderId);
    if (!folder || index < 0 || index >= (int)folder->files.size()) return false;
    const std::wstring file = folder->files[index];
    if (!MediaAvailable(file)) { Toast(L"文件已移动或不存在，可从列表移除后重新添加"); return false; }
    if(supersede) CancelSources();
    if(IsNetworkMedia(file) || IsPlaylistFile(file)) {
        if(!g_hasMedia || !MediaEqual(file,g_file) || g_player.Failed() || g_player.IsEof()) {
            RequestSource(file,true,folderId,index,false); return true;
        }
    }
    if (!g_hasMedia || !MediaEqual(file,g_file)) {
        if (!OpenFile(file)) return false;
    } else g_player.Play();
    g_queueFolder = folderId;
    if (g_lists.selected == folderId) { g_ui.listSelection = index; RevealListSelection(); }
    return true;
}

static bool StepPlaylist(int direction) {
    const auto* folder = g_lists.Find(g_queueFolder);
    const int current = QueueIndex();
    if (!folder || current < 0) return false;
    for (int i = current + direction; i >= 0 && i < (int)folder->files.size(); i += direction) {
        if (MediaAvailable(folder->files[i])) return PlayListItem(folder->id, i);
    }
    Toast(direction > 0 ? L"已到列表末尾，没有可播放的下一项" : L"已到列表开头，没有可播放的上一项");
    return false;
}

static void AddMediaFiles(const std::vector<std::wstring>& files, bool playFirst) {
    if(playFirst) CancelSources();
    std::vector<std::wstring> ordinary;
    bool selectedFirst=false,ordinaryFirst=false;
    for(const auto& path:files) {
        if(IsNetworkMedia(path) || IsPlaylistFile(path)) {
            RequestSource(path,playFirst && !selectedFirst); selectedFirst=true;
        } else {ordinary.push_back(path);if(!selectedFirst){selectedFirst=true;ordinaryFirst=true;}}
    }
    if(ordinary.empty()) return;
    size_t before = g_lists.Active().files.size();
    const int first = g_lists.AddMany(g_lists.selected, ordinary);
    if (first < 0) { Toast(L"没有可添加的媒体文件，或列表已满"); return; }
    SavePlaylists();
    g_ui.listSelection = first; RevealListSelection();
    Toast(L"已添加 " + std::to_wstring(g_lists.Active().files.size() - before) + L" 个文件到“" + g_lists.Active().name + L"”");
    if (playFirst && ordinaryFirst) PlayListItem(g_lists.selected, first,false);
}

static void StartSourceJob(SourceRequest request) {
    g_sourceRequest=std::move(request); g_sourceCancel=std::make_shared<std::atomic<bool>>(false);
    g_sourceDone=false; g_sourceList={}; g_sourceReader.reset(); g_sourceError.clear();
    auto cancel=g_sourceCancel; const auto task=g_sourceRequest;
    const bool modern=g_player.ModernCoreEnabled() && Player::ModernCoreAvailable();
    try {g_sourceWorker=std::thread([task,cancel,modern]{
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try {
            if(task.importList && IsPlaylistFile(task.path)) {
                if(!ReadPlaylist(task.path,g_sourceList,cancel)) g_sourceError=L"无法读取 M3U/M3U8 清单，请检查格式、地址或网络";
            }
            if(g_sourceError.empty() && task.play && (g_sourceList.items.empty() || g_sourceList.hls) &&
               !modern) {
                auto reader=std::make_unique<FfmpegReader>();
                if(reader->Open(task.path,nullptr,false,cancel)) g_sourceReader=std::move(reader);
                else g_sourceError=L"媒体连接失败，请检查地址、网络或源的访问权限";
            }
        } catch(const std::exception&){g_sourceError=L"媒体准备失败，请重试";}
        if(SUCCEEDED(com)) CoUninitialize();
        g_sourceDone.store(true);
    });} catch(const std::exception&) {g_sourceCancel.reset();Toast(L"无法启动媒体连接任务，请重试");return;}
    Toast(L"正在读取媒体或清单… Esc 可取消，当前媒体可继续播放");
}
static void RequestSource(const std::wstring& path,bool play,unsigned folder,int index,bool importList) {
    const auto source=Playlists::Absolute(path);
    if(source.empty()){Toast(L"无效的媒体地址");return;}
    SourceRequest request{source,play,importList,folder,index};
    if(g_sourceWorker.joinable()) {
        if(g_sourceQueue.size()<128) g_sourceQueue.push_back(std::move(request));
        else Toast(L"待导入清单过多，请等待当前任务完成");
    } else StartSourceJob(std::move(request));
}
static void CancelSources(bool wait) {
    g_sourceQueue.clear();
    if(g_sourceCancel) g_sourceCancel->store(true);
    if(wait && g_sourceWorker.joinable()) {
        g_sourceWorker.join(); g_sourceReader.reset(); g_sourceList={}; g_sourceCancel.reset();
    }
}
static void PollSources() {
    if(!g_sourceWorker.joinable() || !g_sourceDone.load()) return;
    g_sourceWorker.join();
    const auto request=g_sourceRequest;
    const bool cancelled=g_sourceCancel->load();
    auto imported=std::move(g_sourceList); auto prepared=std::move(g_sourceReader);
    auto error=std::move(g_sourceError); g_sourceCancel.reset(); g_sourceList={};
    if(!cancelled) {
        if(!error.empty()) Toast(error);
        else {
            unsigned folder=request.folder; int index=request.index;
            if(!imported.items.empty()) {
                folder=g_lists.Import(imported); index=0;
                if(folder) {g_ui.listSelection=0;g_ui.listScroll=0;SavePlaylists();Toast(L"已导入清单到“"+g_lists.Active().name+L"”");}
                else {folder=UINT_MAX;Toast(L"列表文件夹或条目数量已满");}
            } else if(folder==UINT_MAX) {
                folder=g_lists.selected; index=g_lists.Add(folder,request.path); SavePlaylists();
            }
            if(request.play && folder!=UINT_MAX && g_lists.Find(folder)) {
                if(prepared || (g_player.ModernCoreEnabled() && Player::ModernCoreAvailable() && (imported.items.empty() || imported.hls))) {
                    if(OpenFile(request.path,std::move(prepared))) {
                        g_queueFolder=folder;
                        if(g_lists.selected==folder){g_ui.listSelection=std::max(0,index);RevealListSelection();}
                    }
                } else {
                    const auto* list=g_lists.Find(folder);
                    if(index>=0 && index<(int)list->files.size()) {
                        const auto& first=list->files[index];
                        if(IsNetworkMedia(first)||IsPlaylistFile(first)) g_sourceQueue.push_front({first,true,false,folder,index});
                        else PlayListItem(folder,index,false);
                    }
                }
            }
        }
    }
    if(!g_sourceQueue.empty()) {auto next=g_sourceQueue.front();g_sourceQueue.pop_front();StartSourceJob(std::move(next));}
    g_ui.dirty=true;
}

static std::vector<std::wstring> ChooseMedia(bool directory) {
    CancelGesture();
    Com<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.put())))) {
        Toast(L"无法打开文件选择窗口"); return {};
    }
    FILEOPENDIALOGOPTIONS flags{};
    dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR |
        (directory ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT));
    dialog->SetTitle(directory ? L"导入此文件夹中的视频和音频（不含子文件夹）" : L"添加媒体文件，可多选");
    if (!directory) {
        const COMDLG_FILTERSPEC types[] = {
            {L"视频、音频与播放清单", L"*.m3u8;*.m3u;*.mpd;*.mp4;*.mkv;*.mov;*.avi;*.webm;*.m4v;*.ts;*.mts;*.m2ts;*.mpg;*.mpeg;*.vob;*.flv;*.wmv;*.mxf;*.ogv;*.3gp;*.mp3;*.wav;*.flac;*.aac;*.m4a;*.ogg;*.opus;*.wma;*.ape"},
            {L"所有文件", L"*.*"}};
        dialog->SetFileTypes(2, types);
    }
    if (FAILED(dialog->Show(g_main))) return {};
    Com<IShellItemArray> items;
    if (FAILED(dialog->GetResults(items.put()))) return {};
    DWORD count = 0; items->GetCount(&count);
    std::vector<std::wstring> paths;
    for (DWORD i = 0; i < count && i < 10000; ++i) {
        Com<IShellItem> item; PWSTR path = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, item.put())) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            paths.emplace_back(path); CoTaskMemFree(path);
        }
    }
    if (directory && !paths.empty()) {
        auto files = MediaInFolder(paths.front());
        if (files.empty()) Toast(L"该文件夹中没有支持的视频或音频文件");
        return files;
    }
    return paths;
}

static void OpenFileDialog() {
    auto files = ChooseMedia(false);
    if (!files.empty()) AddMediaFiles(files, true);
}

struct ListNamePrompt { std::wstring value; bool accepted = false, network=false; std::wstring title,hint; };
static INT_PTR CALLBACK ListNameProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* prompt = (ListNamePrompt*)GetWindowLongPtrW(dialog, DWLP_USER);
    if (message == WM_INITDIALOG) {
        prompt = (ListNamePrompt*)lParam;
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        SetWindowTextW(dialog, !prompt->title.empty()?prompt->title.c_str():prompt->network?L"打开网络媒体或清单":L"列表文件夹名称");
        auto child = [&](DWORD style, const wchar_t* type, const wchar_t* text, int x, int y, int w, int h, int id) {
            RECT r = {x, y, x + w, y + h}; MapDialogRect(dialog, &r);
            HWND c = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, r.left, r.top, r.right - r.left, r.bottom - r.top,
                dialog, (HMENU)(INT_PTR)id, g_inst, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            return c;
        };
        child(0, L"STATIC", !prompt->hint.empty()?prompt->hint.c_str():prompt->network?L"输入 HTTP(S)、RTSP、RTMP 或其他流地址：":L"为列表文件夹命名（最多 80 字）：", 12, 10, 216, 12, 0);
        HWND edit = child(WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, L"EDIT", prompt->value.c_str(), 12, 28, 216, 16, 101);
        SendMessageW(edit, EM_SETLIMITTEXT, prompt->network?8192:80, 0);
        child(WS_TABSTOP | BS_DEFPUSHBUTTON, L"BUTTON", L"确定", 120, 56, 50, 18, IDOK);
        child(WS_TABSTOP, L"BUTTON", L"取消", 178, 56, 50, 18, IDCANCEL);
        SendMessageW(edit, EM_SETSEL, 0, -1); SetFocus(edit);
        return FALSE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK && prompt) {
        std::vector<wchar_t> value(prompt->network?8193:81); GetDlgItemTextW(dialog, 101, value.data(),(int)value.size());
        prompt->value = value.data();
        const auto first = prompt->value.find_first_not_of(L" \t");
        if (first == std::wstring::npos) { MessageBeep(MB_ICONINFORMATION); return TRUE; }
        prompt->value = prompt->value.substr(first, prompt->value.find_last_not_of(L" \t") - first + 1);
        prompt->accepted = true; EndDialog(dialog, IDOK); return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) { EndDialog(dialog, IDCANCEL); return TRUE; }
    return FALSE;
}

static bool AskListName(std::wstring& name,bool network=false,const std::wstring& title=L"",const std::wstring& hint=L"") {
    struct { DLGTEMPLATE dialog; WORD menu = 0, windowClass = 0, title = 0; } data{};
    data.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    data.dialog.cx = 240; data.dialog.cy = 86;
    ListNamePrompt prompt{name,false,network,title,hint};
    DialogBoxIndirectParamW(g_inst, &data.dialog, g_main, ListNameProc, (LPARAM)&prompt);
    if (prompt.accepted) name = prompt.value;
    return prompt.accepted;
}
static void OpenNetworkDialog(bool play) {
    CancelGesture();
    std::wstring address;
    if(AskListName(address,true)) {
        if(!IsNetworkMedia(address) || Playlists::Absolute(address).empty()){Toast(L"地址无效；支持 HTTP(S)、RTSP(S)、RTMP(S)、RTP、UDP、SRT");return;}
        CancelSources(); RequestSource(address,play);
    }
}
static void OpenMediaMenu(bool play) {
    CancelGesture();
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"打开媒体文件 / 导入 M3U/M3U8…");
    AppendMenuW(menu,MF_STRING,2,L"打开网络地址 (Ctrl+U)…");
    POINT point;GetCursorPos(&point);
    const auto command=TrackPopupMenu(menu,TPM_RETURNCMD,point.x,point.y,0,g_main,nullptr);DestroyMenu(menu);
    if(command==1) {auto files=ChooseMedia(false);if(!files.empty()) AddMediaFiles(files,play);}
    if(command==2) OpenNetworkDialog(play);
}

static void SelectListFolder(unsigned id) {
    if (!g_lists.Find(id)) return;
    g_lists.selected = id;
    g_ui.listScroll = 0; g_ui.listSelection = std::max(0, g_lists.IndexOf(id, g_file));
    RevealListSelection(); SavePlaylists();
    g_ui.focus = -1; g_ui.keyboardFocus = false;
}

static void ListFolderMenu() {
    CancelGesture();
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < g_lists.folders.size(); ++i) {
        std::wstring name = g_lists.folders[i].name;
        // Ampersands in user names are literal, not menu accelerators.
        for (size_t p = 0; (p = name.find(L'&', p)) != std::wstring::npos; p += 2) name.insert(p, 1, L'&');
        AppendMenuW(menu, MF_STRING | (g_lists.folders[i].id == g_lists.selected ? MF_CHECKED : 0), 100 + i, name.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 1, L"新建列表文件夹…");
    AppendMenuW(menu, MF_STRING | (!g_lists.selected ? MF_GRAYED : 0), 2, L"重命名此列表文件夹…");
    AppendMenuW(menu, MF_STRING | (!g_lists.selected ? MF_GRAYED : 0), 3, L"删除此列表文件夹…");
    POINT point = {(LONG)g_r.listFolder.left, (LONG)g_r.listFolder.bottom}; ClientToScreen(g_overlay.Hwnd(), &point);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, point.x, point.y, 0, g_main, nullptr);
    DestroyMenu(menu);
    if (command >= 100 && command - 100 < g_lists.folders.size()) SelectListFolder(g_lists.folders[command - 100].id);
    else if (command == 1 || command == 2) {
        std::wstring name = command == 1 ? L"新列表" : g_lists.Active().name;
        if (!AskListName(name)) return;
        if (command == 1) {
            unsigned id = g_lists.Create(name);
            if (id) SelectListFolder(id); else Toast(L"名称已存在、无效，或列表文件夹已满");
        } else if (g_lists.Rename(g_lists.selected, name)) SavePlaylists();
        else Toast(L"名称已存在或无效");
    } else if (command == 3) {
        if (MessageBoxW(g_main, L"删除此列表文件夹及其条目？\n磁盘上的视频、音频和字幕文件均会保留。", L"删除列表文件夹", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
            if (g_queueFolder == g_lists.selected) g_queueFolder = UINT_MAX;
            g_lists.Remove(g_lists.selected); SelectListFolder(0);
        }
    }
    g_ui.dirty = true;
}

static void RemoveListItem(int index) {
    auto& folder = g_lists.Active();
    if (index < 0 || index >= (int)folder.files.size()) return;
    if (g_queueFolder == folder.id && MediaEqual(folder.files[index],g_file)) g_queueFolder = UINT_MAX;
    folder.titles.erase(folder.files[index]);
    folder.files.erase(folder.files.begin() + index);
    RevealListSelection(); SavePlaylists(); g_ui.dirty = true;
}

static void ImportSubtitleDialog() {
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"字幕文件\0*.srt;*.vtt;*.ass;*.ssa;*.smi;*.sami;*.sub;*.idx;*.sup\0所有文件\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;
    if(g_player.AddSubtitle(file)) {
        g_ui.subsOn=true;g_subs.Clear();g_player.SubtitlePresentation(!g_ui.captionsOn,g_ui.subScale);Toast(L"已添加字幕轨，可在轨道菜单切换");SaveConfig();
    } else if (g_subs.Load(file)) {
        g_ui.subsOn = true;
        Toast(L"字幕已导入 · " + std::to_wstring(g_subs.Count()) + L" 条");
        SaveConfig();
    } else {
        Toast(L"无法解析该字幕文件");
    }
}

static void SaveCaptionsFile(bool translated) {
    if(g_captionStopping){g_captionSavePending=true;return;}
    CollectCaptionFinals();CollectTranslations();
    const auto all=translated?BilingualCaptions(g_live.cues,g_translated.cues):g_live.cues;
    if (all.empty()) { Toast(L"还没有生成任何字幕"); return; }

    std::wstring base = L"captions";
    if (!g_file.empty()) {
        size_t slash = g_file.find_last_of(L"\\/");
        size_t dot = g_file.find_last_of(L'.');
        base = (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
            ? g_file.substr(slash + 1, dot - slash - 1) : L"captions";
    }
    std::wstring suggested = base + (translated?L".bilingual.srt":L".srt");
    wchar_t file[MAX_PATH * 4] = {};
    wcsncpy(file, suggested.c_str(), MAX_PATH * 4 - 1);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"SRT 字幕\0*.srt\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.lpstrDefExt = L"srt";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;
    Toast(SaveSrt(file, all) ? L"字幕已保存" : L"保存失败");
}
static void SaveCaptionsDialog(){
    if(!g_translated.Count()){SaveCaptionsFile(false);return;}
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"保存识别原文 (SRT)");AppendMenuW(menu,MF_STRING,2,L"保存完整双语字幕 · 未翻译句保留原文 (SRT)");
    POINT point{};GetCursorPos(&point);const auto choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,g_main,nullptr);DestroyMenu(menu);
    if(choice)SaveCaptionsFile(choice==2);
}

static void ToggleFullscreen() {
    if (!g_fullscreen) {
        g_wpPrev.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(g_main, &g_wpPrev);
        MONITORINFO mi = { sizeof(MONITORINFO) };
        if (GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi)) {
            g_fullscreen = true;
            SetWindowLongPtrW(g_main, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(g_main, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        }
    } else {
        g_fullscreen = false;
        SetWindowLongPtrW(g_main, GWL_STYLE, WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE);
        SetWindowPlacement(g_main, &g_wpPrev);
        SetWindowPos(g_main, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    g_ui.controlsVisible = true;
    g_ui.lastActivity = QpcNow();
    g_ui.dirty = true;
}

static double FastForwardRate() {
    return g_cfg.holdRate;
}

static void EnterFF(bool keyboard = false) {
    if (!g_hasMedia) return;
    if(g_player.Live()){Toast(L"直播源不支持快进或倍速定位");return;}
    if (keyboard && !g_cfg.rightSpeed) return;
    if (g_player.PlaybackHeld()) { g_ui.pressInVideo = false; Toast(L"正在准备字幕，可调整进度或按空格暂停"); return; }
    if (!g_ui.ffActive) {
        g_ui.ffWasPlaying = g_player.WantsToPlay();
        g_ui.ffRestoreRate=g_player.Rate();
        g_ui.ffActive = true;
        g_ui.rightFF = keyboard;
        g_ui.ffScrub = !g_player.SetRateEx(FastForwardRate());
        g_ui.lastScrub = QpcNow();
        if (g_ui.ffScrub) g_player.Pause();
        else if (!g_ui.ffWasPlaying) g_player.Play();
    }
}

static void ExitFF() {
    if (!g_ui.ffActive) return;
    g_ui.ffActive = false;
    if (g_player.IsEof()) {
        g_ui.ffScrub = false;
        g_player.SetRate(g_ui.ffRestoreRate);
        g_player.Pause();
        return;
    }
    if (g_ui.ffScrub) {
        g_ui.ffScrub = false;
        g_player.SetRate(g_ui.ffRestoreRate);
        if (g_ui.ffWasPlaying) g_player.Play();
        return;
    }
    double pos = g_player.Position();
    g_player.SetRate(g_ui.ffRestoreRate);
    g_player.Seek(pos);
    if (g_ui.ffWasPlaying) g_player.Play(); else g_player.Pause();
}

// ---------------------------------------------------------------------------
// UI drawing
// ---------------------------------------------------------------------------
static void DrawTitleBar(ID2D1RenderTarget* rt, float W) {
    if (g_fullscreen) return;
    FillRect(rt, g_r.title, C(11, 12, 16, 0.94f));
    FillRect(rt, D2D1::RectF(0, g_r.title.bottom - 1, W, g_r.title.bottom), C(255, 255, 255, 0.05f));
    float cy = (g_r.title.top + g_r.title.bottom) / 2;
    FillRound(rt, RectF(S(18), cy - S(9), S(18), S(18)), S(6), kAccent);
    Text(rt, L"\uE768", RectF(S(18), cy - S(9), S(18), S(18)), S(10), C(255, 255, 255), 1, 400, true);
    Text(rt, L"LUMEN", RectF(S(44), g_r.title.top, S(70), g_r.title.bottom), S(12), kText, 0, 700);

    std::wstring name = L"未打开文件";
    if (!g_file.empty()) {
        size_t slash = g_file.find_last_of(L"\\/");
        name = slash == std::wstring::npos ? g_file : g_file.substr(slash + 1);
    }
    Text(rt, name, RectF(S(118), 0, std::max(0.0f, g_r.topOpen.left - S(132)), g_r.title.bottom), S(12.5f), C(158, 164, 180));
    Text(rt, L"\uE8E5", g_r.topOpen, S(15), kDim, 1, 400, true);

    POINT p; GetCursorPos(&p); ScreenToClient(g_overlay.Hwnd(), &p);
    auto drawBtn = [&](D2D1_RECT_F r, const wchar_t* glyph, bool danger) {
        bool hot = InRect(r, (float)p.x, (float)p.y);
        if (hot) FillRect(rt, r, danger ? C(196, 58, 62) : C(255, 255, 255, 0.07f));
        Text(rt, glyph, r, S(10), hot && danger ? C(255, 255, 255) : C(190, 195, 208), 1, 400, true);
    };
    drawBtn(g_r.tbMin, L"\uE921", false);
    drawBtn(g_r.tbMax, g_maximized ? L"\uE923" : L"\uE922", false);
    drawBtn(g_r.tbClose, L"\uE8BB", true);
}

static void DrawIconButton(ID2D1RenderTarget* rt, D2D1_RECT_F r, const wchar_t* glyph, bool active, float size = 15, bool enabled = true) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    if (enabled && (active || InRect(r, (float)g_ui.mouse.x, (float)g_ui.mouse.y)))
        FillRound(rt, r, S(8), active ? C(91, 140, 255, 0.18f) : C(255, 255, 255, 0.08f));
    Text(rt, glyph, r, S(size), !enabled ? C(92, 100, 118) : (active ? C(150, 180, 255) : C(210, 214, 224)), 1, 400, true);
}

static bool SubtitleIndicatorActive() {
    if(!g_ui.subsOn)return false;
    if(g_ui.captionsOn)return g_captions.Running()||g_live.Count()>0;
    if(g_player.CoreActive())return g_player.CoreInfo().subtitleTrack>0;
    return g_subs.Count()>0;
}

static void DrawBottomBar(ID2D1RenderTarget* rt, float W, float H, const std::vector<Control>& controls) {
    float top = g_r.bar.top;
    FillRect(rt, D2D1::RectF(0, top - S(34), W, H), C(0, 0, 0, 0.30f));
    FillRect(rt, D2D1::RectF(0, top, W, H), C(0, 0, 0, 0.52f));

    double dur = g_player.Duration();
    double pos = g_ui.seekDrag ? g_ui.seekPreview * dur : g_player.Position();
    float pct = !g_player.Live() && dur > 0.05 ? (float)(pos / dur) : 0.0f;
    pct = Clampf(pct, 0, 1);

    float sy = (g_r.seek.top + g_r.seek.bottom) / 2;
    float x0 = g_r.seek.left, x1 = g_r.seek.right;
    FillRound(rt, D2D1::RectF(x0, sy - S(2), x1, sy + S(2)), S(2), C(255, 255, 255, 0.20f));
    if (pct > 0) FillRound(rt, D2D1::RectF(x0, sy - S(2), x0 + (x1 - x0) * pct, sy + S(2)), S(2), kAccent);
    float knobX = x0 + (x1 - x0) * pct;
    bool seekFocus = false;
    if (g_ui.keyboardFocus && g_ui.focus >= 0 && g_ui.focus < (int)controls.size()) seekFocus = controls[g_ui.focus].slider == 1;
    bool showKnob = g_ui.hoverSeek || g_ui.seekDrag || seekFocus;
    if (seekFocus && g_player.CanSeek()) FillRound(rt, RectF(knobX - S(9), sy - S(9), S(18), S(18)), S(9), kAccent);
    if(!g_player.Live()) FillRound(rt, D2D1::RectF(knobX - S(6), sy - S(6), knobX + S(6), sy + S(6)), S(6),
        showKnob ? C(255, 255, 255) : C(225, 230, 242));

    bool playing = g_player.WantsToPlay();
    FillRound(rt, g_r.play, (g_r.play.right - g_r.play.left) / 2, g_hasMedia ? C(255, 255, 255, 0.96f) : C(95, 102, 118));
    Text(rt, playing ? L"\uE769" : L"\uE768", g_r.play, S(14), C(12, 14, 20), 1, 400, true);
    DrawIconButton(rt, g_r.prev, L"\uE892", false, 16, CanStepPlaylist(-1));
    DrawIconButton(rt, g_r.next, L"\uE893", false, 16, CanStepPlaylist(1));
    if (g_r.back.right > g_r.back.left) {
        DrawIconButton(rt, g_r.back, L"", false, 15, g_player.CanSeek());
        DrawIconButton(rt, g_r.fwd, L"", false, 15, g_player.CanSeek());
        Text(rt, L"−10", g_r.back, S(11), g_player.CanSeek() ? kText : kDim, 1, 500);
        Text(rt, L"+10", g_r.fwd, S(11), g_player.CanSeek() ? kText : kDim, 1, 500);
    }

    std::wstring tPos = FmtTime(pos);
    std::wstring tDur = L" / " + FmtTime(dur);
    Text(rt, g_player.Live()?L"直播 · "+tPos:tPos+tDur, g_r.time, S(12.5f), g_hasMedia ? kText : kDim, 0, 500);

    if (g_hasMedia && g_r.badge.right > g_r.badge.left) {
        bool hv = g_player.HasVideo();
        std::wstring badge = !hv ? L"音频" : g_player.UsesSystemWindow()?L"系统":(g_player.IsHardware() ? L"硬解" : L"软解");
        D2D1_COLOR_F bc = !hv ? C(120, 170, 255)
            : (g_player.IsHardware() ? C(90, 200, 150) : C(220, 170, 90));
        FillRound(rt, g_r.badge, S(11), C(255, 255, 255, 0.07f));
        Text(rt, badge, g_r.badge, S(10.5f), bc, 1, 500);
    }

    const wchar_t* vg = g_ui.volume <= 0.001f ? L"\uE74F" : (g_ui.volume < 0.5f ? L"\uE993" : L"\uE767");
    DrawIconButton(rt, g_r.volIcon, vg, g_ui.volume <= 0.001f, 14);
    if (g_r.volTrack.right > g_r.volTrack.left) {
    float vy = (g_r.volTrack.top + g_r.volTrack.bottom) / 2;
    FillRound(rt, D2D1::RectF(g_r.volTrack.left, vy - S(2), g_r.volTrack.right, vy + S(2)), S(2), C(255, 255, 255, 0.20f));
    float vw = (g_r.volTrack.right - g_r.volTrack.left) * Clampf(g_ui.volume, 0, 1);
    if (vw > 0) FillRound(rt, D2D1::RectF(g_r.volTrack.left, vy - S(2), g_r.volTrack.left + vw, vy + S(2)), S(2), C(228, 232, 242));
    if (g_ui.volDrag || InRect(g_r.volTrack, (float)g_ui.mouse.x, (float)g_ui.mouse.y))
        FillRound(rt, RectF(g_r.volTrack.left + vw - S(5), vy - S(5), S(10), S(10)), S(5), kText);
    }

    const bool ccActive = SubtitleIndicatorActive();
    if (g_r.cc.right > g_r.cc.left) {
        if (ccActive) FillRound(rt, g_r.cc, S(8), C(91, 140, 255, 0.2f));
        Text(rt, L"CC", g_r.cc, S(12), ccActive ? C(150, 180, 255) : C(170, 176, 190), 1, 700);
    }
    DrawIconButton(rt, g_r.gear, L"\uE713", g_ui.settings);
    DrawIconButton(rt, g_r.more, L"\uE712", false);
    DrawIconButton(rt, g_r.list, L"", g_ui.playlist, 15);
    const float lx = (g_r.list.left + g_r.list.right) / 2, ly = (g_r.list.top + g_r.list.bottom) / 2;
    for (int i = -1; i <= 1; ++i) {
        const auto color = g_ui.playlist ? C(150, 180, 255) : C(170, 176, 190);
        FillRound(rt, RectF(lx - S(8), ly + S(i*5) - S(1), S(2), S(2)), S(1), color);
        FillRound(rt, RectF(lx - S(3), ly + S(i*5) - S(1), S(11), S(2)), S(1), color);
    }
    DrawIconButton(rt, g_r.fs, g_fullscreen ? L"\uE73F" : L"\uE740", false, 14);
}

static std::wstring VisibleCaption() {
    if(!g_ui.subsOn)return {};
    const double pos=g_player.Position();
    if (g_ui.captionsOn && !g_captionStarting.load() && g_captions.Running()) {
        const auto partial=g_captions.Partial();
        if(!partial.empty())return partial;
        const auto* original=g_live.At(pos);
        if(g_lastAsrGeneration==g_captions.TimelineGeneration() &&
            (!original || *original==g_translationSource) &&
            pos>=g_translationDisplay.start && pos<g_translationDisplay.end &&
            !g_translationDisplay.text.empty())return g_translationDisplay.text;
        if(original)return *original;
    }
    const auto* sidecar=g_subs.At(pos);return sidecar?*sidecar:std::wstring();
}

static void DrawSubtitle(ID2D1RenderTarget* rt, float W, float H) {
    g_ui.subtitleTop = 0;
    const auto text=VisibleCaption();
    if (text.empty()) return;

    // Always keep the caption clear of the control strip. Gating this on
    // controlsVisible proved unreliable (the bar is also painted while paused and
    // in the settings view), and a caption drawn over the buttons is worse than
    // one that sits a little higher.
    if (g_ui.settings) W = g_r.panel.left;
    if (W < S(280)) return;
    float barTop = !g_ui.settings && !g_ui.help && (g_ui.controlsVisible || !g_player.IsPlaying()) ? g_r.bar.top : H;
    float maxW = W * 0.86f;
    float size = S(24) * g_ui.subScale;
    // LEADING alignment: metrics.width is then the real text width, which is what
    // the centring maths below needs. With a centred layout it reports the whole
    // layout box instead and the glyphs drift off to the right.
    auto* f = TF(size, 600, 0, false, true);
    if (!f) return;

    Com<IDWriteTextLayout> layout;
    if (FAILED(g_gfx.DWrite()->CreateTextLayout(text.c_str(), (UINT32)text.size(), f, maxW, size * 4, layout.put()))) return;
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    DWRITE_TEXT_METRICS tm = {};
    layout->GetMetrics(&tm);
    float tw = tm.width, th = std::min(tm.height, std::min(size * 4, barTop - g_r.video.top - S(48)));
    if (th <= 0) return;
    if (tw > maxW) tw = maxW;
    float cx = W / 2;
    float cy = barTop - S(24) - th / 2;
    D2D1_RECT_F box = D2D1::RectF(cx - tw / 2 - S(14), cy - th / 2 - S(8), cx + tw / 2 + S(14), cy + th / 2 + S(8));
    g_ui.subtitleTop = box.top;
    FillRound(rt, box, S(10), C(0, 0, 0, 0.62f));
    auto* b = Brush(rt, C(250, 250, 252));
    // The shared text format centres paragraphs, so the layout box (which is much
    // taller than one line) would push the glyphs down by tm.top. Compensate so
    // the text actually starts at cy - th/2.
    rt->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
    rt->DrawTextLayout(D2D1::Point2F(cx - tw / 2, cy - th / 2), layout.get(), b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    rt->PopAxisAlignedClip();
}

static void DrawFFPill(ID2D1RenderTarget* rt, float W) {
    if (!g_ui.ffActive) return;
    wchar_t label[48];
    swprintf(label, 48, L"%g× 快进中", FastForwardRate());
    float w = S(138), h = S(38);
    D2D1_RECT_F r = RectF(W / 2 - w / 2, g_r.video.top + S(26), w, h);
    FillRound(rt, r, h / 2, C(12, 14, 20, 0.86f));
    StrokeRound(rt, r, h / 2, C(255, 255, 255, 0.10f));
    Text(rt, L"\uE893", RectF(r.left + S(14), r.top, S(18), h), S(13), kAccent, 1, 400, true);
    Text(rt, label, RectF(r.left + S(34), r.top, w - S(48), h), S(13), C(235, 238, 246), 0, 600);
}

static void DrawSettingSlider(ID2D1RenderTarget* rt, D2D1_RECT_F row, float t, const wchar_t* value, bool enabled = true) {
    const float left = row.left + S(96), right = row.right - S(36), cy = (row.top + row.bottom) / 2;
    FillRound(rt, D2D1::RectF(left, cy - S(2), right, cy + S(2)), S(2), C(255, 255, 255, 0.16f));
    const float x = left + (right - left) * Clampf(t, 0, 1);
    FillRound(rt, D2D1::RectF(left, cy - S(2), x, cy + S(2)), S(2), enabled ? kAccent : kDim);
    FillRound(rt, D2D1::RectF(x - S(6), cy - S(6), x + S(6), cy + S(6)), S(6), enabled ? kText : kDim);
    Text(rt, value, D2D1::RectF(right + S(5), row.top, row.right, row.bottom), S(10.5f), kDim, 2);
}

static void DrawSettings(ID2D1RenderTarget* rt, float W, float H) {
    if (!g_ui.settings) return;
    D2D1_RECT_F p = g_r.panel;
    FillRect(rt, D2D1::RectF(p.left - S(30), p.top, p.left, p.bottom), C(0, 0, 0, 0.35f));
    FillRect(rt, p, C(16, 18, 24, 0.99f));
    FillRect(rt, D2D1::RectF(p.left, p.top, p.left + 1, p.bottom), C(255, 255, 255, 0.07f));

    float px = p.left + S(22);
    Text(rt, L"设置", g_r.sTitle, S(16), kText, 0, 700);
    Text(rt, L"\uE8BB", g_r.sClose, S(11), C(150, 156, 170), 1, 400, true);
    rt->PushAxisAlignedClip(g_r.settingsView, D2D1_ANTIALIAS_MODE_ALIASED);

    Text(rt, L"解码", g_r.sLabelDecode, S(12), C(120, 128, 145), 0, 600);
    Text(rt, L"硬件解码优先（立即切换）", g_r.sHw, S(13.5f), kText, 0, 500);
    Toggle(rt, g_r.sHw, g_hasMedia ? g_player.HardwarePreferred() : g_cfg.hw != 0);

    std::wstring status;
    D2D1_COLOR_F sc = kDim;
    if (g_hasMedia) {
        if (!g_player.HasVideo()) { status = L"当前：纯音频播放"; sc = C(120, 170, 255); }
        else if (g_player.IsHardware()) { status = g_player.UsesSystemWindow()?L"当前：系统播放管线":L"当前：D3D11VA 硬件解码"; sc = C(90, 200, 150); }
        else { status = g_player.IsCompatibility() ? L"当前：软件播放 · FFmpeg 兼容解码" : L"当前：软件播放 · 系统解码器"; sc = C(220, 170, 90); }
    } else {
        status = L"打开视频后显示解码状态";
    }
    if (g_hasMedia && g_player.AudioUnavailable()) status += L" · 音轨暂不可用";
    Text(rt, status, g_r.sStatusHw, S(11.5f), sc, 0, 400, false, true);
    FillRound(rt,g_r.sVideo,S(8),C(255,255,255,0.07f));
    Text(rt,L"画质与 GLSL 着色器…",g_r.sVideo,S(12.5f),kText,1,500);
    auto render=g_player.RenderInfo();
    std::wstring videoStatus=render.outputDepth?L"libplacebo · "+std::to_wstring(render.outputDepth)+L" bit · "+
        (render.hdrInput?(render.hdrOutput?L"HDR → HDR":L"HDR → SDR 色调映射"):(render.hdrOutput?L"SDR → HDR 容器":L"SDR 色彩管理")):
        L"自动 HDR / SDR · 高位深 GPU 渲染";
    if(render.shaderError)videoStatus+=L" · 着色器错误，已回退";
    else if(render.shader)videoStatus+=L" · GLSL 已启用";
    Text(rt,videoStatus,g_r.sVideoStatus,S(10.5f),kDim,0,400,false,true);
    FillRound(rt,g_r.sPlayback,S(8),C(255,255,255,0.07f));
    Text(rt,L"音轨 · 字幕轨 · 音频 · 播放控制…",g_r.sPlayback,S(12.0f),kText,1,500);

    Text(rt, L"字幕", g_r.sLabelSubs, S(12), C(120, 128, 145), 0, 600);
    FillRound(rt, g_r.sImport, S(8), C(255, 255, 255, 0.07f));
    StrokeRound(rt, g_r.sImport, S(8), C(255, 255, 255, 0.08f));
    Text(rt, L"导入字幕…", g_r.sImport, S(12.5f), kText, 1, 500);

    std::wstring subInfo = g_subs.Count()
        ? (g_subs.path.substr(g_subs.path.find_last_of(L"\\/") + 1) + L" · " + std::to_wstring(g_subs.Count()) + L" 条")
        : L"支持内嵌轨 / ASS / SMI 等";
    Text(rt, subInfo, RectF(g_r.sImport.right + S(12), g_r.sImport.top, std::max(0.0f, p.right - S(22) - g_r.sImport.right - S(12)), g_r.sImport.bottom - g_r.sImport.top),
        S(10.5f), kDim, 0, 400);

    Text(rt, L"显示字幕", g_r.sSubs, S(13.5f), kText, 0, 500);
    Toggle(rt, g_r.sSubs, g_ui.subsOn);

    Text(rt, L"字幕大小", g_r.sSubSize, S(13.5f), kText, 0, 500);
    wchar_t pct[32];
    swprintf(pct, 32, L"%d%%", (int)(g_ui.subScale * 100));
    DrawSettingSlider(rt, g_r.sSubSize, (g_ui.subScale - 0.6f) / 1.6f, pct);

    Text(rt, L"播放交互", g_r.sLabelSpeed, S(12), C(120, 128, 145), 0, 600);
    Text(rt, L"按住键盘 → 倍速", g_r.sRightSpeed, S(13.5f), kText, 0, 500);
    Toggle(rt, g_r.sRightSpeed, g_cfg.rightSpeed);
    Text(rt, L"长按倍率", g_r.sHoldRate, S(13.5f), kText, 0, 500);
    wchar_t speed[24]; swprintf(speed, 24, L"%g×", g_cfg.holdRate);
    DrawSettingSlider(rt, g_r.sHoldRate, (g_cfg.holdRate - 1.25f) / 6.75f, speed);

    Text(rt, L"实时字幕", g_r.sLabelCaptions, S(12), C(120, 128, 145), 0, 600);
    Text(rt, L"开启实时字幕", g_r.sCaptions, S(13.5f), kText, 0, 500);
    Toggle(rt, g_r.sCaptions, g_ui.captionsOn);
    FillRound(rt,g_r.sModels,S(8),C(91,140,255,.16f));
    StrokeRound(rt,g_r.sModels,S(8),C(91,140,255,.3f));
    Text(rt,L"模型与语言…",g_r.sModels,S(12.5f),C(170,195,255),1,500);

    std::wstring sstatus;
    D2D1_COLOR_F ssc = kDim;
    if (g_captionStopping) {
        sstatus=L"正在后台完成尾句 · 播放不受影响";
    } else if (g_captionStarting.load()) {
        sstatus = !g_ui.captionsOn ? L"实时字幕已关闭" : !g_player.PlaybackHeld() ? L"模型后台加载中 · 可随时播放" : (g_player.WantsToPlay() ? L"正在加载模型 · 就绪后播放" : L"正在加载模型 · 保持暂停");
        ssc = C(150, 180, 255);
    } else if (g_captions.Running()) {
        const auto* source=FindCaptionModel(g_cfg.captionSelection.recognition);
        sstatus=L"识别中 · "+CaptionLanguage(source?source->language:"zh");
        if(!g_cfg.captionSelection.translation.empty()&&source&&source->language!=g_cfg.captionSelection.target){auto status=g_translation.Status();sstatus+=L"\n"+(status.empty()?L"翻译未就绪 · 显示原文":status);}
        ssc = C(90, 200, 150);
    } else if (g_captions.Available()) {
        sstatus = g_ui.captionsOn && !g_hasMedia ? L"已开启 · 打开媒体后开始识别"
            : L"已就绪 · "+g_captions.ModelName();
    } else {
        sstatus = L"请先在“模型与语言”中安装识别模型";
        ssc = C(200, 140, 120);
    }
    Text(rt, sstatus, g_r.sStatusCaptions, S(11), ssc, 0, 400, false, true);

    bool canSave = g_live.Count() > 0;
    FillRound(rt, g_r.sCaptionsSave, S(8), canSave ? C(91, 140, 255, 0.22f) : C(255, 255, 255, 0.05f));
    StrokeRound(rt, g_r.sCaptionsSave, S(8), canSave ? C(91, 140, 255, 0.35f) : C(255, 255, 255, 0.06f));
    Text(rt, L"保存字幕 (SRT)", g_r.sCaptionsSave, S(12), canSave ? C(170, 195, 255) : C(120, 126, 140), 1, 500);
    Text(rt, L"已生成 " + std::to_wstring(g_live.Count()) + L" 条",
        RectF(g_r.sCaptionsSave.right + S(12), g_r.sCaptionsSave.top, S(110), S(32)), S(11), kDim, 0, 400);

    Text(rt, L"离线识别。开关不改变播放状态。未保存结果仅在内存中，关闭软件后清除。",
        g_r.sHint, S(10.5f), kDim, 0, 400, false, true);
    rt->PopAxisAlignedClip();
    if (g_ui.settingsScrollMax > 0) {
        float height = g_r.settingsView.bottom - g_r.settingsView.top;
        float thumb = height * height / (height + g_ui.settingsScrollMax);
        float y = g_r.settingsView.top + (height - thumb) * g_ui.settingsScroll / g_ui.settingsScrollMax;
        FillRound(rt, RectF(W - S(5), y, S(3), thumb), S(1.5f), C(120, 128, 145, 0.6f));
    }

    Text(rt, L"Lumen Player 2.2.5  ·  mpv / libplacebo / libass",
        RectF(px, H - S(34), p.right - p.left - S(44), S(20)), S(10), C(96, 102, 116));
}

static void DrawWelcome(ID2D1RenderTarget* rt, float W, float H) {
    if (g_hasMedia || g_ui.settings) return;
    const bool compact = g_r.openBtn.top - g_r.video.top < S(210);
    float cx = W / 2, cy = g_r.openBtn.top - S(86);
    const auto logo = compact ? RectF(cx-S(24),g_r.openBtn.top-S(116),S(48),S(48)) : RectF(cx-S(38),cy-S(78),S(76),S(76));
    FillRound(rt, logo, S(compact ? 16 : 24), C(91, 140, 255, 0.14f));
    Text(rt, L"\uE768", logo, S(compact ? 22 : 30), kAccent, 1, 400, true);
    Text(rt, L"LUMEN", RectF(cx - S(160), compact ? g_r.openBtn.top-S(62) : cy+S(8), S(320), S(40)), S(compact ? 22 : 26), kText, 1, 700);
    const float textW = std::min(S(440),W-S(36));
    if (!compact) Text(rt, L"轻量 · 现代 · 硬解优先的视频播放器", RectF(cx - textW/2, cy + S(48), textW, S(26)), S(13), kDim, 1, 400);

    FillRound(rt, g_r.openBtn, S(10), kAccent);
    Text(rt, L"打开视频 / 音频文件", g_r.openBtn, S(14), C(255, 255, 255), 1, 600);
    if (!compact) Text(rt, L"也可以把文件拖到这里，或按 O 打开",
        RectF(cx - textW/2, g_r.openBtn.bottom + S(18), textW, S(24)), S(11.5f), C(110, 116, 130), 1, 400);
}

static void DrawPlaylist(ID2D1RenderTarget* rt, const std::vector<Control>& controls) {
    if (!g_ui.playlist) return;
    const auto p = g_r.listPanel;
    FillRect(rt, p, C(18, 21, 29, 0.985f));
    const bool resizing = g_ui.listResizeDrag || (g_ui.keyboardFocus && g_ui.focus == 6) || InRect(g_r.listDivider, (float)g_ui.mouse.x, (float)g_ui.mouse.y);
    FillRect(rt, RectF(p.left, p.top, S(resizing ? 2 : 1), p.bottom - p.top), resizing ? kAccent : C(255, 255, 255, 0.08f));
    Text(rt, L"播放列表", RectF(p.left + S(22), p.top + S(12), p.right-p.left-S(128), S(30)), S(17), kText, 0, 600);
    DrawIconButton(rt, g_r.listClose, L"\uE8BB", false, 12);
    FillRound(rt, g_r.listReset, S(6), C(255,255,255,0.05f));
    Text(rt, L"重置", g_r.listReset, S(11), kDim, 1, 500);
    FillRound(rt, g_r.listFolder, S(7), C(255, 255, 255, 0.065f));
    Text(rt, g_lists.Active().name, RectF(g_r.listFolder.left + S(10), g_r.listFolder.top, g_r.listFolder.right - g_r.listFolder.left - S(40), S(32)), S(13), kText, 0, 500);
    Text(rt, L"\uE70D", RectF(g_r.listFolder.right - S(30), g_r.listFolder.top, S(22), S(32)), S(10), kDim, 1, 400, true);
    for (const auto r : {g_r.listAdd, g_r.listImport}) FillRound(rt, r, S(7), C(91, 140, 255, 0.13f));
    Text(rt, L"＋ 添加媒体", g_r.listAdd, S(12), C(175, 198, 255), 1, 500);
    Text(rt, L"导入文件夹…", g_r.listImport, S(12), C(175, 198, 255), 1, 500);
    const auto& folder = g_lists.Active();
    const auto& files = folder.files;
    const bool rowsFocused = g_ui.keyboardFocus && g_ui.focus >= 0 && g_ui.focus < (int)controls.size() && controls[g_ui.focus].slider == 5;
    rt->PushAxisAlignedClip(g_r.listView, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const int first = std::max(0, (int)(g_ui.listScroll / S(56)));
    const int end = std::min((int)files.size(), first + (int)((g_r.listView.bottom - g_r.listView.top) / S(56)) + 2);
    for (int i = first; i < end; ++i) {
        auto row = RectF(g_r.listView.left, g_r.listView.top + i * S(56) - g_ui.listScroll, g_r.listView.right - g_r.listView.left - S(4), S(52));
        const bool playing = g_queueFolder == folder.id && MediaEqual(files[i],g_file);
        const bool selected = i == g_ui.listSelection;
        const bool hot = InRect(row, (float)g_ui.mouse.x, (float)g_ui.mouse.y);
        if (playing || selected || hot) FillRound(rt, row, S(7), playing ? C(91, 140, 255, 0.17f) : C(255, 255, 255, selected ? 0.07f : 0.04f));
        if (selected && rowsFocused) StrokeRound(rt, row, S(7), C(91, 140, 255, 0.6f));
        Text(rt, playing ? L"\uE768" : std::to_wstring(i + 1), RectF(row.left + S(3), row.top, S(28), S(52)), S(11), playing ? kAccent : kDim, 1, 500, playing);
        const std::wstring name = g_lists.ItemName(folder.id,files[i]);
        Text(rt, name, RectF(row.left + S(35), row.top + S(4), row.right - row.left - S(70), S(24)), S(12.5f), playing ? C(175, 198, 255) : kText, 0, 500);
        Text(rt, MediaLocation(files[i]), RectF(row.left + S(35), row.top + S(29), row.right - row.left - S(70), S(18)), S(10), kDim);
        Text(rt, L"\uE8BB", RectF(row.right - S(30), row.top + S(10), S(28), S(32)), S(10), C(135, 143, 163), 1, 400, true);
    }
    if (files.empty()) Text(rt, L"添加文件，或导入磁盘文件夹\n点击条目播放，⏮ / ⏭ 切换视频", g_r.listView, S(12), kDim, 1, 400, false, true);
    rt->PopAxisAlignedClip();
    if (g_ui.listScrollMax > 0) {
        const float height = g_r.listView.bottom - g_r.listView.top;
        const float thumb = std::max(S(20), height * height / (height + g_ui.listScrollMax));
        const float y = g_r.listView.top + (height - thumb) * g_ui.listScroll / g_ui.listScrollMax;
        FillRound(rt, RectF(p.right - S(5), y, S(3), thumb), S(1.5f), C(120, 128, 145, 0.65f));
    }
    Text(rt, std::to_wstring(files.size()) + L" 个文件 · 顺序连播 · P 收起", RectF(p.left + S(22), p.bottom - S(27), p.right - p.left - S(44), S(24)), S(10.5f), kDim);
}

static void DrawToast(ID2D1RenderTarget* rt, float W, float H) {
    const bool waiting = g_captionStarting.load() && g_ui.captionsOn && g_player.PlaybackHeld();
    const bool connecting=g_sourceWorker.joinable() && g_sourceCancel && !g_sourceCancel->load() && !g_sourceDone.load();
    const std::wstring text = connecting?L"正在连接媒体或读取清单… Esc 取消":waiting ? (g_player.WantsToPlay() ? L"等待字幕模型加载，完成后自动播放" : L"等待字幕模型加载，完成后保持暂停") : g_ui.toast;
    if (text.empty()) return;
    double age = waiting || connecting ? 0 : QpcNow() - g_ui.toastTime;
    if (age > 2.4) { g_ui.toast.clear(); return; }
    if (g_ui.settings && g_r.panel.left > S(140)) W = g_r.panel.left;
    float alpha = age > 1.9 ? (float)((2.4 - age) / 0.5) : 1.0f;
    auto* f = TF(S(12.5f), 500, 0, false, true);
    if (!f) return;
    Com<IDWriteTextLayout> layout;
    if (FAILED(g_gfx.DWrite()->CreateTextLayout(text.c_str(), (UINT32)text.size(), f, W * 0.85f - S(36), S(76), layout.put()))) return;
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    DWRITE_TEXT_METRICS tm = {};
    layout->GetMetrics(&tm);
    float bw = tm.width + S(36), bh = tm.height + S(20);
    const float bottom = g_ui.subtitleTop > 0 ? std::min(g_r.bar.top - S(16), g_ui.subtitleTop - S(10)) : g_r.bar.top - S(16);
    D2D1_RECT_F r = RectF(W / 2 - bw / 2, std::max(g_r.video.top + S(12), bottom - bh), bw, bh);
    FillRound(rt, r, bh / 2, C(20, 22, 30, 0.94f * alpha));
    StrokeRound(rt, r, bh / 2, C(255, 255, 255, 0.09f * alpha));
    auto* b = Brush(rt, C(235, 238, 246, alpha));
    rt->DrawTextLayout(D2D1::Point2F(W / 2 - tm.width / 2, r.top + (bh - tm.height) / 2), layout.get(), b, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

static void DrawInteraction(ID2D1RenderTarget* rt, float W, float H, const std::vector<Control>& controls) {
    for (size_t i = 0; i < controls.size(); ++i) {
        const auto& control = controls[i];
        if (!ControlVisible(control)) continue;
        const bool focused = g_ui.keyboardFocus && g_ui.focus == (int)i;
        const bool hot = InRect(control.rect, (float)g_ui.mouse.x, (float)g_ui.mouse.y);
        if (control.slider != 1 && control.slider != 5 && control.slider != 6 && (focused || (hot && control.enabled)))
            StrokeRound(rt, control.rect, S(7), focused ? kAccent : C(185, 201, 240, 0.4f), focused ? S(2) : S(1));
        if (!hot || control.slider == 5 || g_ui.help || QpcNow() - g_ui.hoverTime < 0.55 || g_ui.seekDrag || g_ui.volDrag || g_ui.subDrag || g_ui.rateDrag) continue;
        std::wstring label = control.label;
        if (control.slider == 1 && g_hasMedia) {
            float t = Clampf((g_ui.mouse.x - g_r.seek.left) / std::max(1.0f, g_r.seek.right - g_r.seek.left), 0, 1);
            label = FmtTime(t * g_player.Duration());
        } else if (control.slider == 2) label = L"音量 " + std::to_wstring((int)(g_ui.volume * 100)) + L"%";
        float width = std::min(W - S(24), std::max(S(76), S(13) * (float)label.size() + S(20)));
        float x = Clampf((control.rect.left + control.rect.right - width) / 2, S(12), W - width - S(12));
        float y = Clampf(control.rect.top - S(38), g_r.video.top + S(8), H - S(44));
        auto box = RectF(x, y, width, S(30));
        FillRound(rt, box, S(7), C(33, 37, 49, 0.98f));
        StrokeRound(rt, box, S(7), C(255, 255, 255, 0.12f));
        Text(rt, label, RectF(x + S(8), y, width - S(16), S(30)), S(11.5f), kText, 1);
        break;
    }
}

static void DrawHelp(ID2D1RenderTarget* rt, float W, float H) {
    if (!g_ui.help) return;
    FillRect(rt, RectF(0, g_r.video.top, W, H - g_r.video.top), C(0, 0, 0, 0.68f));
    float width = std::min(S(520), W - S(32));
    float top = g_r.helpClose.top - S(8);
    float height = std::min(S(430), H - top - S(12));
    auto box = RectF((W - width) / 2, top, width, height);
    FillRound(rt, box, S(14), C(22, 25, 34));
    StrokeRound(rt, box, S(14), C(255, 255, 255, 0.12f));
    Text(rt, L"快捷键与操作", RectF(box.left + S(24), top + S(8), width - S(80), S(36)), S(17), kText, 0, 600);
    Text(rt, L"\uE8BB", g_r.helpClose, S(12), kDim, 1, 400, true);
    const wchar_t* keys[] = {L"空格 / 单击画面", L"← / 点按 →", L"↑ / ↓ / 滚轮", L"M", L"F / F11 / 双击画面", L"O / Ctrl+O / Ctrl+U", L"S / C", L"G / F1", L"P / PageUp / PageDown", L"Tab / Shift+Tab", L"长按左键 / 键盘 →", L"T / 鼠标右键", L", / .", L"[ / ] / \\", L"Esc"};
    const wchar_t* actions[] = {L"播放 / 暂停", L"后退 / 前进 5 秒", L"调节音量", L"静音 / 恢复原音量", L"切换全屏", L"打开文件 / 网络地址", L"显示字幕 / 实时字幕", L"设置 / 快捷键帮助", L"列表 / 上一项 / 下一项", L"切换控件，回车操作", L"使用设置倍率，松开恢复", L"音轨、音频与播放菜单", L"上一帧 / 下一帧（暂停）", L"设 A / 设 B / 清除循环", L"关闭浮层 / 退出全屏"};
    float row = std::min(S(29), (height - S(60)) / 15);
    for (int i = 0; i < 15; ++i) {
        float y = top + S(46) + i * row;
        Text(rt, keys[i], RectF(box.left + S(24), y, width * 0.43f, row), S(11.5f), kText);
        Text(rt, actions[i], RectF(box.left + width * 0.48f, y, width * 0.47f, row), S(11.5f), kDim);
    }
}

static void DrawAudioCard(ID2D1RenderTarget* rt, float W, float H) {
    if (!g_hasMedia || g_player.HasVideo() || g_ui.settings) return;
    float cx = W / 2;
    float cy = (g_r.video.top + g_r.video.bottom) / 2 - S(30);
    float d = S(104);
    FillRound(rt, RectF(cx - d / 2, cy - d / 2, d, d), d / 2, C(91, 140, 255, 0.12f));
    for (int i = 0; i < 9; ++i) {
        float bh = S(12) + (i % 3 == 1 ? S(26) : (i % 3 == 0 ? S(16) : S(21)));
        float bx = cx - S(52) + i * S(13);
        FillRound(rt, RectF(bx, cy - bh / 2, S(6), bh), S(3), C(150, 180, 255, 0.9f));
    }

    std::wstring name = L"音频";
    if (!g_file.empty()) {
        size_t slash = g_file.find_last_of(L"\\/");
        name = slash == std::wstring::npos ? g_file : g_file.substr(slash + 1);
    }
    Text(rt, name, RectF(cx - W * 0.4f, cy + d / 2 + S(16), W * 0.8f, S(28)), S(15), kText, 1, 600);
    Text(rt, L"纯音频文件 · 可开启实时字幕生成文本",
        RectF(cx - W * 0.4f, cy + d / 2 + S(44), W * 0.8f, S(24)), S(12), kDim, 1, 400);
}

static void PaintOverlay() {
    if (!g_overlay.Hwnd() || !g_gfx.RT()) return;
    int w = g_overlay.Width(), h = g_overlay.Height();
    if (w <= 0 || h <= 0) return;

    if (g_main && IsIconic(g_main)) return;
    ComputeLayout(w, h);
    if (g_ui.viewportDirty) UpdateVideoWindows();
    g_textKeep.clear();
    RECT rc = { 0, 0, w, h };
    if (!g_gfx.Bind(g_overlay.Dc(), rc)) return;
    ID2D1RenderTarget* rt = g_gfx.RT();
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 1.0f / 255.0f));

    const float mediaW = g_r.video.right;
    const auto controls = Controls();
    DrawAudioCard(rt, mediaW, (float)h);
    if (!g_ui.settings && !g_ui.help && (g_ui.controlsVisible || !g_player.IsPlaying()))
        DrawBottomBar(rt, mediaW, (float)h, controls);
    DrawWelcome(rt, mediaW, (float)h);
    DrawSubtitle(rt, mediaW, (float)h);
    DrawFFPill(rt, mediaW);
    DrawTitleBar(rt, (float)w);
    DrawSettings(rt, (float)w, (float)h);
    DrawPlaylist(rt, controls);
    DrawToast(rt, mediaW, (float)h);
    DrawHelp(rt, (float)w, (float)h);
    DrawInteraction(rt, (float)w, (float)h, controls);

    HRESULT result = rt->EndDraw();
    if (SUCCEEDED(result)) { g_overlay.Present(); g_ui.dirty = false; }
    else if (result == D2DERR_RECREATE_TARGET) {
        g_brush.reset();
        g_gfx.RecreateTarget();
        g_ui.dirty = true;
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static void ActionSeekToX(float x) {
    if (!g_hasMedia || g_player.Duration() <= 0) return;
    ComputeLayout(g_overlay.Width(), g_overlay.Height());
    float t = (x - g_r.seek.left) / std::max(1.0f, g_r.seek.right - g_r.seek.left);
    g_ui.seekPreview = Clampf(t, 0, 1);
    g_ui.dirty = true;
}

static void UpdateSeekPreview() {
    if (!g_ui.seekDrag || !g_hasMedia) return;
    const double now = QpcNow(), target = g_ui.seekPreview * g_player.Duration();
    const bool changed = g_ui.seekSubmitted < 0 || std::fabs(target - g_ui.seekSubmitted) > 0.035;
    // Coalesce mouse events. A slow decoder gets time to finish a frame; a
    // newer target replaces the one pending on the existing decode thread.
    if (changed && now - g_ui.seekRequestTime >= 0.16 &&
        (g_ui.seekSubmitted < 0 || g_player.SeekReady() || now - g_ui.seekRequestTime > 0.8)) {
        g_player.Pause();
        g_player.PreviewSeek(target);
        g_ui.seekSubmitted = target;
        g_ui.seekRequestTime = now;
        if (!g_player.PlaybackHeld()) g_player.Play();
    }
    // Give each loaded picture a short, normally volumed audio preview. The
    // software audio clock is held until its requested picture is decoded.
    if (g_ui.seekSubmitted >= 0 && g_player.SeekReady() &&
        g_player.Position() >= std::min(g_ui.seekSubmitted, std::max(0.0, g_player.Duration() - 0.05)) + 0.16) g_player.Pause();
}

static void EndSeekPreview() {
    if (!g_ui.seekDrag) return;
    g_ui.seekDrag = false;
    g_player.Pause();
    g_player.PreviewSeek(g_ui.seekPreview * g_player.Duration());
    g_player.SetScrubbing(false);
    if (g_ui.seekWasPlaying) g_player.Play();
}

static void ActionSubSize(float x) {
    float left = g_r.sSubSize.left + S(96), right = g_r.sSubSize.right - S(36);
    float t = Clampf((x - left) / std::max(1.0f, right - left), 0, 1);
    g_ui.subScale = std::round((0.6f + t * 1.6f) * 100) / 100;
    g_ui.dirty = true;
}

static void ActionHoldRate(float x) {
    const float left = g_r.sHoldRate.left + S(96), right = g_r.sHoldRate.right - S(36);
    const float t = Clampf((x - left) / std::max(1.0f, right - left), 0, 1);
    g_cfg.holdRate = std::round((1.25f + t * 6.75f) * 4) / 4;
    g_ui.dirty = true;
}

static void ActionToggleCaptions(bool fromSettings = false) {
    if(!g_ui.captionsOn){const auto* model=FindCaptionModel(g_cfg.captionSelection.recognition);
        if(!model||g_modelStore.InstalledPath(*model).empty()){Toast(L"请先安装字幕识别模型");ShowCaptionModels();return;}}
    g_ui.captionsOn = !g_ui.captionsOn;
    if (g_ui.captionsOn) {
        StartCaptionsIfNeeded(!fromSettings);
        if (!g_hasMedia) Toast(L"已开启实时字幕，打开媒体后开始识别");
    } else {
        StopCaptionsInBackground();
        g_player.HoldPlayback(false);
        Toast(L"实时字幕已关闭");
    }
    SaveConfig();
}

static void ActionSwitchDecoder() {
    CancelGesture();
    const bool hardware = g_hasMedia ? !g_player.HardwarePreferred() : !g_cfg.hw;
    if (g_hasMedia && !g_player.SwitchDecoder(hardware)) {
        Toast(L"此文件暂不支持该播放管线，已保留当前解码与播放状态"); return;
    }
    g_cfg.hw = hardware;
    UpdateVideoWindows(); g_player.Repaint(); SaveConfig();
    Toast(hardware ? L"已切换为硬解优先，状态显示实际解码方式" : L"已切换到软件解码");
}

#include "core_menu.inc"

static void VideoSettingsMenu() {
    if(g_player.ModernCoreEnabled()) {CoreSettingsMenu(true);return;}
    HMENU menu=CreatePopupMenu();
    auto info=g_player.RenderInfo();
    std::wstring output=info.outputDepth?(std::to_wstring(info.inputDepth)+L" bit 输入 → "+std::to_wstring(info.outputDepth)+L" bit 输出"):
        L"libplacebo · Lanczos / Hermite · 自动色彩管理";
    AppendMenuW(menu,MF_STRING|MF_DISABLED,0,output.c_str());
    if(info.adapter[0])AppendMenuW(menu,MF_STRING|MF_DISABLED,0,Utf8ToW(info.adapter).c_str());
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(!g_cfg.forceSdr?MF_CHECKED:0),1,L"自动匹配当前 HDR / SDR 显示输出");
    AppendMenuW(menu,MF_STRING|(g_cfg.forceSdr?MF_CHECKED:0),2,L"固定 SDR 输出（HDR 视频做色调映射）");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    std::wstring shader=g_cfg.shaderPath.empty()?L"未加载外部着色器":L"着色器："+g_cfg.shaderPath.substr(g_cfg.shaderPath.find_last_of(L"\\/")+1);
    AppendMenuW(menu,MF_STRING|MF_DISABLED,0,shader.c_str());
    AppendMenuW(menu,MF_STRING,3,L"加载 GLSL / hook 着色器…");
    AppendMenuW(menu,MF_STRING|(g_cfg.shaderPath.empty()?MF_GRAYED:0),4,L"移除着色器 · 恢复默认画质");
    POINT point{(LONG)g_r.sVideo.left,(LONG)g_r.sVideo.bottom};ClientToScreen(g_overlay.Hwnd(),&point);
    const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,point.x,point.y,0,g_main,nullptr);DestroyMenu(menu);
    if(!command)return;
    bool sdr=g_cfg.forceSdr;std::wstring path=g_cfg.shaderPath;
    if(command==1 || command==2)sdr=command==2;
    if(command==4)path.clear();
    if(command==3) {
        wchar_t file[32768]{};OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner=g_main;dialog.lpstrFilter=L"mpv GLSL 着色器 (*.glsl;*.hook)\0*.glsl;*.hook\0所有文件\0*.*\0\0";
        dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrTitle=L"加载 GLSL 着色器（最大 4 MiB）";
        dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
        if(!GetOpenFileNameW(&dialog))return;path=file;
    }
    if(!g_player.ConfigureVideo(path,sdr)) {Toast(L"着色器无法加载或格式无效，已保留原有设置");return;}
    g_cfg.shaderPath=path;g_cfg.forceSdr=sdr;SaveConfig();g_player.Repaint();
    Toast(path.empty()?L"已应用默认画质与显示输出设置":L"已加载 GLSL 着色器，编译失败时自动恢复默认画质");
}

static bool VideoClickTarget(float x,float y) {
    return g_hasMedia && !g_ui.settings && !g_ui.help && InRect(g_r.video,x,y) && y<g_r.bar.top;
}
static double VideoClickDelay() {
    return std::min(.2,GetDoubleClickTime()/1000.0);
}
static void CommitVideoClick() {
    if(!g_ui.pendingClick)return;
    g_ui.pendingClick=false;
    // Preserve the chosen action if EOF arrives during double-click arbitration.
    if(g_hasMedia){if(g_ui.clickWasPlaying)g_player.Pause();else g_player.Play();}
}
static void HandleClick(float x, float y) {
    if (g_ui.rightKeyDown) CancelGesture();
    if(g_ui.pendingClick) {
        // Another ordinary video down is a distinct click, not WM_LBUTTONDBLCLK.
        if(VideoClickTarget(x,y))CommitVideoClick();else g_ui.pendingClick=false;
    }
    g_ui.mouse = {(LONG)x,(LONG)y};
    bool controlsWereVisible = g_ui.controlsVisible || !g_player.IsPlaying();
    g_ui.dirty = true;
    g_ui.keyboardFocus = false;
    g_ui.lastActivity = QpcNow();
    g_ui.controlsVisible = true;
    if (g_ui.help) {
        if (InRect(g_r.helpClose, x, y)) g_ui.help = false;
        return;
    }
    if (g_ui.settings && InRect(g_r.panel, x, y)) {
        if (InRect(g_r.sClose, x, y)) { g_ui.settings = false; return; }
        if (!InRect(g_r.settingsView, x, y)) return;
        if (InRect(g_r.sHw, x, y)) {
            ActionSwitchDecoder();
            return;
        }
        if (InRect(g_r.sRightSpeed, x, y)) { g_cfg.rightSpeed = !g_cfg.rightSpeed; SaveConfig(); return; }
        if(InRect(g_r.sVideo,x,y)) {VideoSettingsMenu();return;}
        if(InRect(g_r.sPlayback,x,y)) {CoreSettingsMenu();return;}
        if (InRect(g_r.sHoldRate, x, y)) {
            if (x < g_r.sHoldRate.left + S(88)) return;
            g_ui.rateDrag = true; SetCapture(g_overlay.Hwnd()); ActionHoldRate(x); return;
        }
        if (InRect(g_r.sImport, x, y)) { ImportSubtitleDialog(); return; }
        if (InRect(g_r.sSubs, x, y)) { g_ui.subsOn = !g_ui.subsOn; SaveConfig(); return; }
        if (InRect(g_r.sSubSize, x, y)) {
            if (x < g_r.sSubSize.left + S(88)) return;
            g_ui.subDrag = true;
            SetCapture(g_overlay.Hwnd());
            ActionSubSize(x);
            return;
        }
        if (InRect(g_r.sCaptions, x, y)) { ActionToggleCaptions(true); return; }
        if(InRect(g_r.sModels,x,y)){ShowCaptionModels();return;}
        if (InRect(g_r.sCaptionsSave, x, y)) { if (g_live.Count()) SaveCaptionsDialog(); return; }
        return;
    }

    if (g_ui.playlist && InRect(g_r.listDivider, x, y)) {
        CancelGesture();
        g_ui.pendingClick = false;
        g_ui.listResizeDrag = true;
        g_ui.listResizeX = x;
        g_ui.listResizeWidth = (g_r.listPanel.right - g_r.listPanel.left) / g_u;
        SetCapture(g_overlay.Hwnd());
        return;
    }

    if (g_ui.playlist && InRect(g_r.listPanel, x, y)) {
        if (InRect(g_r.listClose, x, y)) { TogglePlaylist(); return; }
        if (InRect(g_r.listReset, x, y)) { ResetPlaylistWidth(); return; }
        if (InRect(g_r.listFolder, x, y)) { ListFolderMenu(); return; }
        if (InRect(g_r.listAdd, x, y)) { OpenMediaMenu(false); return; }
        if (InRect(g_r.listImport, x, y)) { auto files = ChooseMedia(true); if (!files.empty()) AddMediaFiles(files, false); return; }
        if (g_ui.listScrollMax > 0 && x >= g_r.listPanel.right - S(12) && y >= g_r.listView.top && y < g_r.listView.bottom) {
            const float height = g_r.listView.bottom - g_r.listView.top;
            const float thumb = std::max(S(20), height * height / (height + g_ui.listScrollMax));
            const float top = g_r.listView.top + (height - thumb) * g_ui.listScroll / g_ui.listScrollMax;
            if (y < top || y > top + thumb) g_ui.listScroll = Clampf((y - g_r.listView.top - thumb/2) / std::max(1.f,height-thumb) * g_ui.listScrollMax,0,g_ui.listScrollMax);
            g_ui.listScrollDrag = true; g_ui.listDragY = y; g_ui.listDragScroll = g_ui.listScroll;
            SetCapture(g_overlay.Hwnd()); return;
        }
        if (InRect(g_r.listView, x, y)) {
            int index = (int)((y - g_r.listView.top + g_ui.listScroll) / S(56));
            if (index < (int)g_lists.Active().files.size()) {
                g_ui.listSelection = index;
                if (x >= g_r.listView.right - S(38)) RemoveListItem(index);
                else PlayListItem(g_lists.selected, index);
            }
        }
        return;
    }

    if (!g_fullscreen) {
        if (InRect(g_r.tbClose, x, y)) { PostMessageW(g_main, WM_CLOSE, 0, 0); return; }
        if (InRect(g_r.tbMax, x, y)) { ShowWindow(g_main, g_maximized ? SW_RESTORE : SW_MAXIMIZE); return; }
        if (InRect(g_r.tbMin, x, y)) { ShowWindow(g_main, SW_MINIMIZE); return; }
        if (InRect(g_r.topOpen, x, y)) { OpenMediaMenu(); return; }
    }
    if (g_ui.settings) { g_ui.settings = false; return; }
    if (!controlsWereVisible && InRect(g_r.bar, x, y)) return;

    if (InRect(g_r.play, x, y)) { if (g_hasMedia) g_player.TogglePlay(); return; }
    if (InRect(g_r.prev, x, y)) { if (CanStepPlaylist(-1)) StepPlaylist(-1); return; }
    if (InRect(g_r.next, x, y)) { if (CanStepPlaylist(1)) StepPlaylist(1); return; }
    if (InRect(g_r.back, x, y)) { g_player.SeekRelative(-10); return; }
    if (InRect(g_r.fwd, x, y)) { g_player.SeekRelative(10); return; }
    if (InRect(g_r.seekHit, x, y)) {
        if (g_player.CanSeek()) {
            if (g_ui.ffActive) ExitFF();
            g_ui.pressInVideo = g_ui.pendingClick = false;
            g_ui.seekWasPlaying = g_player.WantsToPlay();
            g_player.Pause(); g_player.SetScrubbing(true);
            g_ui.seekDrag = true; g_ui.seekSubmitted = -1; g_ui.seekRequestTime = 0;
            SetCapture(g_overlay.Hwnd()); ActionSeekToX(x); UpdateSeekPreview();
        }
        return;
    }
    if (InRect(g_r.volTrack, x, y)) {
        g_ui.volDrag = true;
        SetCapture(g_overlay.Hwnd());
        float t = Clampf((x - g_r.volTrack.left) / std::max(1.0f, g_r.volTrack.right - g_r.volTrack.left), 0, 1);
        SetVolume(t);
        return;
    }
    if (InRect(g_r.volIcon, x, y)) {
        ToggleMute();
        return;
    }
    if (InRect(g_r.cc, x, y)) {
        g_ui.subsOn = !g_ui.subsOn;
        SaveConfig();
        Toast(g_ui.subsOn ? L"字幕已显示" : L"字幕已隐藏");
        return;
    }
    if (InRect(g_r.gear, x, y)) { ToggleSettings(); return; }
    if (InRect(g_r.more, x, y)) { PlaybackMenu(); return; }
    if (InRect(g_r.list, x, y)) { TogglePlaylist(); return; }
    if (InRect(g_r.fs, x, y)) { ToggleFullscreen(); return; }

    if (!g_hasMedia) {
        if (InRect(g_r.openBtn, x, y)) OpenFileDialog();
        return;
    }
    // The empty area of the bottom bar is not a play/pause hit target.
    if (g_ui.controlsVisible && InRect(g_r.bar, x, y)) return;
    if (VideoClickTarget(x, y)) {
        g_ui.pressInVideo = true;
        g_ui.pressTime = QpcNow();
        g_ui.pressPt.x = (LONG)x;
        g_ui.pressPt.y = (LONG)y;
        SetCapture(g_overlay.Hwnd());
    }
}

static void HandleMouseMove(float x, float y) {
    if (g_ui.mouse.x == (LONG)x && g_ui.mouse.y == (LONG)y) return;
    g_ui.mouse = {(LONG)x, (LONG)y};
    g_ui.hoverTime = QpcNow();
    g_ui.dirty = true;
    g_ui.hoverSeek = InRect(g_r.seekHit, x, y);
    g_ui.lastActivity = QpcNow();
    g_ui.controlsVisible = true;
    if (g_ui.listResizeDrag) {
        const float maximum = std::max(240.0f, g_overlay.Width()/g_u - 320);
        const float width = Clampf(g_ui.listResizeWidth + (g_ui.listResizeX - x)/g_u, 240, maximum);
        if (g_ui.listResizeMoved || std::abs(width - g_ui.listResizeWidth) > .1f) {
            g_ui.listResizeMoved = true;
            ChangePlaylistWidth(width, false);
        }
        return;
    }
    if (g_ui.listScrollDrag) {
        const float height = g_r.listView.bottom - g_r.listView.top;
        const float thumb = std::max(S(20), height * height / (height + g_ui.listScrollMax));
        g_ui.listScroll = Clampf(g_ui.listDragScroll + (y - g_ui.listDragY) * g_ui.listScrollMax / std::max(1.f,height-thumb), 0, g_ui.listScrollMax);
    }
    if (g_ui.seekDrag) { ActionSeekToX(x); UpdateSeekPreview(); return; }
    if (g_ui.subDrag) { ActionSubSize(x); return; }
    if (g_ui.rateDrag) { ActionHoldRate(x); return; }
    if (g_ui.volDrag) {
        float t = Clampf((x - g_r.volTrack.left) / std::max(1.0f, g_r.volTrack.right - g_r.volTrack.left), 0, 1);
        SetVolume(t);
        return;
    }
    if (g_ui.pressInVideo && !g_ui.ffActive) {
        if (abs((int)x - g_ui.pressPt.x) > 8 || abs((int)y - g_ui.pressPt.y) > 8)
            g_ui.pressInVideo = false;
    }
}

static void HandleMouseUp(float x, float y) {
    if (g_ui.listResizeDrag) HandleMouseMove(x,y);
    if (g_ui.listResizeDrag && g_ui.listResizeMoved) SaveConfig();
    g_ui.listResizeDrag = g_ui.listResizeMoved = false;
    g_ui.listScrollDrag = false;
    if (g_ui.seekDrag) { ActionSeekToX(x); EndSeekPreview(); }
    if (g_ui.volDrag) { g_ui.volDrag = false; SaveConfig(); }
    if (g_ui.subDrag) { g_ui.subDrag = false; SaveConfig(); }
    if (g_ui.rateDrag) { g_ui.rateDrag = false; SaveConfig(); }

    if (g_ui.ffActive && !g_ui.rightFF) { ExitFF(); g_ui.pressInVideo = false; }
    const bool click=g_ui.pressInVideo && QpcNow()-g_ui.pressTime<0.35 && !g_ui.suppressClick && VideoClickTarget(x,y);
    const bool wasPlaying=click && g_player.WantsToPlay();
    g_ui.pressInVideo = false;
    g_ui.suppressClick = false;
    if (GetCapture() == g_overlay.Hwnd()) ReleaseCapture();
    // ReleaseCapture sends WM_CAPTURECHANGED; queue only after its cancellation.
    if(click){g_ui.pendingClick=true;g_ui.pendingClickTime=g_ui.pressTime;g_ui.clickWasPlaying=wasPlaying;}
    g_ui.dirty = true;
}

static void CancelGesture() {
    if (g_ui.ffActive) ExitFF();
    EndSeekPreview();
    if (g_ui.subDrag || g_ui.rateDrag || g_ui.volDrag || g_ui.listResizeMoved) SaveConfig();
    g_ui.pressInVideo = g_ui.pendingClick = g_ui.seekDrag = g_ui.volDrag = g_ui.subDrag = false;
    g_ui.rateDrag = false;
    g_ui.listScrollDrag = false;
    g_ui.listResizeDrag = g_ui.listResizeMoved = false;
    g_ui.rightKeyDown = g_ui.rightKeyUsed = false;
    g_ui.suppressClick = false;
    g_ui.dirty = true;
    if (GetCapture() == g_overlay.Hwnd()) ReleaseCapture();
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------
static void Tick() {
    if(g_captionStopping && g_captionStopped.load()) {
        const bool save=g_captionSavePending && g_captionStopSession==g_captionSession;
        g_captionSavePending=false;FinishCaptionStop();
        if(!g_coreAwait && !g_player.Loading())StartCaptionsIfNeeded(false);
        if(save)SaveCaptionsDialog();
        g_ui.dirty=true;
    }
    PollSources();
    if(g_coreAwait && !g_player.Loading()) {
        const auto state=g_player.CoreInfo();
        if(state.ready || state.failed) {
            g_coreAwait=false;
            if(state.ready) {
                if(!g_coreSidecar.empty())g_player.AddSubtitle(g_coreSidecar);
                g_coreSidecar.clear();UpdateVideoWindows();
                if(g_ui.captionsOn)StartCaptionsIfNeeded();else g_player.HoldPlayback(false);
                g_player.SubtitlePresentation(g_ui.subsOn && !g_ui.captionsOn,g_ui.subScale);
            }
        }
    }
    if(g_player.CoreActive()) {
        const bool hasAudio=g_hasMedia && !g_player.Loading() && g_player.HasAudio();
        if(hasAudio && !g_coreHadAudio && !g_coreAwait && g_ui.captionsOn &&
            !g_captionStarting.load() && !g_captions.Running()) {
            // Live sources can discover their audio track after FILE_LOADED.
            // A late track must not interrupt playback while its model starts.
            StartCaptionsIfNeeded(false);
        }
        g_coreHadAudio=hasAudio;
        static bool lastVisibility=false;static float lastScale=-1;
        const bool visible=g_ui.subsOn && !g_ui.captionsOn;
        if(lastVisibility!=visible || lastScale!=g_ui.subScale) {
            g_player.SubtitlePresentation(visible,g_ui.subScale);lastVisibility=visible;lastScale=g_ui.subScale;
        }
    }
    UpdateSeekPreview();
    const int captionResult = g_captionStopping?0:g_captionResult.exchange(0);
    if (captionResult) {
        if (g_captionStart.joinable()) g_captionStart.join();
        g_captionStarting.store(false);
        if (captionResult > 0 && g_ui.captionsOn && g_hasMedia && g_player.HasAudio()) {
            StartCaptionTap();
        } else {
            StopCaptionsInBackground();
            g_player.HoldPlayback(false);
            if (captionResult < 0 && g_ui.captionsOn && g_hasMedia && g_player.HasAudio()) {
                g_ui.captionsOn = false;
                Toast(L"字幕模型加载失败，媒体仍可播放");
                SaveConfig();
            }
        }
    }
    const bool audioRecovery = g_player.NeedsAudioRecovery();
    if (g_player.NeedsFallback() || audioRecovery) {
        const bool wasHardware = g_player.IsHardware();
        CancelGesture();
        const bool ok = g_player.SwitchToSoftware();
        g_hasMedia = ok;
        UpdateVideoWindows();
        if (!ok) { StopCaptions(); g_player.Close(); g_queueFolder = UINT_MAX; g_file.clear(); UpdateTitle(); }
        Toast(ok ? (audioRecovery ? (g_player.AudioUnavailable() ? L"音频输出暂不可用，视频继续静音播放" : L"音频设备变化，已恢复播放连接") :
            wasHardware ? L"系统播放异常，已切换为软件解码" : L"系统解码器不兼容，已切换 FFmpeg 软件解码") : L"无法恢复媒体播放，请重新打开文件或检查音频设备");
    } else if (g_hasMedia && g_player.Failed()) {
        const auto error=g_player.CoreInfo().error;
        StopCaptions(); g_player.Close(); g_hasMedia = false; g_file.clear();g_coreAwait=false;
        UpdateTitle(); UpdateVideoWindows();
        Toast(error.empty()?L"当前解码器无法继续播放该媒体，请检查文件或系统解码组件":error);
    }
    if (!g_captionStarting.load() && g_captions.Running() && g_player.TapFailed()) {
        StopCaptions(); g_player.HoldPlayback(false); g_ui.captionsOn = false;
        Toast(L"该音轨的实时字幕采集中断，媒体继续播放"); SaveConfig();
    } else if (!g_captionStarting.load() && g_captions.Running()) {
        CollectCaptionFinals();
        CollectTranslations();
    } else if (!g_captionStarting.load() && g_ui.captionsOn && g_player.TapRunning()) {
        StopCaptions();
        g_ui.captionsOn = false;
        Toast(L"实时字幕识别已停止，请重新开启；详情见运行日志");
        SaveConfig();
    }
    if (g_ui.pendingClick && QpcNow() - g_ui.pendingClickTime > VideoClickDelay()) {
        CommitVideoClick();
    }
    double idle = QpcNow() - g_ui.lastActivity;
    const bool wasVisible = g_ui.controlsVisible;
    const bool pointerInBar = InRect(g_r.bar, (float)g_ui.mouse.x, (float)g_ui.mouse.y);
    if (g_player.IsPlaying() && !g_ui.seekDrag && !g_ui.volDrag && !g_ui.subDrag && !g_ui.rateDrag && !g_ui.settings && !g_ui.playlist && !g_ui.help && !g_ui.ffActive && !pointerInBar && idle > 2.6)
        g_ui.controlsVisible = false;
    if (!g_player.IsPlaying()) g_ui.controlsVisible = true;
    if (wasVisible != g_ui.controlsVisible) g_ui.dirty = true;

    // Changes are sampled cheaply; a full layered-window upload only happens
    // when visible text, progress, hover, a toast or a control actually changes.
    static long long lastProgress = -1;
    static bool lastPlaying = false;
    static bool lastHardware = false;
    static unsigned lastRenderState = 0;
    static std::wstring lastCaption;
    static bool tooltipVisible = false;
    const bool playing = g_player.IsPlaying();
    const bool hardware=g_player.IsHardware();
    const auto rendering=g_player.RenderInfo();
    const unsigned renderState=(unsigned)rendering.inputDepth | (unsigned)rendering.outputDepth<<5 |
        (unsigned)rendering.hdrInput<<10 | (unsigned)rendering.hdrOutput<<11 | (unsigned)rendering.shader<<12 | (unsigned)rendering.shaderError<<13;
    if(hardware!=lastHardware || renderState!=lastRenderState)g_ui.dirty=true;
    lastHardware=hardware;lastRenderState=renderState;
    const long long progress = g_ui.controlsVisible && g_hasMedia ? (long long)(g_player.Position() * 10) : -1;
    static bool lastSeekReady = true;
    const bool seekReady = g_player.SeekReady();
    if (seekReady != lastSeekReady) g_ui.dirty = true;
    lastSeekReady = seekReady;
    if (progress != lastProgress || playing != lastPlaying) g_ui.dirty = true;
    lastProgress = progress;
    lastPlaying = playing;
    auto caption=VisibleCaption();
    if (lastCaption != caption) { lastCaption = std::move(caption); g_ui.dirty = true; }
    const bool tooltip = QpcNow() - g_ui.hoverTime >= 0.55;
    if (tooltip != tooltipVisible) { tooltipVisible = tooltip; g_ui.dirty = true; }
    if (!g_ui.toast.empty()) g_ui.dirty = true;

    if (g_ui.rightKeyDown && !g_ui.rightKeyUsed && QpcNow() - g_ui.rightKeyTime > 0.34) {
        g_ui.rightKeyUsed = true;
        if (!g_ui.ffActive) EnterFF(true);
    }
    if (g_ui.pressInVideo && !g_ui.ffActive && QpcNow() - g_ui.pressTime > 0.34) EnterFF();
    if (g_ui.ffActive) {
        g_ui.dirty = true;
        if (g_ui.ffScrub) {
            double now = QpcNow();
            if (now - g_ui.lastScrub >= 0.20) {
                g_ui.lastScrub = now;
                double rate = FastForwardRate();
                g_player.SeekRelative(rate * 0.20);
            }
        }
    }
    if (!g_player.IsEof()) g_queueEnded = false;
    else if (!g_queueEnded && g_player.SeekReady() && !g_ui.seekDrag && !g_ui.ffActive && !g_opening && !g_sourceWorker.joinable()) {
        g_queueEnded = true;
        if (g_player.PlaybackRequested() && CanStepPlaylist(1)) StepPlaylist(1);
    }
}

// ---------------------------------------------------------------------------
// Window procs
// ---------------------------------------------------------------------------
static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
        ComputeLayout(g_overlay.Width(), g_overlay.Height());
        RECT cr;
        GetClientRect(hwnd, &cr);
        const float b = 6;
        bool left = x < b, right = x >= cr.right - b, top = y < b, bottom = y >= cr.bottom - b;
        if (!g_fullscreen && !g_maximized && (left || right || top || bottom)) {
            WPARAM code = HTCLIENT;
            if (top && left) code = HTTOPLEFT;
            else if (top && right) code = HTTOPRIGHT;
            else if (bottom && left) code = HTBOTTOMLEFT;
            else if (bottom && right) code = HTBOTTOMRIGHT;
            else if (left) code = HTLEFT;
            else if (right) code = HTRIGHT;
            else if (top) code = HTTOP;
            else code = HTBOTTOM;
            CancelGesture();
            POINT point = {(LONG)x, (LONG)y};
            ClientToScreen(hwnd, &point);
            SendMessageW(g_main, WM_NCLBUTTONDOWN, code, MAKELPARAM(point.x, point.y));
            return 0;
        }
        if (!g_fullscreen && InRect(g_r.title, x, y) && x < g_r.topOpen.left) {
            CancelGesture();
            SendMessageW(g_main, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
            return 0;
        }
        HandleClick(x, y);
        PaintOverlay();
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        float x = (float)GET_X_LPARAM(lParam), y = (float)GET_Y_LPARAM(lParam);
        ComputeLayout(g_overlay.Width(), g_overlay.Height());
        if (g_ui.playlist && !g_ui.help && InRect(g_r.listDivider, x, y)) {
            ResetPlaylistWidth(); PaintOverlay(); return 0;
        }
        bool isVideo = VideoClickTarget(x,y);
        if(isVideo&&(!g_ui.pendingClick||QpcNow()-g_ui.pendingClickTime>VideoClickDelay())){
            // Windows may still classify a later independent press as a double click.
            g_ui.suppressClick=false;HandleClick(x,y);PaintOverlay();return 0;
        }
        g_ui.pendingClick = false;
        g_ui.suppressClick = true;
        g_ui.pressInVideo = false;
        if (isVideo) ToggleFullscreen();
        else if (!g_fullscreen && InRect(g_r.title, x, y) && x < g_r.topOpen.left)
            ShowWindow(g_main, g_maximized ? SW_RESTORE : SW_MAXIMIZE);
        else {
            // CS_DBLCLKS replaces the second button-down with this message.
            // Buttons must still receive that activation, including play/pause.
            g_ui.suppressClick = false;
            HandleClick(x, y);
        }
        PaintOverlay();
        return 0;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        HandleMouseMove((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam));
        if (g_ui.listResizeDrag && g_ui.viewportDirty) PaintOverlay();
        return 0;
    }
    case WM_MOUSELEAVE:
        g_ui.mouse = {-100, -100};
        g_ui.hoverSeek = false;
        g_ui.dirty = true;
        return 0;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        CancelGesture();
        return 0;
    case WM_KILLFOCUS:
        CancelGesture();
        g_ui.pendingClick = false;
        return 0;
    case WM_SETCURSOR: {
        POINT point; GetCursorPos(&point); ScreenToClient(hwnd, &point);
        const bool divider = g_ui.playlist && !g_ui.help && (g_ui.listResizeDrag || InRect(g_r.listDivider, (float)point.x, (float)point.y));
        bool hot = false;
        for (const auto& control : Controls())
            if (control.enabled && ControlVisible(control) && InRect(control.rect, (float)point.x, (float)point.y)) { hot = true; break; }
        SetCursor(LoadCursorW(nullptr, g_opening ? IDC_WAIT : divider ? IDC_SIZEWE : (hot ? IDC_HAND : IDC_ARROW)));
        return TRUE;
    }
    case WM_LBUTTONUP:
        HandleMouseUp((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam));
        PaintOverlay();
        return 0;
    case WM_MOUSEWHEEL: {
        g_ui.pendingClick = false;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &point);
        if (g_ui.help) return 0;
        if (g_ui.settings && InRect(g_r.panel, (float)point.x, (float)point.y)) {
            g_ui.settingsScroll = Clampf(g_ui.settingsScroll - (float)delta / WHEEL_DELTA * S(48), 0, g_ui.settingsScrollMax);
            ComputeLayout(g_overlay.Width(), g_overlay.Height());
        } else if (g_ui.playlist && InRect(g_r.listPanel, (float)point.x, (float)point.y)) {
            g_ui.listScroll = Clampf(g_ui.listScroll - (float)delta / WHEEL_DELTA * S(56), 0, g_ui.listScrollMax);
        } else {
            SetVolume(g_ui.volume + (float)delta / WHEEL_DELTA * 0.05f, true);
            Toast(L"音量 " + std::to_wstring((int)(g_ui.volume * 100)) + L"%");
        }
        g_ui.lastActivity = QpcNow();
        g_ui.controlsVisible = true;
        PaintOverlay();
        return 0;
    }
    case WM_KEYUP:
        if (wParam == VK_RIGHT && g_ui.rightKeyDown) {
            const bool tap = !g_ui.rightKeyUsed;
            g_ui.rightKeyDown = false;
            if (g_ui.ffActive && g_ui.rightFF) ExitFF();
            if (tap && g_hasMedia) g_player.SeekRelative(5);
            g_ui.rightKeyUsed = false;
            PaintOverlay(); return 0;
        }
        break;
    case WM_RBUTTONUP:
        CoreSettingsMenu();PaintOverlay();return 0;
    case WM_KEYDOWN: {
        if (g_opening) return 0;
        g_ui.pendingClick = false;
        bool repeating = (lParam & (1LL << 30)) != 0;
        g_ui.lastActivity = QpcNow();
        g_ui.controlsVisible = true;
        if (repeating && wParam != VK_LEFT && wParam != VK_RIGHT && wParam != VK_UP && wParam != VK_DOWN) return 0;
        if (g_ui.help && wParam != VK_ESCAPE && wParam != VK_F1) return 0;
        if (wParam != VK_RIGHT && g_ui.rightKeyDown) CancelGesture();
        if (wParam == VK_TAB) {
            auto controls = Controls();
            int direction = GetKeyState(VK_SHIFT) & 0x8000 ? -1 : 1;
            for (size_t i = 0; i < controls.size(); ++i) {
                g_ui.focus = (g_ui.focus + direction + (int)controls.size()) % (int)controls.size();
                if (controls[g_ui.focus].enabled && controls[g_ui.focus].rect.right > controls[g_ui.focus].rect.left) break;
            }
            g_ui.keyboardFocus = true;
            if (g_ui.settings && g_ui.focus > 0) {
                auto r = controls[g_ui.focus].rect;
                if (r.top < g_r.settingsView.top) g_ui.settingsScroll -= g_r.settingsView.top - r.top + S(8);
                if (r.bottom > g_r.settingsView.bottom) g_ui.settingsScroll += r.bottom - g_r.settingsView.bottom + S(8);
                ComputeLayout(g_overlay.Width(), g_overlay.Height());
            }
            PaintOverlay();
            return 0;
        }
        auto controls = Controls();
        const bool listKeys = g_ui.playlist && (!g_ui.keyboardFocus || (g_ui.focus >= 0 && g_ui.focus < (int)controls.size() && controls[g_ui.focus].slider == 5));
        if (listKeys && (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_HOME || wParam == VK_END || wParam == VK_DELETE || wParam == VK_RETURN)) {
            if (wParam == VK_UP) --g_ui.listSelection;
            if (wParam == VK_DOWN) ++g_ui.listSelection;
            if (wParam == VK_HOME) g_ui.listSelection = 0;
            if (wParam == VK_END) g_ui.listSelection = (int)g_lists.Active().files.size() - 1;
            if (wParam == VK_DELETE) RemoveListItem(g_ui.listSelection);
            if (wParam == VK_RETURN) PlayListItem(g_lists.selected, g_ui.listSelection);
            RevealListSelection();
            g_ui.focus = 4; g_ui.keyboardFocus = true;
            PaintOverlay(); return 0;
        }
        if (g_ui.keyboardFocus && g_ui.focus >= 0 && g_ui.focus < (int)controls.size()) {
            auto control = controls[g_ui.focus];
            if (control.slider == 6 && (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_HOME || wParam == VK_RETURN || wParam == VK_SPACE)) {
                if (wParam == VK_LEFT || wParam == VK_RIGHT)
                    ChangePlaylistWidth((g_r.listPanel.right - g_r.listPanel.left)/g_u + (wParam == VK_LEFT ? 8 : -8), true);
                else ResetPlaylistWidth();
                PaintOverlay(); return 0;
            }
            if (wParam == VK_RETURN || (wParam == VK_SPACE && !control.slider)) {
                const bool focus = g_ui.keyboardFocus;
                HandleClick((control.rect.left + control.rect.right) / 2, (control.rect.top + control.rect.bottom) / 2);
                HandleMouseUp((control.rect.left + control.rect.right) / 2, (control.rect.top + control.rect.bottom) / 2);
                g_ui.keyboardFocus = focus;
                PaintOverlay(); return 0;
            }
            if (control.slider > 0 && control.slider < 5 && !(control.slider == 1 && wParam == VK_RIGHT && g_cfg.rightSpeed) &&
                (wParam == VK_LEFT || wParam == VK_RIGHT || (control.slider != 1 && (wParam == VK_UP || wParam == VK_DOWN)))) {
                float step = wParam == VK_RIGHT || wParam == VK_UP ? 0.05f : -0.05f;
                if (control.slider == 1) g_player.SeekRelative(step * 100);
                if (control.slider == 2) SetVolume(g_ui.volume + step, true);
                if (control.slider == 3) { g_ui.subScale = Clampf(g_ui.subScale + step, 0.6f, 2.2f); SaveConfig(); }
                if (control.slider == 4) { g_cfg.holdRate = Clampf(g_cfg.holdRate + (step > 0 ? 0.25f : -0.25f), 1.25f, 8.0f); SaveConfig(); }
                PaintOverlay(); return 0;
            }
        }
        switch (wParam) {
        case VK_SPACE: g_player.TogglePlay(); PaintOverlay(); return 0;
        case VK_LEFT: g_player.SeekRelative(-5); PaintOverlay(); return 0;
        case VK_RIGHT:
            if (!g_cfg.rightSpeed) g_player.SeekRelative(5);
            else if (!repeating && !g_ui.rightKeyDown && g_hasMedia && !g_ui.seekDrag) {
                g_ui.rightKeyDown = true; g_ui.rightKeyUsed = false; g_ui.rightKeyTime = QpcNow();
                g_ui.pendingClick = false;
            }
            PaintOverlay(); return 0;
        case VK_PRIOR: StepPlaylist(-1); PaintOverlay(); return 0;
        case VK_NEXT: StepPlaylist(1); PaintOverlay(); return 0;
        case VK_UP:
            SetVolume(g_ui.volume + 0.05f, true);
            Toast(L"音量 " + std::to_wstring((int)(g_ui.volume * 100)) + L"%");
            PaintOverlay();
            return 0;
        case VK_DOWN:
            SetVolume(g_ui.volume - 0.05f, true);
            Toast(L"音量 " + std::to_wstring((int)(g_ui.volume * 100)) + L"%");
            PaintOverlay();
            return 0;
        case 'F':
        case VK_F11: ToggleFullscreen(); PaintOverlay(); return 0;
        case 'M': ToggleMute(); PaintOverlay(); return 0;
        case VK_HOME: g_player.Seek(0); PaintOverlay(); return 0;
        case VK_END: g_player.Seek(g_player.Duration()); PaintOverlay(); return 0;
        case 'O': OpenFileDialog(); return 0;
        case 'U': if(GetKeyState(VK_CONTROL)&0x8000) OpenNetworkDialog(); return 0;
        case 'S': g_ui.subsOn = !g_ui.subsOn; SaveConfig(); PaintOverlay(); return 0;
        case 'C': ActionToggleCaptions(g_ui.settings); PaintOverlay(); return 0;
        case 'G': ToggleSettings(); PaintOverlay(); return 0;
        case 'T': CoreSettingsMenu();PaintOverlay();return 0;
        case VK_OEM_COMMA: g_player.FrameStep(-1);PaintOverlay();return 0;
        case VK_OEM_PERIOD: g_player.FrameStep(1);PaintOverlay();return 0;
        case VK_OEM_4: Toast(g_player.SetABLoop(g_player.Position(),-1)?L"已设 A 点":L"当前播放路径或媒体不支持 A–B 循环");PaintOverlay();return 0;
        case VK_OEM_6: {auto state=g_player.CoreInfo();if(state.abA<0 || !g_player.SetABLoop(state.abA,g_player.Position()))Toast(L"请先设 A 点，B 点需要位于 A 点之后");else Toast(L"A–B 循环已开启");PaintOverlay();return 0;}
        case VK_OEM_5: g_player.SetABLoop(-1,-1);Toast(L"已清除 A–B 循环");PaintOverlay();return 0;
        case 'P': TogglePlaylist(); PaintOverlay(); return 0;
        case VK_F1: g_ui.help = !g_ui.help; PaintOverlay(); return 0;
        case VK_ESCAPE:
            if(g_sourceWorker.joinable()){CancelSources();Toast(L"已取消媒体连接");PaintOverlay();return 0;}
            if(g_player.CancelLoading()) {
                g_hasMedia=g_coreAwait=false;g_coreSidecar.clear();g_file.clear();g_queueFolder=UINT_MAX;
                UpdateTitle();UpdateVideoWindows();Toast(L"已取消媒体加载");PaintOverlay();return 0;
            }
            CancelGesture();
            if (GetCapture() == hwnd) ReleaseCapture();
            if (g_ui.help) g_ui.help = false;
            else if (g_ui.settings) g_ui.settings = false;
            else if (g_ui.playlist) { g_ui.playlist = false; g_ui.viewportDirty = true; }
            else if (g_fullscreen) ToggleFullscreen();
            g_ui.focus = -1; g_ui.keyboardFocus = false;
            PaintOverlay();
            return 0;
        }
        break;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK VideoProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        g_player.Repaint();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        g_player.Repaint();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SETFOCUS:
        if (g_overlay.Hwnd()) SetFocus(g_overlay.Hwnd());
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
        if (g_overlay.Hwnd()) return SendMessageW(g_overlay.Hwnd(), msg, wParam, lParam);
        break;
    case WM_ERASEBKGND: {
        RECT client; GetClientRect(hwnd, &client);
        FillRect((HDC)wParam, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    case WM_NCCALCSIZE:
        // Initial creation uses FALSE too: leave the whole window as client
        // area, rather than retaining a native resize frame around our UI.
        return 0;
    case WM_NCACTIVATE:
        if (!IsIconic(hwnd))
            return DefWindowProcW(hwnd, msg, wParam, -1);
        break;
    case WM_NCPAINT:
        // The layered UI owns the frame. Native activation/theme repainting
        // would otherwise draw light resize borders over its dark edges.
        if (!IsIconic(hwnd)) return 0;
        break;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        MONITORINFO mi = { sizeof(MONITORINFO) };
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        }
        mmi->ptMinTrackSize.x = (LONG)S(560);
        mmi->ptMinTrackSize.y = (LONG)S(360);
        return 0;
    }
    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, 1, 50, nullptr);
        return 0;
    case WM_SIZE: {
        int w = LOWORD(lParam), h = HIWORD(lParam);
        g_maximized = IsZoomed(hwnd) != 0;
        if (w > 0 && h > 0) {
            g_overlay.Resize(w, h);
            if (g_overlay.Hwnd()) SetWindowPos(g_overlay.Hwnd(), HWND_TOP, 0, 0, w, h, SWP_NOACTIVATE);
            UpdateVideoWindows();
            PaintOverlay();
        }
        return 0;
    }
    case WM_DPICHANGED: {
        g_u = (float)HIWORD(wParam) / 96.0f;
        g_tf.clear();
        RECT* pr = (RECT*)lParam;
        SetWindowPos(hwnd, nullptr, pr->left, pr->top, pr->right - pr->left, pr->bottom - pr->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_TIMER:
        if (wParam == 1) {
            Tick();
            if (g_ui.dirty) PaintOverlay();
        }
        return 0;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        std::vector<std::wstring> files;
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count && i < 10000; ++i) {
            const UINT size = DragQueryFileW(drop, i, nullptr, 0);
            std::vector<wchar_t> path(size + 1);
            if (!DragQueryFileW(drop, i, path.data(), size + 1)) continue;
            std::wstring ext = LowerExt(path.data());
            if (ext == L".srt" || ext == L".vtt" || ext == L".ass" || ext == L".ssa" || ext==L".smi" || ext==L".sami" || ext==L".sup" || ext==L".idx") {
                if(g_player.AddSubtitle(path.data())){g_subs.Clear();g_ui.subsOn=true;SaveConfig();Toast(L"已添加字幕轨");}
                else if (g_subs.Load(path.data())) { g_ui.subsOn = true; SaveConfig(); Toast(L"字幕已导入"); }
                else Toast(L"无法解析该字幕文件");
            } else if (GetFileAttributesW(path.data()) & FILE_ATTRIBUTE_DIRECTORY) {
                auto imported = MediaInFolder(path.data()); files.insert(files.end(), imported.begin(), imported.end());
            } else files.emplace_back(path.data());
        }
        DragFinish(drop);
        if (!files.empty()) AddMediaFiles(files, true);
        g_ui.dirty = true;
        return 0;
    }
    case WM_CLOSE:
        g_modelStore.Cancel();
        CancelSources(true);
        CancelGesture();
        SaveConfig();
        DiscardUnsavedCaptions();
        g_subs.Clear();
        g_player.Close();
        g_overlay.Destroy();
        g_brush.reset();
        g_tf.clear();
        g_gfx.Shutdown();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    g_inst = inst;
    LogMsg("=== Lumen Player 2.2.5 start (mpv/gpu-next/WASAPI/libass/selectable captions) ===");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        LogMsg("UNHANDLED EXCEPTION code=0x%08X at %p", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
        TerminateProcess(GetCurrentProcess(), 1);
        return EXCEPTION_EXECUTE_HANDLER;
    });
    {
        typedef BOOL (WINAPI* FnSetCtx)(HANDLE);
        FnSetCtx fn = (FnSetCtx)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
        if (fn) fn((HANDLE)-4);   // PER_MONITOR_AWARE_V2
        else SetProcessDPIAware();
    }

    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) { CoUninitialize(); return 1; }

    LoadConfig();
    const bool listsLoaded = g_lists.Load(PlaylistPath());

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LumenMain";
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    WNDCLASSEXW vo = { sizeof(vo) };
    vo.style = CS_DBLCLKS;
    vo.lpfnWndProc = OverlayProc;
    vo.hInstance = inst;
    vo.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    vo.lpszClassName = L"LumenOverlay";
    RegisterClassExW(&vo);

    WNDCLASSEXW vv = { sizeof(vv) };
    vv.style = CS_HREDRAW | CS_VREDRAW;
    vv.lpfnWndProc = VideoProc;
    vv.hInstance = inst;
    vv.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    vv.lpszClassName = L"LumenVideo";
    RegisterClassExW(&vv);

    RECT wa = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int availW = wa.right - wa.left, availH = wa.bottom - wa.top;
    int winW = 1120, winH = 700;
    if (winW > availW * 92 / 100) winW = availW * 92 / 100;
    if (winH > availH * 90 / 100) winH = availH * 90 / 100;
    int sx = wa.left + (availW - winW) / 2;
    int sy = wa.top + (availH - winH) / 2;
    g_main = CreateWindowExW(0, L"LumenMain", L"Lumen Player",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE,
        sx < 0 ? 0 : sx, sy < 0 ? 0 : sy, winW, winH, nullptr, nullptr, inst, nullptr);
    if (!g_main) { MFShutdown(); CoUninitialize(); return 1; }

    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_main, 20, &dark, sizeof(dark));
    int corner = 2;
    DwmSetWindowAttribute(g_main, 33, &corner, sizeof(corner));

    RECT rc;
    GetClientRect(g_main, &rc);
    {
        typedef UINT (WINAPI* Fn)(HWND);
        Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        UINT dpi = fn ? fn(g_main) : 96;
        g_u = dpi ? dpi / 96.0f : 1.0f;
    }

    g_hwVideo = CreateWindowExW(0, L"LumenVideo", L"", WS_CHILD,
        0, 0, rc.right, rc.bottom, g_main, nullptr, inst, nullptr);
    g_swVideo = CreateWindowExW(0, L"LumenVideo", L"", WS_CHILD,
        0, 0, rc.right, rc.bottom, g_main, nullptr, inst, nullptr);
    if (!g_gfx.Init()) {
        MessageBoxW(g_main, L"无法初始化界面渲染器", L"Lumen", MB_ICONERROR);
        return 0;
    }
    if (!g_overlay.Create(g_main, L"LumenOverlay", 100)) {
        g_gfx.Shutdown(); DestroyWindow(g_main); MFShutdown(); CoUninitialize(); return 1;
    }
    g_overlay.Resize(rc.right, rc.bottom);
    g_overlay.Show(true);

    g_player.SetWindows(g_hwVideo, g_swVideo);
    UpdateVideoWindows();
    g_player.SetVolume(g_ui.volume);
    g_player.SetAudioSink([](const float* data, int frames, int rate, int ch, double pts) {
        g_captions.PushMix(data, frames, rate, ch, pts);
    }, [](double pts) { g_captions.FinishInput(pts); }, [](double pts) {
        if (!g_captionStarting.load()) g_captions.ResetTimeline(pts);
    });
    ConfigureCaptionChoice();

    ShowWindow(g_main, SW_SHOW);
    UpdateWindow(g_main);
    SetFocus(g_overlay.Hwnd());
    PaintOverlay();

    if (!listsLoaded) Toast(L"播放列表数据无法读取，原文件已保留且不会被覆盖");

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) {
        std::vector<std::wstring> files;
        for (int i = 1; i < argc; ++i) if (MediaAvailable(argv[i])) files.emplace_back(argv[i]);
        if (!files.empty()) AddMediaFiles(files, true);
    }
    if (argv) LocalFree(argv);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CancelSources(true);
    g_modelStore.Cancel();g_modelStore.Wait();
    DiscardUnsavedCaptions();
    g_player.Close();
    g_overlay.Destroy();
    g_brush.reset();
    g_tf.clear();
    g_gfx.Shutdown();
    if (g_hwVideo) DestroyWindow(g_hwVideo);
    if (g_swVideo) DestroyWindow(g_swVideo);
    MFShutdown();
    CoUninitialize();
    return 0;
}
