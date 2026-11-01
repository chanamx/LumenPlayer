#include "caption_models.h"
#include "../third_party/nlohmann/json.hpp"
#include <winhttp.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#define BZ_NO_STDIO
#include "../third_party/mpv/deps-ucrt64/ucrt64/include/bzlib.h"

namespace fs=std::filesystem;
using nlohmann::json;
#include "caption_catalog.inc"

#ifdef LUMEN_TEST
std::atomic<uint64_t> g_captionTestFreeBytes{UINT64_MAX};
#endif
namespace {
struct Http { HINTERNET h=nullptr; ~Http(){if(h)WinHttpCloseHandle(h);} };
struct File { HANDLE h=INVALID_HANDLE_VALUE; ~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);} };
struct Finally { std::function<void()> fn; ~Finally(){fn();} };
bool DecodeBzip2(const std::wstring& input,const std::wstring& output,uint64_t limit,const std::atomic<bool>& cancelled,std::wstring& error){
    File from{CreateFileW(input.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    File to{CreateFileW(output.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if(from.h==INVALID_HANDLE_VALUE||to.h==INVALID_HANDLE_VALUE){error=L"无法准备压缩包解码文件。";return false;}
    bz_stream stream{};if(BZ2_bzDecompressInit(&stream,0,0)!=BZ_OK){error=L"无法初始化压缩包解码器。";return false;}
    Finally cleanup{[&]{BZ2_bzDecompressEnd(&stream);}};
    char in[65536],out[65536];uint64_t total=0;const double deadline=QpcNow()+120;
    while(!cancelled.load()&&QpcNow()<deadline){
        if(!stream.avail_in){DWORD n=0;if(!ReadFile(from.h,in,sizeof(in),&n,nullptr)||!n){error=L"压缩包内容不完整。";return false;}stream.next_in=in;stream.avail_in=n;}
        stream.next_out=out;stream.avail_out=sizeof(out);const int result=BZ2_bzDecompress(&stream);const DWORD count=sizeof(out)-stream.avail_out;
        if(result!=BZ_OK&&result!=BZ_STREAM_END){error=L"压缩包解码校验失败。";return false;}
        if(total+count>limit){error=L"压缩包解码大小超出预期。";return false;}
        DWORD wrote=0;if(count&&(!WriteFile(to.h,out,count,&wrote,nullptr)||wrote!=count)){error=L"无法保存解码后的模型文件。";return false;}total+=count;
        if(result==BZ_STREAM_END)return FlushFileBuffers(to.h)!=FALSE;
    }
    error=cancelled.load()?L"已取消":L"压缩包解码超时";return false;
}
std::wstring SourceHost(const URL_COMPONENTS& url){
    std::wstring host(url.lpszHostName,url.dwHostNameLength);
    std::transform(host.begin(),host.end(),host.begin(),towlower);
    if(host.compare(0,4,L"www.")==0)host.erase(0,4);
    return host;
}
bool PlainFile(const std::wstring& p,uint64_t bytes) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    return GetFileAttributesExW(p.c_str(),GetFileExInfoStandard,&d) &&
        !(d.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
        ((uint64_t(d.nFileSizeHigh)<<32)|d.nFileSizeLow)==bytes;
}
std::string ReadSmall(const fs::path& path) {
    std::ifstream in(path,std::ios::binary); if(!in)return {};
    std::string s(16385,'\0');in.read(s.data(),s.size());s.resize((size_t)in.gcount());
    return s.size()<=16384?s:std::string();
}
bool WriteSmall(const fs::path& path,const std::string& s) {
    File f{CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
    DWORD n=0;return f.h!=INVALID_HANDLE_VALUE && WriteFile(f.h,s.data(),(DWORD)s.size(),&n,nullptr) && n==s.size() && FlushFileBuffers(f.h);
}
std::vector<CaptionAsset> AllFiles(const CaptionModel& m) {
    auto out=m.files;for(const auto& a:m.archives)for(const auto& member:a.members)out.push_back(member.file);return out;
}
std::string Stamp(const CaptionModel& m) {
    std::string s="LumenPlayerModel/1\n"+m.id+"\n";
    for(const auto& a:AllFiles(m))s+=a.sha256+"\n";
    return s;
}
CaptionAsset ReadAsset(const json& j) {
    CaptionAsset a; a.path=Utf8ToW(j.at("path").get<std::string>());a.bytes=j.at("bytes").get<uint64_t>();a.sha256=j.at("sha256").get<std::string>();
    if(j.contains("sources"))for(const auto& s:j["sources"])a.sources.push_back({s.at("provider").get<std::string>(),Utf8ToW(s.at("url").get<std::string>())});
    if(a.sha256.size()!=64 || a.bytes==0)throw std::runtime_error("Invalid fixed model identity");
    return a;
}
CaptionArchive ReadArchive(const json& j) {
    CaptionArchive a;a.download=ReadAsset(j.at("download"));
    for(const auto& m:j.at("members"))a.members.push_back({ReadAsset(m),Utf8ToW(m.at("member").get<std::string>())});
    return a;
}
bool FastFiles(const std::wstring& root,const std::vector<CaptionAsset>& assets) {
    if(assets.empty())return false;
    for(const auto& f:assets){std::wstring p;if(!CaptionModelStore::SafePath(root,f.path,p)||!PlainFile(p,f.bytes))return false;}
    return true;
}
bool CheckedFiles(const std::wstring& root,const std::vector<CaptionAsset>& assets,const std::atomic<bool>* cancel) {
    if(!FastFiles(root,assets))return false;
    for(const auto& f:assets){std::wstring p;if(!CaptionModelStore::SafePath(root,f.path,p)||CaptionFileSHA256(p,cancel)!=f.sha256)return false;}
    return true;
}
bool SecureURL(const URL_COMPONENTS& u) {
    if(u.dwUserNameLength || u.dwPasswordLength)return false;
    if(u.nScheme==INTERNET_SCHEME_HTTPS)return true;
#ifdef LUMEN_TEST
    const std::wstring host(u.lpszHostName,u.dwHostNameLength);
    return u.nScheme==INTERNET_SCHEME_HTTP && (host==L"127.0.0.1" || host==L"localhost");
#else
    return false;
#endif
}
uint64_t AddBytes(uint64_t a,uint64_t b){return b>UINT64_MAX-a?UINT64_MAX:a+b;}
uint64_t InstallSpace(const CaptionModel& model,const std::wstring& work,bool archiveOnly,bool fallback,const std::atomic<bool>& cancelled){
    const auto remaining=[&](const CaptionAsset& asset){
        std::wstring path;if(!CaptionModelStore::SafePath(work,asset.path,path))throw std::runtime_error("Unsafe download budget path");
        if(PlainFile(path,asset.bytes)&&CaptionFileSHA256(path,&cancelled)==asset.sha256)return uint64_t(0);
        if(!CaptionModelStore::SafePath(work,asset.path+L".part",path))throw std::runtime_error("Unsafe partial budget path");
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info) &&
            !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_SPARSE_FILE|FILE_ATTRIBUTE_COMPRESSED))){
            const uint64_t bytes=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
            if(bytes<=asset.bytes)return asset.bytes-bytes;
        }
        return asset.bytes;
    };
    uint64_t required=64ull*1024*1024,decoded=0;
    const auto archive=[&](const CaptionArchive& asset){
        required=AddBytes(required,remaining(asset.download));uint64_t extracted=0;
        for(const auto& member:asset.members)extracted=AddBytes(extracted,member.file.bytes);
        required=AddBytes(required,extracted);
        if(fs::path(asset.download.path).extension()==L".bz2")decoded=std::max(decoded,AddBytes(extracted,16ull*1024*1024));
    };
    if(archiveOnly)archive(model.fallback);
    else{
        for(const auto& asset:model.files)required=AddBytes(required,remaining(asset));
        for(const auto& asset:model.archives)archive(asset);
        if(fallback&&!model.fallback.members.empty())archive(model.fallback);
    }
    // Existing live/previous model bytes are already reflected in free space.
    return AddBytes(required,decoded);
}
std::vector<std::wstring> DownloadCaches(const CaptionModel& model){
    std::vector<std::wstring> paths={L".lumen-download"};
    for(const auto& file:AllFiles(model))paths.push_back(file.path+L".part");
    const auto archive=[&](const CaptionArchive& a){paths.push_back(a.download.path);paths.push_back(a.download.path+L".part");paths.push_back(a.download.path+L".decoded.tar");for(const auto& member:a.members)paths.push_back(L".extract/"+member.member);};
    for(const auto& a:model.archives)archive(a);
    if(!model.fallback.members.empty())archive(model.fallback);
    return paths;
}
bool CleanDownloadCaches(const CaptionModel& model,const std::wstring& directory){
    bool clean=true;std::set<fs::path> dirs;
    for(const auto& relative:DownloadCaches(model)){
        std::wstring path;if(!CaptionModelStore::SafePath(directory,relative,path)){clean=false;continue;}
        const auto attrs=GetFileAttributesW(path.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES && GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND)clean=false;
        if(attrs!=INVALID_FILE_ATTRIBUTES && ((attrs&FILE_ATTRIBUTE_DIRECTORY)||!DeleteFileW(path.c_str())))clean=false;
        for(auto parent=fs::path(path).parent_path();parent!=fs::path(directory)&&!parent.empty();parent=parent.parent_path())dirs.insert(parent);
    }
    // Only remove empty, catalog-derived directories; preserve unknown files.
    for(auto it=dirs.rbegin();it!=dirs.rend();++it)RemoveDirectoryW(it->c_str());
    return clean;
}
void RemoveKnown(const CaptionModel& model,const std::wstring& directory,bool legacy) {
    auto files=legacy?model.legacyFiles:AllFiles(model);
    std::set<fs::path> dirs;
    for(const auto& f:files){std::wstring p;if(!CaptionModelStore::SafePath(directory,f.path,p))throw std::runtime_error("Unsafe model path");
        const DWORD attrs=GetFileAttributesW(p.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_DIRECTORY) && !DeleteFileW(p.c_str()))throw std::runtime_error("Model file is in use");
        for(fs::path parent=fs::path(p).parent_path();parent!=fs::path(directory)&&!parent.empty();parent=parent.parent_path())dirs.insert(parent);
    }
    if(!legacy)for(const auto* name:{L".lumen-installed",L".lumen-download"})DeleteFileW(PathJoin(directory,name).c_str());
    if(!legacy){
        for(const auto& relative:DownloadCaches(model)){std::wstring p;if(!CaptionModelStore::SafePath(directory,relative,p))throw std::runtime_error("Unsafe cache path");DeleteFileW(p.c_str());}
        std::error_code e;
        for(fs::recursive_directory_iterator it(directory,fs::directory_options::skip_permission_denied,e),end;it!=end&&!e;it.increment(e)){
            const DWORD a=GetFileAttributesW(it->path().c_str());if(a!=INVALID_FILE_ATTRIBUTES && a&FILE_ATTRIBUTE_REPARSE_POINT){it.disable_recursion_pending();continue;}
            if(it->is_directory(e))dirs.insert(it->path());
        }
    }
    for(auto it=dirs.rbegin();it!=dirs.rend();++it){std::error_code e;fs::remove(*it,e);}
    std::error_code e;fs::remove(fs::path(directory),e); // Only removes an empty directory; unknown user files remain.
}
}

uint64_t CaptionModel::DownloadBytes() const {uint64_t n=0;for(const auto& f:files)n+=f.bytes;for(const auto& a:archives)n+=a.download.bytes;return n;}
const std::vector<CaptionModel>& CaptionCatalog() {
    static const auto models=[] {
        std::vector<CaptionModel> out;
        const auto catalog=json::parse(kCaptionCatalog);
        for(const auto& j:catalog.at("models")){
            CaptionModel m;m.id=j.at("id");m.language=j.at("language");m.task=j.at("task");m.engine=j.at("engine");
            m.name=Utf8ToW(j.at("name"));m.description=Utf8ToW(j.at("description"));m.tier=Utf8ToW(j.at("tier"));m.license=Utf8ToW(j.at("license"));m.homepage=Utf8ToW(j.at("homepage"));
            m.architecture=j.value("architecture",0);m.interval=j.value("interval",.5);m.providers=j.at("providers").get<std::vector<std::string>>();
            m.targetLanguages=j.value("targetLanguages",std::vector<std::string>{});
            for(const auto& f:j.at("files"))m.files.push_back(ReadAsset(f));for(const auto& a:j.at("archives"))m.archives.push_back(ReadArchive(a));
            if(j.contains("archiveFallback"))m.fallback=ReadArchive(j["archiveFallback"]);
            if(j.contains("legacyDirectory")){m.legacyDirectory=Utf8ToW(j["legacyDirectory"]);for(const auto& f:j["legacyFiles"])m.legacyFiles.push_back(ReadAsset(f));}
            if(m.id.empty()||m.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos)throw std::runtime_error("Invalid model id");
            out.push_back(std::move(m));
        }return out;
    }();return models;
}
const CaptionModel* FindCaptionModel(const std::string& id){for(const auto& m:CaptionCatalog())if(m.id==id)return &m;return nullptr;}
std::wstring CaptionProviderName(const std::string& p){if(p=="auto")return L"自动选择 · 失败换源";if(p=="huggingface")return L"Hugging Face";if(p=="hf-mirror")return L"HF 镜像 · 社区";if(p=="modelscope")return L"魔搭社区";if(p=="github")return L"GitHub 官方发布";if(p=="github-api")return L"GitHub API 备用入口";if(p=="moonshine")return L"Moonshine 官方";return Utf8ToW(p);}
std::wstring CaptionLanguage(const std::string& p){if(p=="zh")return L"中文";if(p=="en")return L"英语";if(p=="ja")return L"日语";if(p=="de")return L"德语";if(p=="fr")return L"法语";return L"多语种";}
std::wstring CaptionSize(uint64_t n){wchar_t s[64];if(n>=1024ull*1024*1024)swprintf(s,64,L"%.2f GiB",double(n)/(1024*1024*1024));else swprintf(s,64,L"%.1f MiB",double(n)/(1024*1024));return s;}
std::wstring QuoteProcessArgument(const std::wstring& s){std::wstring out=L"\"";size_t slashes=0;for(wchar_t c:s){if(c==L'\\'){++slashes;continue;}out.append(slashes*(c==L'\"'?2:1),L'\\');slashes=0;if(c==L'\"')out+=L'\\';out+=c;}out.append(slashes*2,L'\\');return out+L'\"';}
std::string CaptionFileSHA256(const std::wstring& path,const std::atomic<bool>* cancel) {
    File f{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    if(f.h==INVALID_HANDLE_VALUE)return {};
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(f.h,&info)||info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return {};
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD n=0,size=0;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    Finally cleanup{[&]{if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);}};
    if(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&size,sizeof(size),&n,0)<0)return {};
    std::vector<unsigned char> object(size),buffer(256*1024);unsigned char digest[32]{};
    if(BCryptCreateHash(alg,&hash,object.data(),size,nullptr,0,0)<0)return {};
    for(;;){if(cancel && cancel->load())return {};if(!ReadFile(f.h,buffer.data(),(DWORD)buffer.size(),&n,nullptr))return {};if(!n)break;if(BCryptHashData(hash,buffer.data(),n,0)<0)return {};}
    if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0)return {};
    std::string result;const char* hex="0123456789abcdef";for(unsigned char c:digest){result+=hex[c>>4];result+=hex[c&15];}return result;
}

CaptionModelStore::CaptionModelStore(std::wstring applicationDirectory):app_(std::move(applicationDirectory)),root_(PathJoin(app_,L"models")){}
CaptionModelStore::~CaptionModelStore(){Cancel();Wait();}
bool CaptionModelStore::SafePath(const std::wstring& root,const std::wstring& relative,std::wstring& result) {
    if(relative.empty())return false;
    fs::path rel(relative);if(rel.is_absolute()||rel.has_root_path())return false;
    for(const auto& c:rel)if(c==L".."||c==L"."||c.empty()||c.wstring().find_first_of(L":\"<>|?*")!=std::wstring::npos)return false;
    fs::path current=fs::absolute(fs::path(root)).lexically_normal();
    DWORD a=GetFileAttributesW(current.c_str());if(a!=INVALID_FILE_ATTRIBUTES && a&FILE_ATTRIBUTE_REPARSE_POINT)return false;
    for(const auto& c:rel){current/=c;a=GetFileAttributesW(current.c_str());if(a!=INVALID_FILE_ATTRIBUTES && a&FILE_ATTRIBUTE_REPARSE_POINT)return false;}
    result=current.wstring();return true;
}
std::wstring CaptionModelStore::InstalledPath(const CaptionModel& m) const {
    std::wstring dir;if(!SafePath(root_,Utf8ToW(m.id),dir))return {};
    if(ReadSmall(fs::path(dir)/L".lumen-installed")==Stamp(m)&&FastFiles(dir,AllFiles(m)))return dir;
    if(!m.legacyDirectory.empty()&&SafePath(app_,m.legacyDirectory,dir)&&FastFiles(dir,{m.legacyFiles[0],m.legacyFiles[1]}))return dir;
    return {};
}
bool CaptionModelStore::VerifyInstalled(const CaptionModel& m,std::wstring& dir,std::wstring& error,const std::atomic<bool>* cancel) const {
    dir=InstalledPath(m);if(dir.empty()){error=L"模型尚未完整安装，请先安装或重新安装。";return false;}
    const bool legacy=!m.legacyDirectory.empty() && fs::path(dir).filename()==m.legacyDirectory;
    const auto files=legacy?std::vector<CaptionAsset>{m.legacyFiles[0],m.legacyFiles[1]}:AllFiles(m);
    if(!CheckedFiles(dir,files,cancel)){dir.clear();error=cancel&&cancel->load()?L"已取消":L"模型校验失败，请重新安装。";return false;}return true;
}
CaptionModelStore::Progress CaptionModelStore::Status() const {std::lock_guard<std::mutex> lock(statusMutex_);return progress_;}
void CaptionModelStore::Publish(std::wstring phase,std::wstring detail,uint64_t received){std::lock_guard<std::mutex> lock(statusMutex_);progress_.phase=std::move(phase);progress_.detail=std::move(detail);if(received!=UINT64_MAX)progress_.received=received;}
void CaptionModelStore::SetInUse(const std::string& id,bool used){std::lock_guard<std::mutex> lock(operationMutex_);if(used)inUse_.insert(id);else inUse_.erase(id);}
void CaptionModelStore::Cancel(){cancelled_.store(true);std::lock_guard<std::mutex> lock(httpMutex_);if(activeRequest_){WinHttpCloseHandle(activeRequest_);activeRequest_=nullptr;}}
void CaptionModelStore::Wait(){if(worker_.joinable())worker_.join();}
bool CaptionModelStore::Install(const CaptionModel& model,const std::string& provider,std::wstring& error){
    std::lock_guard<std::mutex> lock(operationMutex_);
    if(Status().busy){error=L"已有模型正在安装，请先等待或取消。";return false;}
    if(inUse_.count(model.id)){error=L"请先停止使用此模型。";return false;}
    if(std::find(model.providers.begin(),model.providers.end(),provider)==model.providers.end()){error=L"该模型不支持这个下载源。";return false;}
    if(worker_.joinable())worker_.join();cancelled_.store(false);unavailableHosts_.clear();
    {std::lock_guard<std::mutex> s(statusMutex_);progress_={true,model.id,L"准备下载",{},0,model.DownloadBytes()};}
    try{worker_=std::thread([this,model,provider]{RunInstall(model,provider);});}catch(...){std::lock_guard<std::mutex> s(statusMutex_);progress_.busy=false;progress_.phase=L"无法启动下载";error=L"无法启动下载任务。";return false;}return true;
}
bool CaptionModelStore::Download(const CaptionAsset& f,const std::wstring& destination,const std::string& provider,uint64_t base,std::wstring& error){
    if(PlainFile(destination,f.bytes)&&CaptionFileSHA256(destination,&cancelled_)==f.sha256){Publish(L"已校验",f.path,base+f.bytes);return true;}
    fs::create_directories(fs::path(destination).parent_path());const std::wstring part=destination+L".part";
    std::vector<CaptionSource> sources=f.sources;
    if(provider!="auto")std::stable_sort(sources.begin(),sources.end(),[&](const auto& a,const auto& b){return (a.provider==provider)>(b.provider==provider);});
    // Missing auxiliary files may use their sole fixed source (e.g. the Chinese word list).
    if(provider!="auto" && std::any_of(sources.begin(),sources.end(),[&](const auto& s){return s.provider==provider;}))
        sources.erase(std::remove_if(sources.begin(),sources.end(),[&](const auto& s){return s.provider!=provider;}),sources.end());
    for(const auto& source:sources){
        if(cancelled_.load()){error=L"已取消，已下载内容可在下次继续。";return false;}
        uint64_t offset=0;WIN32_FILE_ATTRIBUTE_DATA d{};
        if(GetFileAttributesExW(part.c_str(),GetFileExInfoStandard,&d)){
            if(d.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){error=L"下载目录中有无法安全处理的文件。";return false;}
            offset=(uint64_t(d.nFileSizeHigh)<<32)|d.nFileSizeLow;
            if(offset>f.bytes){error=L"临时文件大小异常，请在模型列表中删除未完成下载后重试。";return false;}
        }
        if(offset<f.bytes){
            Publish(L"正在下载",CaptionProviderName(source.provider)+L" · "+f.path,base+offset);
            URL_COMPONENTS u{};u.dwStructSize=sizeof(u);u.dwHostNameLength=u.dwUrlPathLength=u.dwExtraInfoLength=u.dwUserNameLength=u.dwPasswordLength=(DWORD)-1;
            if(!WinHttpCrackUrl(source.url.c_str(),0,0,&u)||!SecureURL(u)){error=L"下载地址无效或不安全。";continue;}
            if(unavailableHosts_.count(SourceHost(u))){error=L"该站点本次下载暂不可用，可稍后重试或选择其他来源。";continue;}
            std::wstring host(u.lpszHostName,u.dwHostNameLength),object(u.lpszUrlPath,u.dwUrlPathLength);if(u.dwExtraInfoLength)object.append(u.lpszExtraInfo,u.dwExtraInfoLength);
            Http session{WinHttpOpen(L"LumenPlayer/2.1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0)};
            if(!session.h){error=L"无法建立网络会话。";continue;}WinHttpSetTimeouts(session.h,5000,10000,10000,15000);
            Http connection{WinHttpConnect(session.h,host.c_str(),u.nPort,0)};
            if(!connection.h){error=L"连接下载源失败。";continue;}
            HINTERNET request=WinHttpOpenRequest(connection.h,L"GET",object.empty()?L"/":object.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,u.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0);
            if(!request){error=L"无法建立下载请求。";continue;}
            {std::lock_guard<std::mutex> lock(httpMutex_);activeRequest_=request;}
            Finally closeRequest{[&]{std::lock_guard<std::mutex> lock(httpMutex_);if(activeRequest_==request){activeRequest_=nullptr;WinHttpCloseHandle(request);}}};
            if(cancelled_.load()){error=L"已取消";return false;}
            DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
            std::wstring headers=L"Accept-Encoding: identity\r\n";if(source.provider=="github-api")headers+=L"Accept: application/octet-stream\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
            if(offset)headers+=L"Range: bytes="+std::to_wstring(offset)+L"-\r\n";
            if(!WinHttpSendRequest(request,headers.c_str(),(DWORD)-1,nullptr,0,0,0)||!WinHttpReceiveResponse(request,nullptr)){
                const auto code=GetLastError();
                error=cancelled_.load()?L"已取消，已下载内容可继续。":CaptionProviderName(source.provider)+L" 连接失败（"+std::to_wstring(code)+L"），可重试或更换来源。";continue;
            }
            DWORD status=0,size=sizeof(status);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&size,nullptr)){error=L"无法读取下载响应。";continue;}
            if(status==403||status==429||status==503){
                unavailableHosts_.insert(SourceHost(u));
                error=CaptionProviderName(source.provider)+L" 暂不可用（"+std::to_wstring(status)+L"），本次不再请求该站点。";
                Publish(L"正在更换下载源",error,base+offset);continue;
            }
            if(status!=200 && status!=206){error=L"下载源返回 "+std::to_wstring(status)+L"，可尝试其他来源。";continue;}
            if(offset && status==200)offset=0; // Range ignored: safely restart rather than concatenate a full response.
            if(status==206){wchar_t range[160]{};size=sizeof(range);const auto expected=L"bytes "+std::to_wstring(offset)+L"-"+std::to_wstring(f.bytes-1)+L"/"+std::to_wstring(f.bytes);
                if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_RANGE,nullptr,range,&size,nullptr)||range!=expected){error=L"断点响应不匹配，临时文件已保留，尝试其他来源。";continue;}}
            wchar_t finalURL[16384]{};size=sizeof(finalURL);if(!WinHttpQueryOption(request,WINHTTP_OPTION_URL,finalURL,&size)){error=L"无法核实最终下载地址。";return false;}
            URL_COMPONENTS final{};final.dwStructSize=sizeof(final);final.dwHostNameLength=final.dwUserNameLength=final.dwPasswordLength=(DWORD)-1;
            if(!WinHttpCrackUrl(finalURL,0,0,&final)||!SecureURL(final)){error=L"下载重定向不安全。";return false;}
            File file{CreateFileW(part.c_str(),GENERIC_WRITE,0,nullptr,offset?OPEN_EXISTING:CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
            if(file.h==INVALID_HANDLE_VALUE){error=L"无法写入下载目录。";return false;}
            LARGE_INTEGER pos{};pos.QuadPart=offset;if(!SetFilePointerEx(file.h,pos,nullptr,FILE_BEGIN)){error=L"无法继续临时文件。";return false;}
            unsigned char buffer[65536];bool ok=true;const double deadline=QpcNow()+4*60*60;
            while(!cancelled_.load() && QpcNow()<deadline){DWORD n=0,wrote=0;
                if(!WinHttpReadData(request,buffer,sizeof(buffer),&n)){ok=false;break;}if(!n)break;
                if(offset+n>f.bytes){ok=false;error=L"下载大小超出预期。";break;}
                if(!WriteFile(file.h,buffer,n,&wrote,nullptr)||wrote!=n){ok=false;error=L"磁盘写入失败。";break;}
                offset+=n;Publish(L"正在下载",CaptionProviderName(source.provider)+L" · "+f.path,base+offset);
            }
            if(!FlushFileBuffers(file.h))ok=false;
            if(!ok||cancelled_.load()||offset!=f.bytes){if(error.empty())error=L"下载未完成，已保留进度。";continue;}
        }
        Publish(L"正在校验",f.path,base+f.bytes);
        if(PlainFile(part,f.bytes)&&CaptionFileSHA256(part,&cancelled_)==f.sha256){
            if(MoveFileExW(part.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
            error=L"无法保存已校验模型，请检查文件是否正在使用。";return false;
        }
        if(cancelled_.load()){error=L"已取消，进度已保留。";return false;}
        DeleteFileW(part.c_str());error=L"文件完整性校验失败，模型未安装。";
    }return false;
}
bool CaptionModelStore::Extract(const CaptionArchive& archive,const std::wstring& work,std::wstring& error){
    std::wstring archivePath,extract;if(!SafePath(work,archive.download.path,archivePath)||!SafePath(work,L".extract",extract))return false;
    std::wstring decoded;Finally decodedCleanup{[&]{if(!decoded.empty())DeleteFileW(decoded.c_str());}};
    if(fs::path(archivePath).extension()==L".bz2"){
        if(!SafePath(work,archive.download.path+L".decoded.tar",decoded))return false;
        uint64_t limit=16ull*1024*1024;for(const auto& member:archive.members)limit+=member.file.bytes;
        Publish(L"正在解码压缩包",L"正在准备模型文件，可随时取消");
        if(!DecodeBzip2(archivePath,decoded,limit,cancelled_,error))return false;archivePath=decoded;
    }
    fs::create_directories(extract);wchar_t system[MAX_PATH]{};if(!GetSystemDirectoryW(system,MAX_PATH))return false;
    std::wstring executable=PathJoin(system,L"tar.exe"),command=QuoteProcessArgument(executable)+L" -xf "+QuoteProcessArgument(archivePath)+L" -C "+QuoteProcessArgument(extract);
    for(const auto& member:archive.members){std::wstring p;if(!SafePath(extract,member.member,p)){error=L"压缩包路径不安全。";return false;}command+=L" "+QuoteProcessArgument(member.member);}
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,work.c_str(),&si,&pi)){error=L"无法解压模型运行组件。";return false;}
    CloseHandle(pi.hThread);File process{pi.hProcess};const double deadline=QpcNow()+90;
    while(WaitForSingleObject(pi.hProcess,100)==WAIT_TIMEOUT)if(cancelled_.load()||QpcNow()>deadline){TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,5000);error=cancelled_.load()?L"已取消":L"解压超时";return false;}
    DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);if(code){error=L"模型运行组件解压失败。";return false;}
    for(const auto& member:archive.members){std::wstring from,to;
        if(!SafePath(extract,member.member,from)||!SafePath(work,member.file.path,to)||!PlainFile(from,member.file.bytes)||CaptionFileSHA256(from,&cancelled_)!=member.file.sha256){error=L"运行组件校验失败。";return false;}
        fs::create_directories(fs::path(to).parent_path());if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){error=L"无法安装运行组件。";return false;}
    }
    return true;
}
void CaptionModelStore::RunInstall(CaptionModel model,std::string provider){
    std::wstring error;bool ok=false,cacheClean=true;
    try{
        std::wstring work,live;if(!SafePath(root_,L".downloads/"+Utf8ToW(model.id),work)||!SafePath(root_,Utf8ToW(model.id),live))throw std::runtime_error("Unsafe store directory");
        fs::create_directories(root_);if(!SafePath(root_,L".downloads/"+Utf8ToW(model.id),work))throw std::runtime_error("Reparse model directory");
        if(fs::exists(work)&&ReadSmall(fs::path(work)/L".lumen-download")!=Stamp(model))throw std::runtime_error("Unowned temporary directory; preserved");
        const bool archiveOnly=!model.fallback.members.empty()&&provider!="auto"&&std::any_of(model.fallback.download.sources.begin(),model.fallback.download.sources.end(),[&](const auto& source){return source.provider==provider;});
        Publish(L"检查存储空间",L"核对剩余下载、解包及安全余量");
        const uint64_t required=InstallSpace(model,work,archiveOnly,provider=="auto",cancelled_);
        if(cancelled_.load()){error=L"已取消，下载进度已保留。";throw std::runtime_error("Cancelled during space preflight");}
        ULARGE_INTEGER available{};
        if(!GetDiskFreeSpaceExW((root_+L"\\").c_str(),&available,nullptr,nullptr)){error=L"无法检查安装位置的可用空间，请检查目录权限或磁盘连接。";throw std::runtime_error("Cannot query destination free space");}
#ifdef LUMEN_TEST
        available.QuadPart=std::min<uint64_t>(available.QuadPart,g_captionTestFreeBytes.load());
#endif
        if(available.QuadPart<required){error=L"安装位置空间不足：预计还需 "+CaptionSize(required)+L"（含解包与安全余量），当前可用 "+CaptionSize(available.QuadPart)+L"。已有模型和下载进度已保留。";throw std::runtime_error("Insufficient model installation headroom");}
        fs::create_directories(work);if(!WriteSmall(fs::path(work)/L".lumen-download",Stamp(model)))throw std::runtime_error("Cannot write model directory");
        uint64_t base=0;
        auto installArchive=[&](const CaptionArchive& a,const std::string& origin){std::wstring p;if(!SafePath(work,a.download.path,p))return false;
            if(!Download(a.download,p,origin,base,error))return false;base+=a.download.bytes;Publish(L"正在安装",L"校验并安装压缩包文件",base);return Extract(a,work,error);};
        bool downloaded=true;
        if(archiveOnly){
            {std::lock_guard<std::mutex> lock(statusMutex_);progress_.total=model.fallback.download.bytes;}
            downloaded=installArchive(model.fallback,provider);
        }else{
            for(const auto& f:model.files){std::wstring p;if(!SafePath(work,f.path,p)||!Download(f,p,provider,base,error)){downloaded=false;break;}base+=f.bytes;}
            if(!downloaded&&!cancelled_.load()&&provider=="auto"&&!model.fallback.members.empty()){
                base=0;{std::lock_guard<std::mutex> lock(statusMutex_);progress_.total=model.fallback.download.bytes;}
                downloaded=installArchive(model.fallback,"auto");
            }
            if(downloaded)for(const auto& a:model.archives)if(!installArchive(a,"auto")){downloaded=false;break;}
        }
        if(downloaded && !cancelled_.load()){
            Publish(L"正在校验",L"核对整个模型及辅助文件");
            if(!CheckedFiles(work,AllFiles(model),&cancelled_))error=L"模型或辅助文件不完整，未启用。";
            else{
                std::wstring backup;const bool exists=fs::exists(live);
                if(exists && ReadSmall(fs::path(live)/L".lumen-installed")!=Stamp(model))throw std::runtime_error("Existing unowned model directory; preserved");
                if(!WriteSmall(fs::path(work)/L".lumen-installed",Stamp(model)))throw std::runtime_error("Cannot write installation record");
                if(cancelled_.load())throw std::runtime_error("Cancelled before activation");
                if(exists){if(!SafePath(root_,L".previous-"+Utf8ToW(model.id)+L"-"+std::to_wstring(GetTickCount64()),backup)||!MoveFileExW(live.c_str(),backup.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot preserve previous model");}
                if(!MoveFileExW(work.c_str(),live.c_str(),MOVEFILE_WRITE_THROUGH)){if(exists)MoveFileExW(backup.c_str(),live.c_str(),MOVEFILE_WRITE_THROUGH);throw std::runtime_error("Cannot activate model");}
                cacheClean=CleanDownloadCaches(model,live);
                if(!cacheClean)LogMsg("Model installed; some owned download caches remain in use");
                if(exists)RemoveKnown(model,backup,false);
                ok=true;
            }
        }
    }catch(const std::exception& e){LogMsg("Model install failed: %s",e.what());if(error.empty())error=L"无法安装模型。请检查目录权限、剩余空间，或是否存在自有文件。";}
    {std::lock_guard<std::mutex> lock(statusMutex_);progress_.busy=false;progress_.phase=ok?L"已安装":cancelled_.load()?L"已取消":L"安装失败";progress_.detail=ok?(cacheClean?L"校验完成。可选择使用此模型。":L"模型已安装，部分临时文件仍被占用；删除模型时将一并清理。"):error;if(ok)progress_.received=progress_.total;}
}
bool CaptionModelStore::Remove(const CaptionModel& m,std::wstring& error){
    std::lock_guard<std::mutex> lock(operationMutex_);if(Status().busy||inUse_.count(m.id)){error=L"请先停止使用或完成当前下载。";return false;}
    try{
        std::wstring dir;if(!SafePath(root_,Utf8ToW(m.id),dir)){error=L"模型目录包含链接，未删除。";return false;}
        if(fs::exists(dir)){
            if(ReadSmall(fs::path(dir)/L".lumen-installed")!=Stamp(m)){error=L"该目录未由应用登记，文件已保留。";return false;}
            RemoveKnown(m,dir,false);
        }
        if(!m.legacyDirectory.empty()){
            if(!SafePath(app_,m.legacyDirectory,dir)){error=L"旧模型目录包含链接，未删除。";return false;}
            if(fs::exists(dir))RemoveKnown(m,dir,true);
        }
        if(!SafePath(root_,L".downloads/"+Utf8ToW(m.id),dir)){error=L"临时目录包含链接，未删除。";return false;}
        if(fs::exists(dir)){
            if(ReadSmall(fs::path(dir)/L".lumen-download")!=Stamp(m)){error=L"未登记的临时文件已保留。";return false;}
            RemoveKnown(m,dir,false);
        }
        return true;
    }catch(const std::exception& e){LogMsg("Model removal failed: %s",e.what());error=L"部分模型文件正在使用，删除未完成。";return false;}
}
