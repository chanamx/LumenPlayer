#pragma once
#include "common.h"
#include <d2d1.h>
#include <dwrite.h>

// Software-only Direct2D renderer. No D3D device is ever created, so the UI
// never touches the GPU driver.
class Gfx {
public:
    bool Init();
    bool RecreateTarget();
    void Shutdown();
    ID2D1Factory* Factory() { return factory.get(); }
    IDWriteFactory* DWrite() { return dwrite.get(); }
    ID2D1DCRenderTarget* RT() { return rt.get(); }
    bool Bind(HDC hdc, const RECT& rc);

private:
    Com<ID2D1Factory> factory;
    Com<IDWriteFactory> dwrite;
    Com<ID2D1DCRenderTarget> rt;
};

// A per-pixel-alpha child window backed by a 32-bit DIB, drawn with soft D2D
// and presented through UpdateLayeredWindow.
class LayeredWindow {
public:
    bool Create(HWND parent, const std::wstring& className, int id);
    void Destroy();
    void Resize(int w, int h);
    void Show(bool v);
    void Present();
    HWND Hwnd() const { return hwnd; }
    HDC Dc() const { return memDC; }
    int Width() const { return width; }
    int Height() const { return height; }

private:
    HWND hwnd = nullptr;
    HDC memDC = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ oldBmp = nullptr;
    void* bits = nullptr;
    int width = 0, height = 0;
    bool visible = false;
};
