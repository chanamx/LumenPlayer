#pragma once
#include "common.h"
#include <set>

struct CaptionSource { std::string provider; std::wstring url; };
struct CaptionAsset {
    std::wstring path;
    uint64_t bytes=0;
    std::string sha256;
    std::vector<CaptionSource> sources;
};
struct CaptionMember { CaptionAsset file; std::wstring member; };
struct CaptionArchive { CaptionAsset download; std::vector<CaptionMember> members; };
struct CaptionModel {
    std::string id, language, task, engine;
    std::wstring name, description, tier, license, homepage;
    std::vector<std::string> providers;
    std::vector<std::string> targetLanguages;
    std::vector<CaptionAsset> files, legacyFiles;
    std::vector<CaptionArchive> archives;
    CaptionArchive fallback;
    std::wstring legacyDirectory;
    int architecture=0;
    double interval=.5;
    bool Translation() const { return task=="translation"; }
    bool SupportsTranslation(const std::string& source,const std::string& target) const {
        return Translation()&&source!=target&&(language=="multi"||language==source)&&
            (targetLanguages.empty()||std::find(targetLanguages.begin(),targetLanguages.end(),target)!=targetLanguages.end());
    }
    uint64_t DownloadBytes() const;
};
const std::vector<CaptionModel>& CaptionCatalog();
const CaptionModel* FindCaptionModel(const std::string& id);
std::wstring CaptionProviderName(const std::string& provider);
std::wstring CaptionSize(uint64_t bytes);
std::wstring CaptionLanguage(const std::string& language);
std::string CaptionFileSHA256(const std::wstring& path, const std::atomic<bool>* cancel=nullptr);
std::wstring QuoteProcessArgument(const std::wstring& argument);

class CaptionModelStore {
public:
    struct Progress {
        bool busy=false;
        std::string model;
        std::wstring phase, detail;
        uint64_t received=0,total=0;
    };
    explicit CaptionModelStore(std::wstring applicationDirectory=ExeDir());
    ~CaptionModelStore();
    CaptionModelStore(const CaptionModelStore&)=delete;
    CaptionModelStore& operator=(const CaptionModelStore&)=delete;
    const std::wstring& Root() const { return root_; }
    std::wstring InstalledPath(const CaptionModel& model) const;
    bool VerifyInstalled(const CaptionModel& model, std::wstring& directory, std::wstring& error,
                         const std::atomic<bool>* cancel=nullptr) const;
    bool Install(const CaptionModel& model,const std::string& provider,std::wstring& error);
    bool Remove(const CaptionModel& model,std::wstring& error);
    void SetInUse(const std::string& id,bool inUse);
    void Cancel();
    void Wait();
    Progress Status() const;
    // Only relative catalog paths within the store, with no reparse components.
    static bool SafePath(const std::wstring& root,const std::wstring& relative,std::wstring& result);
private:
    bool Download(const CaptionAsset& asset,const std::wstring& destination,
                  const std::string& provider,uint64_t base,std::wstring& error);
    bool Extract(const CaptionArchive& archive,const std::wstring& work,std::wstring& error);
    void RunInstall(CaptionModel model,std::string provider);
    void Publish(std::wstring phase,std::wstring detail={},uint64_t received=UINT64_MAX);
    std::wstring app_,root_;
    mutable std::mutex statusMutex_, operationMutex_;
    std::mutex httpMutex_;
    void* activeRequest_=nullptr;
    Progress progress_;
    std::set<std::string> inUse_;
    std::set<std::wstring> unavailableHosts_;
    std::atomic<bool> cancelled_{false};
    std::thread worker_;
};
