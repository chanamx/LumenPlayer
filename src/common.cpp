#include "common.h"
#include <cstdarg>
#include <cwctype>
#include <share.h>

std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string a((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &a[0], n, nullptr, nullptr);
    return a;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return p.substr(0, pos);
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (b.size() >= 2 && b[1] == L':') return b;
    if (!b.empty() && (b[0] == L'\\' || b[0] == L'/')) return b;
    if (a.empty()) return b;
    wchar_t last = a.back();
    if (last == L'\\' || last == L'/') return a + b;
    return a + L"\\" + b;
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring LowerExt(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return L"";
    std::wstring e = path.substr(dot);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

void LogMsg(const char* fmt, ...) {
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    SYSTEMTIME st; GetLocalTime(&st);
    char line[4096];
    va_list ap; va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    static const std::wstring path = PathJoin(ExeDir(),
#ifdef LUMEN_CORE_TEST
        L"..\\..\\test\\results\\core\\backend.log"
#elif defined(LUMEN_TEST)
        L"..\\test\\results\\backend.log"
#else
        L"LumenPlayer.log"
#endif
    );
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs) && (attrs.nFileSizeHigh || attrs.nFileSizeLow > 1024 * 1024))
        MoveFileExW(path.c_str(), (path + L".previous").c_str(), MOVEFILE_REPLACE_EXISTING);
    FILE* f = _wfsopen(path.c_str(), L"a+", _SH_DENYNO);
    if (f) {
        fprintf(f, "[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line);
        fclose(f);
    }
}

double QpcNow() {
    static const double freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / freq;
}

std::wstring FmtTime(double sec) {
    if (sec < 0 || !std::isfinite(sec)) sec = 0;
    int total = (int)(sec + 0.5);
    int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    wchar_t buf[64];
    if (h > 0) swprintf(buf, 64, L"%d:%02d:%02d", h, m, s);
    else swprintf(buf, 64, L"%d:%02d", m, s);
    return buf;
}

std::string FmtUtf8(const char* fmt, ...) {
    char buf[4096];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}
