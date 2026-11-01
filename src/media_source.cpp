#include "playlist.h"
#include <shlwapi.h>
#include <winhttp.h>
#include <sstream>

bool IsNetworkMedia(const std::wstring& p) {
    for(const wchar_t* scheme:{L"http://",L"https://",L"rtsp://",L"rtsps://",L"rtmp://",L"rtmps://",L"rtp://",L"udp://",L"srt://"})
        if(!_wcsnicmp(p.c_str(),scheme,wcslen(scheme)))return true;
    return false;
}
bool IsHttpMedia(const std::wstring& p) {
    return !_wcsnicmp(p.c_str(),L"http://",7) || !_wcsnicmp(p.c_str(),L"https://",8);
}
static bool SafeText(const std::wstring& p) {
    return !p.empty() && p.size() <= 32760 && std::none_of(p.begin(),p.end(),[](wchar_t c){return c<32;});
}
bool IsPlaylistFile(const std::wstring& p) {
    if(IsNetworkMedia(p) && !IsHttpMedia(p))return false;
    const auto ext=LowerExt(p.substr(0,IsNetworkMedia(p)?p.find_first_of(L"?#"):p.size()));
    return ext==L".m3u8" || ext==L".m3u";
}
bool MediaAvailable(const std::wstring& p) { return IsNetworkMedia(p) || FileExists(p); }
bool MediaEqual(const std::wstring& a,const std::wstring& b) {
    return IsNetworkMedia(a)||IsNetworkMedia(b) ? a==b : !_wcsicmp(a.c_str(),b.c_str());
}
std::wstring MediaName(const std::wstring& p) {
    const auto clean=p.substr(0,IsNetworkMedia(p)?p.find_first_of(L"?#"):p.size());
    const auto slash=clean.find_last_of(L"\\/");
    auto name=slash==std::wstring::npos?clean:clean.substr(slash+1);
    return name.empty()?L"网络媒体":name;
}
std::wstring MediaLocation(const std::wstring& p) {
    if(IsNetworkMedia(p)) {
        const auto end=p.find_first_of(L"/?#",p.find(L"://")+3);
        auto origin=p.substr(0,end); const auto user=origin.find_last_of(L'@');
        return user==std::wstring::npos?origin:origin.substr(0,origin.find(L"://")+3)+origin.substr(user+1);
    }
    const auto slash=p.find_last_of(L"\\/");
    return slash==std::wstring::npos?L"":p.substr(0,slash);
}
std::wstring ResolveMedia(const std::wstring& base,const std::wstring& reference) {
    if(!SafeText(reference)) return {};
    if(IsNetworkMedia(reference)) return Playlists::Absolute(reference);
    if(IsNetworkMedia(base)) {
        if(!IsHttpMedia(base))return {};
        std::vector<wchar_t> joined(32768); DWORD count=(DWORD)joined.size();
        if(FAILED(UrlCombineW(base.c_str(),reference.c_str(),joined.data(),&count,URL_DONT_ESCAPE_EXTRA_INFO))) return {};
        std::wstring result(joined.data());
        return IsNetworkMedia(result)?Playlists::Absolute(result):L"";
    }
    // Do not interpret arbitrary URI schemes as Windows relative file names.
    if(reference.find(L':')!=std::wstring::npos && !(reference.size()>2 && reference[1]==L':')) return {};
    const auto slash=base.find_last_of(L"\\/");
    return Playlists::Absolute(PathIsRelativeW(reference.c_str()) && slash!=std::wstring::npos ? base.substr(0,slash+1)+reference:reference);
}
namespace {
struct HttpHandle {
    HINTERNET value=nullptr;
    ~HttpHandle(){if(value) WinHttpCloseHandle(value);}
};
bool ReadListBytes(std::wstring& source,std::string& bytes,const std::shared_ptr<std::atomic<bool>>& cancel) {
    const auto cancelled=[&]{return cancel && cancel->load();};
    constexpr size_t limit=8*1024*1024;
    if(!IsNetworkMedia(source)) {
        FILE* file=_wfopen(source.c_str(),L"rb"); if(!file) return false;
        fseek(file,0,SEEK_END); long size=ftell(file); rewind(file);
        if(size<0 || size>(long)limit){fclose(file);return false;}
        bytes.resize((size_t)size);
        bool ok=fread(bytes.data(),1,bytes.size(),file)==bytes.size(); fclose(file);
        return ok && !cancelled();
    }
    if(!IsHttpMedia(source))return false;
    URL_COMPONENTS parts{}; parts.dwStructSize=sizeof(parts);
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=(DWORD)-1;
    if(!WinHttpCrackUrl(source.c_str(),0,0,&parts)) return false;
    const std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
    std::wstring object(parts.lpszUrlPath,parts.dwUrlPathLength);
    if(parts.dwExtraInfoLength) object.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    HttpHandle session{WinHttpOpen(L"LumenPlayer/1.7",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0)};
    if(!session.value) return false;
    WinHttpSetTimeouts(session.value,2000,2500,2000,2000);
    HttpHandle connection{WinHttpConnect(session.value,host.c_str(),parts.nPort,0)};
    if(!connection.value || cancelled()) return false;
    HttpHandle request{WinHttpOpenRequest(connection.value,L"GET",object.empty()?L"/":object.c_str(),nullptr,WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0)};
    if(!request.value || !WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) ||
        cancelled() || !WinHttpReceiveResponse(request.value,nullptr)) return false;
    DWORD status=0, count=sizeof(status);
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&status,&count,nullptr) || status<200 || status>=300) return false;
    count=0; WinHttpQueryOption(request.value,WINHTTP_OPTION_URL,nullptr,&count);
    if(count && count<=65536) {
        std::vector<wchar_t> effective(count/sizeof(wchar_t)+1);
        if(WinHttpQueryOption(request.value,WINHTTP_OPTION_URL,effective.data(),&count)) source=effective.data();
    }
    const double deadline=QpcNow()+12;
    char block[16384];
    while(!cancelled() && QpcNow()<deadline) {
        DWORD n=0;
        if(!WinHttpReadData(request.value,block,sizeof(block),&n)) return false;
        if(!n) return true;
        if(bytes.size()+n>limit) return false;
        bytes.append(block,n);
    }
    return false;
}
}
bool ReadPlaylist(const std::wstring& source,PlaylistImport& result,const std::shared_ptr<std::atomic<bool>>& cancel) {
    PlaylistImport parsed;
    parsed.source=Playlists::Absolute(source);
    if(parsed.source.empty()) return false;
    std::wstring base=parsed.source; std::string bytes;
    if(!ReadListBytes(base,bytes,cancel) || bytes.find('\0')!=std::string::npos) return false;
    if(bytes.compare(0,3,"\xef\xbb\xbf")==0) bytes.erase(0,3);
    auto text=Utf8ToW(bytes);
    if(WToUtf8(text)!=bytes) return false;
    std::wistringstream rows(text); std::wstring line,name;
    bool header=false;
    while(std::getline(rows,line)) {
        if(cancel && cancel->load()) return false;
        const auto first=line.find_first_not_of(L" \t\r");
        if(first==std::wstring::npos) continue;
        line=line.substr(first,line.find_last_not_of(L" \t\r")-first+1);
        if(!header) { if(line!=L"#EXTM3U") return false; header=true; continue; }
        if(line.rfind(L"#EXT-X-",0)==0) parsed.hls=true;
        if(line.rfind(L"#EXTINF:",0)==0) {
            // IPTV attributes may contain commas inside quoted values.
            bool quoted=false; size_t comma=std::wstring::npos;
            for(size_t i=8;i<line.size();++i) {
                if(line[i]==L'"') quoted=!quoted;
                if(!quoted && line[i]==L','){comma=i;break;}
            }
            name=comma==std::wstring::npos?L"":line.substr(comma+1,512);
            if(!name.empty() && name.back()>=0xd800 && name.back()<=0xdbff) name.pop_back();
            if(std::any_of(name.begin(),name.end(),[](wchar_t c){return c<32;})) return false;
        }
        if(line[0]==L'#') continue;
        auto path=ResolveMedia(base,line);
        if(path.empty() || parsed.items.size()>=10000) return false;
        parsed.items.push_back({std::move(path),name}); name.clear();
    }
    if(!header || parsed.items.empty()) return false;
    parsed.name=MediaName(source); const auto dot=parsed.name.find_last_of(L'.');
    if(dot!=std::wstring::npos) parsed.name.resize(dot);
    if(parsed.name.empty()) parsed.name=L"网络列表";
    parsed.name.resize(std::min<size_t>(70,parsed.name.size()));
    if(!parsed.name.empty() && parsed.name.back()>=0xd800 && parsed.name.back()<=0xdbff) parsed.name.pop_back();
    if(parsed.hls) parsed.items={{parsed.source,MediaName(source)}};
    result=std::move(parsed); return true;
}
