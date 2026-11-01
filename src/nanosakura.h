#pragma once
#include "common.h"
#include <memory>

class NanoSakura {
public:
    NanoSakura();
    ~NanoSakura();
    void Load(const std::wstring& directory,const std::atomic<bool>& cancelled);
    std::wstring Translate(const std::wstring& text,const std::function<bool()>& obsolete);
    void Cancel();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
