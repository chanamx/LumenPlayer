#pragma once
#include "common.h"
#include <map>
#include <memory>

struct PlaylistItem { std::wstring path, name; };
struct PlaylistImport {
    std::wstring source, name;
    std::vector<PlaylistItem> items;
    bool hls = false;
};
bool IsNetworkMedia(const std::wstring& path);
bool IsHttpMedia(const std::wstring& path);
bool IsPlaylistFile(const std::wstring& path);
bool MediaAvailable(const std::wstring& path);
bool MediaEqual(const std::wstring& a, const std::wstring& b);
std::wstring MediaName(const std::wstring& path);
std::wstring MediaLocation(const std::wstring& path);
std::wstring ResolveMedia(const std::wstring& base, const std::wstring& reference);
bool ReadPlaylist(const std::wstring& source, PlaylistImport& result, const std::shared_ptr<std::atomic<bool>>& cancel = {});

struct PlaylistFolder {
    unsigned id = 0;
    std::wstring name;
    std::vector<std::wstring> files;
    std::wstring source;
    std::map<std::wstring,std::wstring> titles;
};

// Only paths and list metadata are persisted. Media and captions are untouched.
class Playlists {
public:
    Playlists() : folders{{0, L"主列表", {}}} {}
    bool Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;
    PlaylistFolder* Find(unsigned id);
    const PlaylistFolder* Find(unsigned id) const;
    PlaylistFolder& Active() { return *Find(selected); }
    unsigned Create(const std::wstring& name);
    bool Rename(unsigned id, const std::wstring& name);
    bool Remove(unsigned id);
    int Add(unsigned id, const std::wstring& path);
    int AddMany(unsigned id, const std::vector<std::wstring>& paths);
    int IndexOf(unsigned id, const std::wstring& path) const;
    unsigned Import(const PlaylistImport& data);
    std::wstring ItemName(unsigned id, const std::wstring& path) const;
    static std::wstring Absolute(const std::wstring& path);
    std::vector<PlaylistFolder> folders;
    unsigned selected = 0;
private:
    bool writable_ = true;
};

bool IsMediaFile(const std::wstring& path);
std::vector<std::wstring> MediaInFolder(const std::wstring& directory);
