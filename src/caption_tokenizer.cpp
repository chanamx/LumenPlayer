#include "caption_tokenizer.h"
#include "../third_party/nlohmann/json.hpp"
#define PCRE2_CODE_UNIT_WIDTH 8
#include "../third_party/mpv/deps-ucrt64/ucrt64/include/pcre2.h"
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <limits>
#include <stdexcept>

namespace {
std::wstring NFC(const std::wstring& text) {
    if(text.empty())return {};
    int n=NormalizeString(NormalizationC,text.data(),(int)text.size(),nullptr,0);
    if(!n)throw std::runtime_error("Invalid Unicode caption");
    std::wstring result((size_t)std::abs(n),L'\0');
    n=NormalizeString(NormalizationC,text.data(),(int)text.size(),result.data(),(int)result.size());
    if(n<=0)throw std::runtime_error("Cannot normalize caption");
    result.resize(n);return result;
}
uint64_t Pair(int64_t a,int64_t b){return (uint64_t(a)<<32)|uint32_t(b);}
}

struct NanoSakuraTokenizer::Impl {
    struct Merge {size_t rank;int64_t id;};
    std::vector<std::string> vocab,bytes;
    std::unordered_map<uint64_t,Merge> merges;
    std::unordered_map<std::string,int64_t> special;
    int64_t byteIds[256]{};
    pcre2_code* pattern=nullptr;
    ~Impl(){if(pattern)pcre2_code_free(pattern);}
    void EncodePlain(const std::wstring& input,std::vector<int64_t>& output) const {
        const auto text=WToUtf8(NFC(input));if(text.empty())return;
        std::unique_ptr<pcre2_match_data,decltype(&pcre2_match_data_free)> match(pcre2_match_data_create_from_pattern(pattern,nullptr),pcre2_match_data_free);
        if(!match)throw std::bad_alloc();
        for(size_t offset=0;offset<text.size();){
            if(pcre2_match(pattern,(PCRE2_SPTR)text.data(),text.size(),offset,PCRE2_ANCHORED,match.get(),nullptr)<0)
                throw std::runtime_error("Cannot split caption tokens");
            const auto* span=pcre2_get_ovector_pointer(match.get());
            if(span[0]!=offset||span[1]<=offset)throw std::runtime_error("Invalid token boundary");
            std::vector<int64_t> piece;
            for(size_t i=offset;i<span[1];++i)piece.push_back(byteIds[(unsigned char)text[i]]);
            for(;;){
                size_t best=piece.size(),rank=std::numeric_limits<size_t>::max();int64_t id=0;
                for(size_t i=0;i+1<piece.size();++i){const auto found=merges.find(Pair(piece[i],piece[i+1]));
                    if(found!=merges.end()&&found->second.rank<rank){best=i;rank=found->second.rank;id=found->second.id;}}
                if(best==piece.size())break;
                piece[best]=id;piece.erase(piece.begin()+best+1);
            }
            output.insert(output.end(),piece.begin(),piece.end());offset=span[1];
        }
    }
};

NanoSakuraTokenizer::NanoSakuraTokenizer(const std::wstring& path):impl_(new Impl) {
    auto& im=*impl_;std::ifstream file(std::filesystem::path(path),std::ios::binary);nlohmann::json j;file>>j;
    const auto& model=j.at("model");
    if(j.at("normalizer").at("type")!="NFC"||j.at("pre_tokenizer").at("type")!="ByteLevel"||
       j["pre_tokenizer"].at("add_prefix_space")!=false||j["pre_tokenizer"].at("use_regex")!=true||
       !j.at("post_processor").is_null()||j.at("decoder").at("type")!="ByteLevel"||
       !j.at("truncation").is_null()||!j.at("padding").is_null()||model.at("type")!="BPE"||
       !model.at("dropout").is_null()||model.at("byte_fallback")!=false||model.at("ignore_merges")!=false||
       !model.at("continuing_subword_prefix").is_null()||!model.at("end_of_word_suffix").is_null())
        throw std::runtime_error("Unsupported NanoSakura tokenizer pipeline");
    const auto ids=model.at("vocab").get<std::unordered_map<std::string,int64_t>>();
    if(ids.size()!=65536)throw std::runtime_error("Unexpected NanoSakura vocabulary");
    im.vocab.resize(ids.size());im.bytes.resize(ids.size());
    std::unordered_map<wchar_t,unsigned char> alphabet;
    wchar_t extra=256;
    for(int b=0;b<256;++b){const bool plain=(b>=33&&b<=126)||(b>=161&&b<=172)||(b>=174);
        const wchar_t symbol=plain?(wchar_t)b:extra++;alphabet[symbol]=(unsigned char)b;
        im.byteIds[b]=ids.at(WToUtf8(std::wstring(1,symbol)));
    }
    for(const auto& entry:ids){
        if(entry.second<0||(size_t)entry.second>=im.vocab.size())throw std::runtime_error("Invalid token id");
        im.vocab[entry.second]=entry.first;
        for(wchar_t c:Utf8ToW(entry.first)){const auto byte=alphabet.find(c);
            if(byte==alphabet.end())throw std::runtime_error("Invalid byte-level vocabulary");
            im.bytes[entry.second]+=(char)byte->second;
        }
    }
    size_t rank=0;im.merges.reserve(model.at("merges").size());
    for(const auto& m:model.at("merges")){
        if(!m.is_array()||m.size()!=2)throw std::runtime_error("Unexpected BPE merge format");
        const auto a=m[0].get<std::string>(),b=m[1].get<std::string>();
        im.merges.emplace(Pair(ids.at(a),ids.at(b)),Impl::Merge{rank++,ids.at(a+b)});
    }
    for(const auto& token:j.at("added_tokens")){
        if(token.at("special")!=true||token.at("normalized")!=false||token.at("single_word")!=false||token.at("lstrip")!=false||token.at("rstrip")!=false)
            throw std::runtime_error("Unsupported added token");
        im.special.emplace(token.at("content").get<std::string>(),token.at("id").get<int64_t>());
    }
    if(im.special.size()!=6||im.special.at("<eos>")!=2)throw std::runtime_error("Unexpected special tokens");
    // Matches the upstream ByteLevel regex, with Unicode properties enabled.
    const char* regex=R"('s|'t|'re|'ve|'m|'ll|'d| ?\p{L}+| ?\p{N}+| ?[^\s\p{L}\p{N}]+|\s+(?!\S)|\s+)";
    int error=0;PCRE2_SIZE position=0;
    im.pattern=pcre2_compile((PCRE2_SPTR)regex,PCRE2_ZERO_TERMINATED,PCRE2_UTF|PCRE2_UCP,&error,&position,nullptr);
    if(!im.pattern)throw std::runtime_error("Cannot initialize byte-level tokenizer");
}
NanoSakuraTokenizer::~NanoSakuraTokenizer()=default;
std::vector<int64_t> NanoSakuraTokenizer::Encode(const std::wstring& text) const {
    if(text.size()>4096)throw std::runtime_error("Caption exceeds tokenizer limit");
    const auto& im=*impl_;std::vector<int64_t> result;
    size_t offset=0;
    while(offset<text.size()){
        size_t next=text.size();int64_t id=-1;size_t length=0;
        for(const auto& token:im.special){const auto word=Utf8ToW(token.first);const auto found=text.find(word,offset);
            if(found<next){next=found;length=word.size();id=token.second;}}
        im.EncodePlain(text.substr(offset,next-offset),result);
        if(id<0)break;result.push_back(id);offset=next+length;
    }
    return result;
}
std::wstring NanoSakuraTokenizer::Decode(const std::vector<int64_t>& tokens) const {
    const auto& im=*impl_;std::string out;
    for(auto id:tokens){if(id<0||(size_t)id>=im.bytes.size())throw std::runtime_error("Invalid generated token");if(id>=6)out+=im.bytes[id];}
    return Utf8ToW(out);
}
