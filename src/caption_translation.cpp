#include "caption_translation.h"
#include "nanosakura.h"
#include "../third_party/nlohmann/json.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <condition_variable>
#include <filesystem>

using nlohmann::json;
namespace {
struct Handle { HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);} };
struct Http {HINTERNET h=nullptr;~Http(){if(h)WinHttpCloseHandle(h);} };
std::string LanguageName(const std::string& code){if(code=="zh")return "Chinese";if(code=="ja")return "Japanese";if(code=="de")return "German";if(code=="fr")return "French";return "English";}
int ReservePort(){WSADATA w{};if(WSAStartup(MAKEWORD(2,2),&w))return 0;SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);int port=0;if(s!=INVALID_SOCKET){sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=0;if(!bind(s,(sockaddr*)&a,sizeof(a))){int n=sizeof(a);if(!getsockname(s,(sockaddr*)&a,&n))port=ntohs(a.sin_port);}closesocket(s);}WSACleanup();return port;}
std::string RandomKey(){unsigned char bytes[24]{};if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return {};std::string out;for(unsigned char c:bytes){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;}
bool Request(int port,const std::string& key,const wchar_t* path,const std::string* body,std::string& response,DWORD& status){
    Http session{WinHttpOpen(L"LumenPlayer/2.1",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,0)};if(!session.h)return false;
    WinHttpSetTimeouts(session.h,1000,1000,3000,12000);
    Http connection{WinHttpConnect(session.h,L"127.0.0.1",(INTERNET_PORT)port,0)};if(!connection.h)return false;
    Http request{WinHttpOpenRequest(connection.h,body?L"POST":L"GET",path,nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0)};if(!request.h)return false;
    const auto headers=L"Content-Type: application/json\r\nAuthorization: Bearer "+Utf8ToW(key)+L"\r\n";
    if(!WinHttpSendRequest(request.h,headers.c_str(),(DWORD)-1,body?(void*)body->data():nullptr,body?(DWORD)body->size():0,body?(DWORD)body->size():0,0)||!WinHttpReceiveResponse(request.h,nullptr))return false;
    DWORD count=sizeof(status);if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&count,nullptr))return false;
    char buffer[8192];for(;;){DWORD n=0;if(!WinHttpReadData(request.h,buffer,sizeof(buffer),&n))return false;if(!n)return true;if(response.size()+n>65536)return false;response.append(buffer,n);}
}
}

struct CaptionTranslation::Impl {
    std::atomic<bool> active{false},cancel{false};
    std::thread worker;
    std::mutex mutex,processMutex;
    std::condition_variable changed;
    std::deque<Result> queue;
    std::vector<Result> completed;
    uint64_t generation=0;
    std::wstring status;
    HANDLE process=nullptr,job=nullptr;
    NanoSakura* nano=nullptr; // Owned on the worker; protected until its teardown.
    void SetStatus(std::wstring value){std::lock_guard<std::mutex> lock(mutex);status=std::move(value);}
    void Kill(){std::lock_guard<std::mutex> lock(processMutex);if(nano)nano->Cancel();if(job)TerminateJobObject(job,0);else if(process)TerminateProcess(process,0);}
    void RunNano(const CaptionModel& model,const std::wstring& directory){
        NanoSakura engine;
        {std::lock_guard<std::mutex> lock(processMutex);nano=&engine;}
        try{
            SetStatus(L"正在校验日中翻译包 · 识别原文继续显示");
            for(const auto& asset:model.files){std::wstring path;
                if(!CaptionModelStore::SafePath(directory,asset.path,path)||CaptionFileSHA256(path,&cancel)!=asset.sha256)
                    throw std::runtime_error("NanoSakura package verification failed");}
            MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
            if(!GlobalMemoryStatusEx(&memory)||memory.ullAvailPageFile<1024ull*1024*1024)throw std::runtime_error("Not enough available commit for NanoSakura");
            if(!cancel.load()){SetStatus(L"日中翻译准备中 · 先显示原文");engine.Load(directory,cancel);}
            if(!cancel.load())SetStatus(L"日中翻译已就绪 · 句末后生成译文");
            while(!cancel.load()){
                Result item;
                {std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[&]{return cancel.load()||!queue.empty();});
                    if(cancel.load())break;item=std::move(queue.front());queue.pop_front();if(item.generation!=generation)continue;}
                if(item.source.text.size()>1600){SetStatus(L"句子过长 · 此句保留原文");continue;}
                item.translation=engine.Translate(item.source.text,[&]{if(cancel.load())return true;std::lock_guard<std::mutex> lock(mutex);return item.generation!=generation;});
                std::lock_guard<std::mutex> lock(mutex);
                if(cancel.load()||item.generation!=generation)continue;
                if(item.translation.empty()){status=L"本句翻译未完成 · 保留原文";continue;}
                completed.push_back(std::move(item));if(completed.size()>16)completed.erase(completed.begin());status=L"日中翻译已就绪";
            }
        }catch(const std::exception& error){if(!cancel.load()){LogMsg("NanoSakura stopped: %s",error.what());SetStatus(L"日中翻译暂不可用 · 识别原文继续显示");}}
        {std::lock_guard<std::mutex> lock(processMutex);nano=nullptr;}
        active.store(false);
    }
    void Run(CaptionModel model,std::wstring directory,std::string source,std::string target){
        if(model.engine=="nanosakura"){RunNano(model,directory);return;}
        const int port=ReservePort();const std::string key=RandomKey();
        HANDLE localProcess=nullptr,localJob=nullptr;
        auto finish=[&]{
            std::lock_guard<std::mutex> lock(processMutex);
            if(localJob){TerminateJobObject(localJob,0);CloseHandle(localJob);}
            if(localProcess)TerminateProcess(localProcess,0);
            if(localProcess){WaitForSingleObject(localProcess,5000);CloseHandle(localProcess);}process=job=nullptr;
            active.store(false);
        };
        try{
            SetStatus(L"正在校验翻译模型 · 识别原文继续显示");
            const auto verify=[&](const CaptionAsset& asset){
                std::wstring path;
                if(!CaptionModelStore::SafePath(directory,asset.path,path) ||
                    CaptionFileSHA256(path,&cancel)!=asset.sha256)throw std::runtime_error("Translation model verification failed");
            };
            for(const auto& asset:model.files)verify(asset);
            for(const auto& archive:model.archives)for(const auto& member:archive.members)verify(member.file);
            if(cancel.load())throw std::runtime_error("Cancelled");
            MEMORYSTATUSEX mem{};mem.dwLength=sizeof(mem);
            if(!GlobalMemoryStatusEx(&mem)||mem.ullAvailPageFile<2ull*1024*1024*1024)throw std::runtime_error("Not enough available commit for translation");
            if(!port||key.empty())throw std::runtime_error("Cannot create local translation service");
            SetStatus(L"翻译模型准备中 · 先显示原文");
            const auto exe=PathJoin(directory,L"engine/llama-server.exe");
            std::wstring command=QuoteProcessArgument(exe)+L" -m "+QuoteProcessArgument(PathJoin(directory,L"model.gguf"))+
                L" --host 127.0.0.1 --port "+std::to_wstring(port)+L" --api-key "+Utf8ToW(key)+
                L" -t 1 -tb 1 -c 1024 -b 128 -ub 128 -np 1 -ngl 0 --no-op-offload --threads-http 2 --no-repack --cache-ram 0 --ctx-checkpoints 0 --no-cache-prompt --no-cache-idle-slots";
            if(model.id=="index-q4")command+=L" --reasoning off";
            std::filesystem::create_directories(PathJoin(ExeDir(),L"cache"));
            SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
            Handle log{CreateFileW(PathJoin(ExeDir(),L"cache/caption-translation.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
            Handle input{CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
            if(log.h==INVALID_HANDLE_VALUE||input.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open service log");
            STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;si.wShowWindow=SW_HIDE;si.hStdOutput=si.hStdError=log.h;si.hStdInput=input.h;
            PROCESS_INFORMATION pi{};
            localJob=CreateJobObjectW(nullptr,nullptr);if(!localJob)throw std::runtime_error("Cannot create translation job");
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;limits.ProcessMemoryLimit=3ull*1024*1024*1024;
            if(!SetInformationJobObject(localJob,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Cannot limit translation process");
            if(cancel.load())throw std::runtime_error("Cancelled");
            if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,directory.c_str(),&si,&pi))throw std::runtime_error("Cannot start verified translation runtime");
            localProcess=pi.hProcess;Handle thread{pi.hThread};
            if(!AssignProcessToJobObject(localJob,localProcess))throw std::runtime_error("Cannot own translation process");
            {std::lock_guard<std::mutex> lock(processMutex);process=localProcess;job=localJob;}
            if(cancel.load())throw std::runtime_error("Cancelled");
            if(ResumeThread(pi.hThread)==(DWORD)-1)throw std::runtime_error("Cannot resume translation runtime");
            const double deadline=QpcNow()+90;
            for(;;){
                if(cancel.load())throw std::runtime_error("Cancelled");
                if(WaitForSingleObject(localProcess,0)!=WAIT_TIMEOUT)throw std::runtime_error("Translation runtime exited");
                std::string response;DWORD code=0;
                if(Request(port,key,L"/health",nullptr,response,code)&&code==200)break;
                if(QpcNow()>deadline)throw std::runtime_error("Translation initialization timeout");
                std::unique_lock<std::mutex> lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(200),[&]{return cancel.load();});
            }
            SetStatus(L"翻译已就绪 · 句末后生成译文");
            for(;;){Result item;
                {std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[&]{return cancel.load()||!queue.empty();});if(cancel.load())break;item=std::move(queue.front());queue.pop_front();if(item.generation!=generation)continue;}
                if(item.source.text.size()>1600){SetStatus(L"句子过长 · 此句保留原文");continue;}
                std::string prompt="Translate the following text into "+LanguageName(target)+". Note that you should only output the translated result without any additional explanation:\n"+WToUtf8(item.source.text);
                if(model.id=="cat-en-ja")prompt="Translate the following "+LanguageName(source)+" text into "+LanguageName(target)+".\n\n"+WToUtf8(item.source.text);
                json body={{"messages",json::array({{{"role","user"},{"content",prompt}}})},{"stream",false},{"temperature",0},{"seed",42},{"max_tokens",160},{"top_k",20},{"top_p",.6},{"repeat_penalty",1.05},{"cache_prompt",false}};
                std::string response;DWORD code=0;const auto serialized=body.dump();
                if(!Request(port,key,L"/v1/chat/completions",&serialized,response,code)||code!=200){if(cancel.load())break;SetStatus(L"本句翻译未完成 · 保留原文");continue;}
                auto result=json::parse(response,nullptr,false);
                if(result.is_discarded()||!result.contains("choices")||!result["choices"].is_array()||result["choices"].empty())continue;
                const auto& choice=result["choices"][0];
                if(choice.value("finish_reason",std::string())!="stop"||!choice.contains("message")||!choice["message"].contains("content")||!choice["message"]["content"].is_string()){SetStatus(L"本句翻译不完整 · 保留原文");continue;}
                item.translation=Utf8ToW(choice["message"]["content"].get<std::string>());
                if(item.translation.empty())continue;
                {std::lock_guard<std::mutex> lock(mutex);if(item.generation==generation&&!cancel.load()){completed.push_back(std::move(item));if(completed.size()>16)completed.erase(completed.begin());status=L"翻译已就绪";}}
            }
        }catch(const std::exception& e){if(!cancel.load()){LogMsg("Translation stopped: %s",e.what());SetStatus(L"翻译暂不可用 · 识别原文继续显示");}}
        finish();
    }
};

CaptionTranslation::CaptionTranslation():impl_(new Impl){}
CaptionTranslation::~CaptionTranslation(){Stop();}
bool CaptionTranslation::Start(const CaptionModel& model,const std::wstring& directory,const std::string& source,const std::string& target){
    Stop();{std::lock_guard<std::mutex> lock(impl_->mutex);impl_->completed.clear();}
    if(!model.Translation()||directory.empty()||source==target)return false;
    if(!model.SupportsTranslation(source,target)){impl_->SetStatus(L"所选翻译包不支持当前语言方向 · 显示识别原文");return false;}
    if(model.engine!="llama"&&model.engine!="nanosakura")return false;
    if(model.id=="cat-en-ja" && !((source=="en"&&target=="ja")||(source=="ja"&&target=="en")))return false;
    auto* im=impl_.get();im->cancel.store(false);im->active.store(true);
    try{im->worker=std::thread([im,model,directory,source,target]{im->Run(model,directory,source,target);});}catch(...){im->active.store(false);return false;}return true;
}
void CaptionTranslation::RequestStop(){impl_->cancel.store(true);impl_->changed.notify_all();}
void CaptionTranslation::Stop(){auto* im=impl_.get();RequestStop();im->Kill();if(im->worker.joinable())im->worker.join();std::lock_guard<std::mutex> lock(im->mutex);im->queue.clear();im->status.clear();}
void CaptionTranslation::Reset(uint64_t generation){auto* im=impl_.get();std::lock_guard<std::mutex> lock(im->mutex);im->generation=generation;im->queue.clear();im->completed.clear();}
void CaptionTranslation::Submit(const SubCue& cue,uint64_t generation){auto* im=impl_.get();if(!im->active.load()||cue.text.empty())return;std::lock_guard<std::mutex> lock(im->mutex);if(generation!=im->generation)return;
    if(im->queue.size()>=2){im->queue.pop_front();im->status=L"翻译较慢 · 较早句子保留原文";}im->queue.push_back({generation,cue,{}});im->changed.notify_one();
}
void CaptionTranslation::TakeResults(std::vector<Result>& out){std::lock_guard<std::mutex> lock(impl_->mutex);out.insert(out.end(),std::make_move_iterator(impl_->completed.begin()),std::make_move_iterator(impl_->completed.end()));impl_->completed.clear();}
std::wstring CaptionTranslation::Status(){std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->status;}
bool CaptionTranslation::Active() const{return impl_->active.load();}
