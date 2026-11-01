#pragma once
#include "common.h"
#include <memory>

// The pinned NanoSakura tokenizer: NFC, GPT-2 ByteLevel and deterministic BPE.
// Other tokenizer pipelines are rejected rather than approximated.
class NanoSakuraTokenizer {
public:
    explicit NanoSakuraTokenizer(const std::wstring& path);
    ~NanoSakuraTokenizer();
    std::vector<int64_t> Encode(const std::wstring& text) const;
    std::wstring Decode(const std::vector<int64_t>& tokens) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
