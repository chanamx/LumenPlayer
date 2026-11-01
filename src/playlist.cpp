#include "playlist.h"
#include <iomanip>
#include <sstream>
#include <set>
#include <map>
#include <shlwapi.h>
#include <winhttp.h>

static bool ValidName(const std::wstring& s) {
    return !s.empty() && s.size() <= 80 && s.find_first_not_of(L" \t") != std::wstring::npos &&
        std::none_of(s.begin(), s.end(), [](wchar_t c) { return c < 32; });
}

struct PathLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        if(IsNetworkMedia(a)!=IsNetworkMedia(b)) return IsNetworkMedia(a)<IsNetworkMedia(b);
        return IsNetworkMedia(a)?a<b:_wcsicmp(a.c_str(),b.c_str())<0;
    }
};

PlaylistFolder* Playlists::Find(unsigned id) {
    for (auto& folder : folders) if (folder.id == id) return &folder;
    return nullptr;
}
const PlaylistFolder* Playlists::Find(unsigned id) const {
    for (const auto& folder : folders) if (folder.id == id) return &folder;
    return nullptr;
}

std::wstring Playlists::Absolute(const std::wstring& path) {
    if (path.empty() || path.size() > 32760 || std::any_of(path.begin(), path.end(), [](wchar_t c) { return c < 32; })) return {};
    if(IsNetworkMedia(path)) {
        // Preserve the original escaped query and case: signed stream URLs are
        // byte-sensitive. The playback core handles each allowed protocol.
        const auto begin=path.find(L"://")+3;
        const auto end=path.find_first_of(L"/?#",begin);
        const auto authority=path.substr(begin,end-begin);
        const auto at=authority.find_last_of(L'@');
        const auto host=authority.substr(at==std::wstring::npos?0:at+1);
        if(host.empty() || host[0]==L':' || path.find(L'\\')!=std::wstring::npos ||
           std::any_of(authority.begin(),authority.end(),[](wchar_t c){return c<=32 || c==L'"' || c==L'<' || c==L'>';}) )return {};
        if(IsHttpMedia(path)) {
            URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=(DWORD)-1;
            if(!WinHttpCrackUrl(path.c_str(),0,0,&parts) || !parts.dwHostNameLength)return {};
        }
        return path;
    }
    if(path.find(L"://")!=std::wstring::npos) return {};
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!needed || needed > 32768) return {};
    std::vector<wchar_t> full(needed);
    DWORD n = GetFullPathNameW(path.c_str(), (DWORD)full.size(), full.data(), nullptr);
    if (!n || n >= full.size()) return {};
    std::wstring result(full.data(), n);
    std::replace(result.begin(), result.end(), L'/', L'\\');
    return result;
}

int Playlists::IndexOf(unsigned id, const std::wstring& path) const {
    const auto* folder = Find(id);
    if (folder) for (size_t i = 0; i < folder->files.size(); ++i)
        if (MediaEqual(folder->files[i],path)) return (int)i;
    return -1;
}

int Playlists::Add(unsigned id, const std::wstring& path) {
    return AddMany(id, {path});
}

int Playlists::AddMany(unsigned id, const std::vector<std::wstring>& paths) {
    auto* folder = Find(id);
    if (!folder) return -1;
    size_t count = 0;
    for (const auto& f : folders) count += f.files.size();
    std::map<std::wstring,int,PathLess> known;
    for (size_t i = 0; i < folder->files.size(); ++i) known.emplace(folder->files[i], (int)i);
    int first = -1;
    for (const auto& path : paths) {
        auto full = Absolute(path);
        if (full.empty()) continue;
        auto old = known.find(full);
        if (old != known.end()) { if (first < 0) first = old->second; continue; }
        if (count >= 10000) continue;
        const int index = (int)folder->files.size();
        known.emplace(full,index); folder->files.push_back(std::move(full)); ++count;
        if (first < 0) first = index;
    }
    return first;
}

unsigned Playlists::Create(const std::wstring& name) {
    if (!ValidName(name) || folders.size() >= 128) return 0;
    unsigned id = 1;
    for (const auto& f : folders) {
        if (!_wcsicmp(f.name.c_str(), name.c_str())) return 0;
        id = std::max(id, f.id + 1);
    }
    if (!id || id > 1000000) return 0;
    folders.push_back({id, name, {}});
    return id;
}
unsigned Playlists::Import(const PlaylistImport& data) {
    if(data.source.empty() || data.items.empty()) return 0;
    unsigned id=0;
    for(const auto& folder:folders) if(!folder.source.empty() && MediaEqual(folder.source,data.source)){id=folder.id;break;}
    if(!id) {
        auto name=data.name;
        for(unsigned suffix=2;!id && suffix<=130;++suffix) {
            id=Create(name);
            name=data.name+L" ("+std::to_wstring(suffix)+L")";
        }
    }
    if(!id) return 0;
    auto* folder=Find(id); folder->source=data.source;
    std::vector<std::wstring> paths; paths.reserve(data.items.size());
    for(const auto& item:data.items) paths.push_back(item.path);
    AddMany(id,paths);
    std::map<std::wstring,size_t,PathLess> indices;
    for(size_t i=0;i<folder->files.size();++i) indices.emplace(folder->files[i],i);
    for(const auto& item:data.items) {
        auto index=indices.find(item.path);
        if(index!=indices.end() && !item.name.empty()) folder->titles[folder->files[index->second]]=item.name.substr(0,512);
    }
    selected=id; return id;
}
std::wstring Playlists::ItemName(unsigned id,const std::wstring& path) const {
    const auto* folder=Find(id);
    if(folder) {auto it=folder->titles.find(path); if(it!=folder->titles.end()) return it->second;}
    return MediaName(path);
}
bool Playlists::Rename(unsigned id, const std::wstring& name) {
    auto* folder = Find(id);
    if (!id || !folder || !ValidName(name)) return false;
    for (const auto& f : folders) if (f.id != id && !_wcsicmp(f.name.c_str(), name.c_str())) return false;
    folder->name = name;
    return true;
}
bool Playlists::Remove(unsigned id) {
    if (!id || !Find(id)) return false;
    folders.erase(std::remove_if(folders.begin(), folders.end(), [id](const PlaylistFolder& f) { return f.id == id; }), folders.end());
    if (selected == id) selected = 0;
    return true;
}

bool Playlists::Load(const std::wstring& path) {
    if (!FileExists(path)) return true;
    writable_ = false; // Never overwrite a damaged or newer store automatically.
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    rewind(file);
    if (size < 0 || size > 8 * 1024 * 1024) { fclose(file); return false; }
    std::string bytes((size_t)size, '\0');
    const bool read = fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    if (!read || bytes.find('\0') != std::string::npos) return false;
    std::istringstream stream(bytes);
    std::string line;
    if (!std::getline(stream, line) || (line != "LUMEN_PLAYLISTS 1" && line != "LUMEN_PLAYLISTS 2")) return false;
    const bool version2=line=="LUMEN_PLAYLISTS 2";
    std::vector<PlaylistFolder> loaded;
    std::set<unsigned> ids;
    std::set<std::wstring,PathLess> names, files;
    unsigned active = 0;
    size_t total = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        std::istringstream row(line);
        std::string kind, text;
        unsigned id = 0;
        row >> kind;
        if (kind == "selected") { if (!(row >> active)) return false; }
        else if (kind == "folder") {
            if (!(row >> id >> std::quoted(text)) || id > 1000000 || !ids.insert(id).second || loaded.size() >= 128) return false;
            auto name = Utf8ToW(text);
            if (!ValidName(name) || WToUtf8(name) != text) return false;
            if (!id) name = L"主列表";
            if (!names.insert(name).second) return false;
            files.clear();
            loaded.push_back({id, std::move(name), {}});
        } else if (kind == "file") {
            if (loaded.empty() || !(row >> std::quoted(text)) || ++total > 10000) return false;
            auto full = Utf8ToW(text);
            if (full.empty() || full.size() > 32760 || WToUtf8(full) != text || (!IsNetworkMedia(full) && PathIsRelativeW(full.c_str())) || Absolute(full).empty() ||
                std::any_of(full.begin(), full.end(), [](wchar_t c) { return c < 32; })) return false;
            if (!files.insert(full).second) return false;
            loaded.back().files.push_back(std::move(full));
        } else if(version2 && kind=="source") {
            if(loaded.empty() || !loaded.back().source.empty() || !(row>>std::quoted(text))) return false;
            auto value=Utf8ToW(text);
            if(WToUtf8(value)!=text || Absolute(value).empty()) return false;
            loaded.back().source=std::move(value);
        } else if(version2 && kind=="title") {
            std::string title;
            if(loaded.empty() || !(row>>std::quoted(text)>>std::quoted(title))) return false;
            auto path=Utf8ToW(text), name=Utf8ToW(title);
            if(!files.count(path) || name.empty() || name.size()>512 || WToUtf8(name)!=title ||
                std::any_of(name.begin(),name.end(),[](wchar_t c){return c<32;}) || !loaded.back().titles.emplace(path,name).second) return false;
        } else return false;
        row >> std::ws;
        if (!row.eof()) return false;
    }
    if (!ids.count(0)) return false;
    folders = std::move(loaded);
    Find(0)->name = L"主列表";
    selected = ids.count(active) ? active : 0;
    writable_ = true;
    return true;
}

bool Playlists::Save(const std::wstring& path) const {
    if (!writable_) return false;
    std::ostringstream stream;
    stream << "LUMEN_PLAYLISTS 2\nselected " << selected << '\n';
    for (const auto& folder : folders) {
        stream << "folder " << folder.id << ' ' << std::quoted(WToUtf8(folder.name)) << '\n';
        if(!folder.source.empty()) stream << "source " << std::quoted(WToUtf8(folder.source)) << '\n';
        for (const auto& file : folder.files) {
            stream << "file " << std::quoted(WToUtf8(file)) << '\n';
            auto name=folder.titles.find(file);
            if(name!=folder.titles.end()) stream << "title " << std::quoted(WToUtf8(file)) << ' ' << std::quoted(WToUtf8(name->second)) << '\n';
        }
    }
    const auto bytes = stream.str();
    if (bytes.size() > 8 * 1024 * 1024) return false;
    const std::wstring temp = path + L".lumen-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size();
    ok = FlushFileBuffers(file) && ok;
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

bool IsMediaFile(const std::wstring& path) {
    if(IsNetworkMedia(path) || IsPlaylistFile(path)) return true;
    const auto ext = LowerExt(path);
    const std::wstring supported = L"|.mpd|.mp4|.mkv|.mov|.avi|.webm|.m4v|.ts|.mts|.m2ts|.mpg|.mpeg|.vob|.flv|.wmv|.mxf|.ogv|.3gp|.mp3|.wav|.flac|.aac|.m4a|.ogg|.opus|.wma|.ape|";
    return !ext.empty() && supported.find(L"|" + ext + L"|") != std::wstring::npos;
}

std::vector<std::wstring> MediaInFolder(const std::wstring& directory) {
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW entry{};
    HANDLE find = FindFirstFileW(PathJoin(directory, L"*").c_str(), &entry);
    if (find == INVALID_HANDLE_VALUE) return files;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsMediaFile(entry.cFileName))
            files.push_back(PathJoin(directory, entry.cFileName));
    } while (files.size() < 10000 && FindNextFileW(find, &entry));
    FindClose(find);
    std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });
    return files;
}
