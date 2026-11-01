#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>

// ---------------------------------------------------------------------------
// Minimal COM smart pointer (avoids <wrl> dependency quirks on MinGW)
// ---------------------------------------------------------------------------
template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(T* q) : p(q) {}
    Com(const Com& o) : p(o.p) { if (p) p->AddRef(); }
    Com(Com&& o) noexcept : p(o.p) { o.p = nullptr; }
    ~Com() { reset(); }
    Com& operator=(const Com& o) {
        if (o.p) o.p->AddRef();
        reset();
        p = o.p;
        return *this;
    }
    Com& operator=(Com&& o) noexcept {
        if (this != &o) { reset(); p = o.p; o.p = nullptr; }
        return *this;
    }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T* get() const { return p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    T** put() { reset(); return &p; }
    void** vput() { reset(); return (void**)&p; }
    T* detach() { T* q = p; p = nullptr; return q; }
};

// ---------------------------------------------------------------------------
// String / misc helpers
// ---------------------------------------------------------------------------
std::wstring ExeDir();
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
bool FileExists(const std::wstring& p);
std::wstring LowerExt(const std::wstring& path);
void LogMsg(const char* fmt, ...);
double QpcNow();
std::wstring FmtTime(double sec);
std::string FmtUtf8(const char* fmt, ...);
std::wstring Utf8ToW(const std::string& s);
std::string WToUtf8(const std::wstring& s);

inline float Clampf(float v, float lo, float hi) { return !std::isfinite(v) ? lo : (v < lo ? lo : (v > hi ? hi : v)); }
inline double Clampd(double v, double lo, double hi) { return !std::isfinite(v) ? lo : (v < lo ? lo : (v > hi ? hi : v)); }
