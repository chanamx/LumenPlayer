#include "gfx.h"

bool Gfx::Init() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put());
    if (FAILED(hr) || !factory) {
        LogMsg("D2D1CreateFactory(software) failed 0x%08X", hr);
        return false;
    }
    if (!RecreateTarget()) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)dwrite.put());
    if (FAILED(hr) || !dwrite) {
        LogMsg("DWriteCreateFactory failed 0x%08X", hr);
        return false;
    }
    LogMsg("software UI renderer ready");
    return true;
}

bool Gfx::RecreateTarget() {
    rt.reset();
    if (!factory) return false;
    D2D1_RENDER_TARGET_PROPERTIES props = {};
    props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    HRESULT hr = factory->CreateDCRenderTarget(&props, rt.put());
    if (FAILED(hr) || !rt) {
        LogMsg("CreateDCRenderTarget failed 0x%08X", hr);
        return false;
    }
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    return true;
}

bool Gfx::Bind(HDC hdc, const RECT& rc) {
    if (!rt) return false;
    HRESULT hr = rt->BindDC(hdc, &rc);
    return SUCCEEDED(hr);
}

void Gfx::Shutdown() {
    rt.reset();
    dwrite.reset();
    factory.reset();
}

bool LayeredWindow::Create(HWND parent, const std::wstring& className, int id) {
    hwnd = CreateWindowExW(WS_EX_LAYERED, className.c_str(), L"", WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        LogMsg("LayeredWindow CreateWindowEx failed %lu", GetLastError());
        return false;
    }
    return true;
}

void LayeredWindow::Destroy() {
    if (memDC) {
        if (oldBmp) SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
        memDC = nullptr;
        oldBmp = nullptr;
    }
    if (bmp) { DeleteObject(bmp); bmp = nullptr; bits = nullptr; }
    if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
}

void LayeredWindow::Resize(int w, int h) {
    if (!hwnd || w <= 0 || h <= 0) return;
    if (w == width && h == height) return;
    // Allocate replacement resources before releasing the current surface.
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* newBits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP newBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &newBits, nullptr, 0);
    HDC newDC = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!newBmp || !newDC) {
        if (newBmp) DeleteObject(newBmp);
        if (newDC) DeleteDC(newDC);
        return;
    }

    if (memDC) {
        if (oldBmp) SelectObject(memDC, oldBmp);
        DeleteDC(memDC);
        memDC = nullptr;
        oldBmp = nullptr;
    }
    if (bmp) { DeleteObject(bmp); bmp = nullptr; bits = nullptr; }

    bmp = newBmp;
    memDC = newDC;
    bits = newBits;
    width = w;
    height = h;
    oldBmp = SelectObject(memDC, bmp);

    SetWindowPos(hwnd, HWND_TOP, 0, 0, w, h, SWP_NOACTIVATE);
    Show(visible);
}

void LayeredWindow::Show(bool v) {
    visible = v;
    if (!hwnd) return;
    ShowWindow(hwnd, v ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (v) SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void LayeredWindow::Present() {
    if (!hwnd || !memDC) return;
    POINT src = { 0, 0 };
    SIZE size = { width, height };
    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(hwnd, nullptr, nullptr, &size, memDC, &src, 0, &bf, ULW_ALPHA);
}
