// Native menus share the same settings object as the playback worker.
static std::wstring MenuLiteral(std::wstring text) {
    for(size_t i=0;(i=text.find(L'&',i))!=std::wstring::npos;i+=2)text.insert(i,1,L'&');
    return text;
}
static bool AskNumber(const wchar_t* title,const wchar_t* hint,double& value,double lo,double hi) {
    wchar_t initial[64];swprintf(initial,64,L"%g",value);std::wstring text=initial;
    if(!AskListName(text,false,title,hint))return false;
    wchar_t* end=nullptr;const double number=wcstod(text.c_str(),&end);
    if(end==text.c_str() || *end || !std::isfinite(number) || number<lo || number>hi) {
        Toast(L"数值超出允许范围，原设置已保留");return false;
    }
    value=number;return true;
}
static std::wstring TrackLabel(const MediaTrack& t) {
    std::wstring label=L"#"+std::to_wstring(t.id)+L"  ";
    label+=t.language.empty()?L"未标记语言":Utf8ToW(t.language);
    if(!t.title.empty())label+=L" · "+t.title;
    if(!t.codec.empty())label+=L" · "+Utf8ToW(t.codec);
    if(t.channels)label+=L" · "+std::to_wstring(t.channels)+L" 声道";
    if(t.defaultTrack)label+=L" · 默认";
    if(t.forced)label+=L" · 强制";
    if(t.external)label+=L" · 外挂";
    return label;
}
static void CoreSettingsMenu(bool videoOnly) {
    CancelGesture();
    auto options=g_player.PlaybackOptions();const auto state=g_player.CoreInfo();
    HMENU menu=CreatePopupMenu();std::vector<std::function<void()>> actions;
    auto item=[&](HMENU into,const std::wstring& label,std::function<void()> action,bool checked=false,bool enabled=true) {
        actions.push_back(std::move(action));
        AppendMenuW(into,MF_STRING|(checked?MF_CHECKED:0)|(enabled?0:MF_GRAYED),(UINT_PTR)actions.size(),MenuLiteral(label).c_str());
    };
    auto info=[&](HMENU into,const std::wstring& label){AppendMenuW(into,MF_STRING|MF_DISABLED,0,MenuLiteral(label).c_str());};
    auto submenu=[&](HMENU into,const wchar_t* name){HMENU sub=CreatePopupMenu();AppendMenuW(into,MF_POPUP,(UINT_PTR)sub,name);return sub;};
    auto apply=[&]{g_player.ConfigurePlayback(options);g_cfg.hw=options.preferHardware;g_cfg.forceSdr=options.forceSdr;g_cfg.shaderPath=options.shaderPath;SaveConfig();g_ui.dirty=true;};

    if(!g_player.ModernCoreEnabled())info(menu,L"当前为兼容播放模式；增强功能需要标准内核");
    else if(state.loading)info(menu,L"媒体正在加载；设置可预先调整");
    else if(!state.actualInfo.empty())info(menu,Utf8ToW(state.actualInfo));
    if(!state.audioFallbackReason.empty())info(menu,state.audioFallbackReason);

    HMENU video=videoOnly?menu:submenu(menu,L"画质、HDR 与动态映射");
    item(video,L"硬件解码优先（失败自动回退）",[&]{options.preferHardware=true;apply();},options.preferHardware);
    item(video,L"软件解码",[&]{options.preferHardware=false;apply();},!options.preferHardware);
    item(video,L"自动 HDR / SDR 显示输出",[&]{options.forceSdr=false;apply();},!options.forceSdr);
    item(video,L"固定 SDR（映射 HDR 影片）",[&]{options.forceSdr=true;apply();},options.forceSdr);
    auto tone=submenu(video,L"色调映射方法");
    for(const auto& entry:std::vector<std::pair<std::string,std::wstring>>{{"auto",L"自动"},{"spline",L"Spline"},{"bt.2390",L"BT.2390"},{"bt.2446a",L"BT.2446A"},{"st2094-40",L"ST2094-40"},{"reinhard",L"Reinhard"}}) {
        const auto method=entry.first;item(tone,entry.second,[&,method]{options.toneMapping=method;apply();},options.toneMapping==method);
    }
    auto gamut=submenu(video,L"超出显示色域的处理");
    for(const auto& entry:std::vector<std::pair<std::string,std::wstring>>{{"auto",L"自动"},{"perceptual",L"感知映射"},{"relative",L"相对色度"},{"saturation",L"饱和度"}}) {
        const auto method=entry.first;item(gamut,entry.second,[&,method]{options.gamutMapping=method;apply();},options.gamutMapping==method);
    }
    item(video,L"动态分析画面峰值",[&]{options.dynamicPeak=!options.dynamicPeak;apply();},options.dynamicPeak);
    item(video,L"动态映射响应速度…",[&]{double n=options.peakDecay;if(AskNumber(L"动态映射响应",L"平滑衰减 1–1000；越小响应越快，默认 20",n,1,1000)){options.peakDecay=n;apply();}});
    item(video,L"显示峰值亮度…",[&]{double n=options.targetPeak;if(AskNumber(L"显示峰值亮度",L"0 = 自动；手动 10–10000 尼特（HDR 输出）",n,0,10000) && (n==0 || n>=10)){options.targetPeak=n;apply();}});
    item(video,L"SDR / 漫反射参考白…",[&]{double n=options.referenceWhite;if(AskNumber(L"参考白亮度",L"0 = 自动；手动 10–1000 尼特（影响 SDR 映射）",n,0,1000) && (n==0 || n>=10)){options.referenceWhite=n;apply();}});
    item(video,L"恢复默认 HDR 映射",[&]{options.forceSdr=false;options.dynamicPeak=true;options.targetPeak=options.referenceWhite=0;options.peakDecay=20;options.toneMapping=options.gamutMapping="auto";apply();});
    AppendMenuW(video,MF_SEPARATOR,0,nullptr);
    item(video,options.shaderPath.empty()?L"加载 GLSL / hook 着色器…":L"更换 GLSL / hook 着色器…",[&]{
        wchar_t file[32768]{};OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=g_main;
        dialog.lpstrFilter=L"GLSL / hook 着色器\0*.glsl;*.hook\0所有文件\0*.*\0\0";dialog.lpstrFile=file;dialog.nMaxFile=32768;
        dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
        if(GetOpenFileNameW(&dialog) && g_player.ConfigureVideo(file,options.forceSdr)){options.shaderPath=file;apply();}
    });
    item(video,L"移除外部着色器",[&]{options.shaderPath.clear();apply();},false,!options.shaderPath.empty());

    if(!videoOnly) {
        auto audio=submenu(menu,L"音轨");auto subs=submenu(menu,L"字幕轨");
        item(audio,L"自动选择（语言偏好与默认标记）",[&]{g_player.SelectAudioTrack(-1);});
        item(subs,L"自动选择（语言偏好与默认标记）",[&]{g_player.SelectSubtitleTrack(-1);});
        item(subs,L"关闭文件字幕轨",[&]{g_player.SelectSubtitleTrack(0);},state.ready && state.subtitleTrack<=0);
        for(const auto& track:state.tracks) {
            const auto id=track.id;
            if(track.type=="audio")item(audio,TrackLabel(track),[&,id]{g_player.SelectAudioTrack(id);},track.selected);
            if(track.type=="sub")item(subs,TrackLabel(track),[&,id]{g_player.SelectSubtitleTrack(id);g_ui.subsOn=true;SaveConfig();},track.selected);
        }
        item(subs,L"添加外挂字幕…",[]{ImportSubtitleDialog();});
        auto languages=submenu(menu,L"默认轨道与语言偏好");
        info(languages,L"每个新文件重新按语言与容器标记选择");
        for(bool isAudio:{true,false}) {
            auto list=submenu(languages,isAudio?L"优先音轨语言":L"优先字幕语言");
            for(const auto& entry:std::vector<std::pair<std::string,std::wstring>>{{"",L"跟随文件默认轨"},{"zh,zho,chi,en,eng",L"中文 → 英文"},{"en,eng,zh,zho",L"英文 → 中文"},{"ja,jpn,zh,zho",L"日文 → 中文"}}) {
                const auto value=entry.first;item(list,entry.second,[&,isAudio,value]{(isAudio?options.audioLanguages:options.subtitleLanguages)=value;apply();},(isAudio?options.audioLanguages:options.subtitleLanguages)==value);
            }
            item(list,L"自定义语言优先级…",[&,isAudio]{
                std::wstring value=Utf8ToW(isAudio?options.audioLanguages:options.subtitleLanguages);
                if(AskListName(value,false,L"语言优先级",L"填写逗号分隔的语言代码，例如 zh,en,ja")){
                    if(value.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ,- ")==std::wstring::npos){
                        (isAudio?options.audioLanguages:options.subtitleLanguages)=WToUtf8(value);apply();
                    } else Toast(L"请使用语言代码和逗号");
                }
            });
        }

        auto output=submenu(menu,L"音频输出与功放");
        item(output,L"自动 PCM（共享模式）",[&]{options.exclusiveAudio=options.passthrough=false;apply();},!options.exclusiveAudio && !options.passthrough);
        item(output,L"独占 PCM",[&]{options.exclusiveAudio=true;options.passthrough=false;apply();},options.exclusiveAudio && !options.passthrough);
        item(output,L"功放直通（IEC 61937，失败退回 PCM）",[&]{options.passthrough=true;apply();},options.passthrough);
        info(output,L"支持的 HDMI 功放可接收 Atmos / DTS:X 扩展");
        info(output,L"实时字幕或变速时使用 PCM；恢复条件后重试直通");
        if(!state.audioFallbackReason.empty())info(output,state.audioFallbackReason);
        auto devices=submenu(output,L"输出设备");
        item(devices,L"跟随系统默认设备",[&]{options.audioDevice="auto";apply();},options.audioDevice=="auto");
        for(const auto& device:state.audioDevices) {
            if(device.id=="auto")continue;const auto id=device.id;
            item(devices,device.description,[&,id]{options.audioDevice=id;apply();},options.audioDevice==id);
        }
        item(output,L"变速时保持音高",[&]{options.pitchCorrection=!options.pitchCorrection;apply();},options.pitchCorrection);
        auto playback=submenu(menu,L"播放速度、逐帧与 A–B 循环");
        auto rates=submenu(playback,L"播放速度（0.1–12×）");
        for(double rate:{0.1,0.25,0.5,0.75,1.0,1.25,1.5,2.0,3.0,4.0,6.0,8.0,12.0}) {
            wchar_t text[32];swprintf(text,32,L"%g×",rate);
            item(rates,text,[rate]{if(!g_player.SetRateEx(rate))Toast(L"当前播放路径或直播源不支持此倍率");},std::fabs(g_player.Rate()-rate)<0.001);
        }
        item(rates,L"自定义倍率…",[]{double rate=g_player.Rate();if(AskNumber(L"播放速度",L"0.1–12 倍，默认启用音高补偿",rate,0.1,12) && !g_player.SetRateEx(rate))Toast(L"当前路径不支持此倍率");});
        item(playback,L"上一帧（,）",[]{g_player.FrameStep(-1);},false,g_player.CoreActive()&&g_player.HasVideo()&&g_player.CanSeek());
        item(playback,L"下一帧（.）",[]{g_player.FrameStep(1);},false,g_player.CoreActive()&&g_player.HasVideo()&&g_player.CanSeek());
        item(playback,L"设 A 点（[）",[&]{g_player.SetABLoop(g_player.Position(),-1);},false,g_player.CoreActive()&&g_player.CanSeek());
        item(playback,L"设 B 点并循环（]）",[&]{if(!g_player.SetABLoop(state.abA,g_player.Position()))Toast(L"请先设 A 点，并把 B 点放在 A 点之后");},false,state.abA>=0&&g_player.CanSeek());
        item(playback,L"清除 A–B 循环（\\）",[]{g_player.SetABLoop(-1,-1);},false,state.abA>=0 || state.abB>=0);
        if(state.abA>=0)info(playback,L"A = "+FmtTime(state.abA)+L"；B = "+(state.abB>=0?FmtTime(state.abB):L"未设置"));
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        item(menu,L"标准播放内核（完整功能）",[]{g_player.UseModernCore(true);SaveConfig();Toast(L"下次打开媒体使用标准内核");},g_player.ModernCoreEnabled());
        item(menu,L"兼容播放模式（下次打开媒体生效）",[]{g_player.UseModernCore(false);SaveConfig();Toast(L"下次打开媒体使用兼容模式");},!g_player.ModernCoreEnabled());
    }
    POINT point;GetCursorPos(&point);
    const UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,g_main,nullptr);
    DestroyMenu(menu);
    if(command && command<=actions.size())actions[command-1]();
    g_ui.dirty=true;
}
