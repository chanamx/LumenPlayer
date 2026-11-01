#ifndef UNICODE
#define UNICODE
#endif
#include "model_dialog.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shellapi.h>

namespace {
enum { ModelList=100,Source,Target,Install,CancelDownload,Use,Remove,Origin,OriginalOnly,Close,Description,Progress,Status,Current,SourceLabel,TargetLabel,Title,Intro,DetailTitle,ControlEnd };
const char* targets[]={"zh","en","ja","de","fr"};
constexpr COLORREF bg=RGB(20,23,31),surface=RGB(28,33,44),line=RGB(54,63,80),ink=RGB(231,236,246),muted=RGB(162,174,194),blue=RGB(145,181,255);
struct Dialog;
LRESULT CALLBACK ControlProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
struct Dialog {
    CaptionModelStore& store;CaptionSelection& selection;
    std::function<void(const CaptionSelection&)> apply;
    std::function<bool(const std::string&)> release;
    HWND hwnd=nullptr,list=nullptr,hot=nullptr;HFONT font=nullptr,smallFont=nullptr,boldFont=nullptr,titleFont=nullptr;HBRUSH background=nullptr,field=nullptr;
    HIMAGELIST rowHeight=nullptr;
    float scale=1;int selected=0;bool pendingDelete=false;
    bool modelInstalled=false;int progressValue=-1;UINT timerInterval=0;
    std::wstring texts[ControlEnd-ModelList]{};
    std::string pendingModel{};
    std::wstring notice{};
    std::vector<std::wstring> statuses{};
    int S(int n)const{return int(n*scale+.5f);}
    const CaptionModel& Model()const{return CaptionCatalog().at(selected);}
    void SetText(int id,const std::wstring& text){auto& old=texts[id-ModelList];if(old!=text){old=text;SetDlgItemTextW(hwnd,id,text.c_str());}}
    void Enabled(int id,bool value){HWND child=GetDlgItem(hwnd,id);if(bool(IsWindowEnabled(child))!=value)EnableWindow(child,value);}
    void Message(const std::wstring& text){SetText(Status,text);}
    void Notice(const std::wstring& text){notice=text;Message(text);}
    HWND Child(const wchar_t* klass,const wchar_t* text,DWORD style,int id){
        HWND child=CreateWindowExW(0,klass,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,hwnd,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
        texts[id-ModelList]=text;
        SendMessageW(child,WM_SETFONT,(WPARAM)font,FALSE);SetWindowTheme(child,L"DarkMode_Explorer",nullptr);
        if(id==ModelList||(id>=Install&&id<=Close)||id==Source||id==Target||id==Progress)SetWindowSubclass(child,ControlProc,1,(DWORD_PTR)this);
        return child;
    }
    void Fonts(){
        HFONT old[]={font,smallFont,boldFont,titleFont};
        auto make=[&](int size,int weight){return CreateFontW(-S(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");};
        font=make(14,FW_NORMAL);smallFont=make(12,FW_NORMAL);boldFont=make(14,FW_SEMIBOLD);titleFont=make(20,FW_SEMIBOLD);
        for(int id=ModelList;id<ControlEnd;++id)if(HWND child=GetDlgItem(hwnd,id)){
            HFONT f=id==Title?titleFont:id==DetailTitle?boldFont:(id==Intro||id==Current||id==Description||id==Status||id==SourceLabel||id==TargetLabel)?smallFont:font;
            SendMessageW(child,WM_SETFONT,(WPARAM)f,FALSE);
        }
        if(list){
            // A native small-image slot sets report-row height without replacing list behavior.
            auto next=ImageList_Create(1,S(32),ILC_COLOR32,1,1);ListView_SetImageList(list,next,LVSIL_SMALL);
            if(rowHeight)ImageList_Destroy(rowHeight);rowHeight=next;
            SendMessageW(ListView_GetHeader(list),WM_SETFONT,(WPARAM)smallFont,FALSE);
        }
        for(int id:{Source,Target}){SendDlgItemMessageW(hwnd,id,CB_SETITEMHEIGHT,(WPARAM)-1,S(26));SendDlgItemMessageW(hwnd,id,CB_SETITEMHEIGHT,0,S(30));}
        for(auto f:old)if(f)DeleteObject(f);
    }
    void FreeDrawing(){
        if(rowHeight){ListView_SetImageList(list,nullptr,LVSIL_SMALL);ImageList_Destroy(rowHeight);rowHeight=nullptr;}
        for(auto f:{font,smallFont,boldFont,titleFont})if(f)DeleteObject(f);font=smallFont=boldFont=titleFont=nullptr;
        if(background)DeleteObject(background);if(field)DeleteObject(field);background=field=nullptr;
    }
    void Layout(){
        if(!list)return;
        RECT r{};GetClientRect(hwnd,&r);int pad=S(20),gap=S(8),w=r.right-2*pad;
        const bool compact=r.bottom<S(560);
        auto place=[&](int id,int x,int y,int width,int height){MoveWindow(GetDlgItem(hwnd,id),x,y,std::max(1,width),height,TRUE);};
        place(Title,pad,S(14),w,S(30));place(Intro,pad,S(44),w,S(20));ShowWindow(GetDlgItem(hwnd,Intro),compact?SW_HIDE:SW_SHOWNA);
        place(Current,pad,compact?S(49):S(68),w,S(36));
        const int footer=S(232),top=compact?S(88):S(112),listHeight=std::max(S(58),int(r.bottom)-top-footer);
        place(ModelList,pad,top,w,listHeight);
        int y=top+listHeight+S(8);place(DetailTitle,pad,y,w,S(22));y+=S(26);place(Description,pad,y,w,S(34));y+=S(38);
        const int half=(w-S(24))/2,label=S(56);
        place(SourceLabel,pad,y,label,S(30));place(Source,pad+label,y,half-label,S(240));
        place(TargetLabel,pad+half+S(24),y,label,S(30));place(Target,pad+half+S(24)+label,y,half-label,S(240));y+=S(38);
        int bw=(w-3*gap)/4;int column=0;
        for(int id:{Install,Use,CancelDownload,Remove})place(id,pad+column++*(bw+gap),y,bw,S(34));y+=S(40);
        place(Origin,pad,y,S(146),S(28));place(OriginalOnly,pad+S(154),y,S(124),S(28));place(Close,r.right-pad-S(82),y,S(82),S(28));y+=S(34);
        place(Progress,pad,y,w,S(3));y+=S(8);place(Status,pad,y,w,S(28));
        RECT client{};GetClientRect(list,&client);const int tail=S(60+82+96+142),first=std::max(S(100),int(client.right)-tail);
        const int widths[]={first,S(60),S(82),S(96),std::max(S(90),int(client.right)-first-S(60+82+96))};
        for(int i=0;i<5;++i)ListView_SetColumnWidth(list,i,widths[i]);
        ListView_EnsureVisible(list,selected,FALSE);
        InvalidateRect(hwnd,nullptr,TRUE);
    }
    void Choose(){
        int row=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(row>=0)selected=row;
        const auto& model=Model();SetText(DetailTitle,model.name);SetText(Description,model.description+L"  ·  "+model.license);
        SendDlgItemMessageW(hwnd,Source,CB_RESETCONTENT,0,0);
        for(const auto& provider:model.providers)SendDlgItemMessageW(hwnd,Source,CB_ADDSTRING,0,(LPARAM)CaptionProviderName(provider).c_str());
        SendDlgItemMessageW(hwnd,Source,CB_SETCURSEL,0,0);Refresh();
        ListView_EnsureVisible(list,selected,FALSE);
        InvalidateRect(GetDlgItem(hwnd,Install),nullptr,FALSE);InvalidateRect(GetDlgItem(hwnd,Use),nullptr,FALSE);
    }
    void SourceNotice(){
        const auto& model=Model();const int i=(int)SendDlgItemMessageW(hwnd,Source,CB_GETCURSEL,0,0);
        if(i<0||i>=(int)model.providers.size())return;
        const auto& provider=model.providers[i];
        const bool archiveSource=std::any_of(model.fallback.download.sources.begin(),model.fallback.download.sources.end(),[&](const auto& source){return source.provider==provider;});
        if(provider!="auto"&&!archiveSource)for(const auto& file:model.files){
            if(!file.sources.empty()&&std::none_of(file.sources.begin(),file.sources.end(),[&](const auto& s){return s.provider==provider;})){
                Notice(L"此来源提供模型主体；部分辅助文件改用已核验备用来源，先尝试："+CaptionProviderName(file.sources[0].provider)+L"。");return;
            }
        }
        notice.clear();
        if(archiveSource)Message(L"此来源下载完整中文模型压缩包，核对模型与词表后安装。");
        else if(!model.archives.empty())Message(L"模型按所选来源下载；翻译运行组件自动使用 GitHub 发布或 API 备用入口。");
        else Message(L"首次安装需联网，安装后离线使用。自动模式失败会换源；安装不会自动启用模型。");
    }
    void Refresh(){
        const auto progress=store.Status();const auto& catalog=CaptionCatalog();
        const auto* asr=FindCaptionModel(selection.recognition);const auto* mt=FindCaptionModel(selection.translation);
        const bool unsupported=mt&&asr&&mt->language!="multi"&&!mt->SupportsTranslation(asr->language,selection.target);
        if(statuses.size()!=catalog.size())statuses.resize(catalog.size());
        for(size_t i=0;i<catalog.size();++i){const auto& m=catalog[i];std::wstring value;
            const bool installed=!store.InstalledPath(m).empty();if((int)i==selected)modelInstalled=installed;
            if(progress.busy&&progress.model==m.id)value=progress.phase+(progress.total?L" "+std::to_wstring(std::min<uint64_t>(100,progress.received*100/progress.total))+L"%":L"");
            else if(installed)value=(selection.recognition==m.id||selection.translation==m.id)?L"已安装 · 已选择":L"已安装";
            else value=L"未安装";
            if(unsupported&&m.id==selection.translation&&value==L"已安装 · 已选择")value=L"方向不支持";
            if(value!=statuses[i]){ListView_SetItemText(list,(int)i,4,const_cast<wchar_t*>(value.c_str()));statuses[i]=value;}
        }
        const auto& model=Model();bool installed=modelInstalled,task=progress.busy&&progress.model==model.id;
        const bool current=model.Translation()?selection.translation==model.id:selection.recognition==model.id;
        SetText(Use,current?L"当前已选择":model.Translation()?L"用作翻译":L"用作识别");
        SetText(Install,installed?L"重新安装":L"下载安装");
        Enabled(Install,!progress.busy&&!pendingDelete);Enabled(Use,installed&&!task&&!pendingDelete&&!current);
        Enabled(Remove,!progress.busy&&!pendingDelete);Enabled(CancelDownload,progress.busy);Enabled(Source,!progress.busy&&!pendingDelete);
        Enabled(OriginalOnly,!selection.translation.empty());
        const int percent=progress.total?int(std::min<uint64_t>(100,progress.received*100/progress.total)):0;
        if(percent!=progressValue){progressValue=percent;SendDlgItemMessageW(hwnd,Progress,PBM_SETPOS,percent,0);}
        const UINT interval=progress.busy||pendingDelete?250:1000;
        if(interval!=timerInterval){timerInterval=interval;SetTimer(hwnd,1,interval,nullptr);}
        if(!progress.busy&&!notice.empty())Message(notice);
        else if(progress.busy||!progress.phase.empty())Message(progress.phase+(progress.detail.empty()?L"":L" · "+progress.detail));
        if(pendingDelete){
            Message(L"正在停止并释放模型，请稍候…");
            const auto* removing=FindCaptionModel(pendingModel);
            if(removing&&selection.translation==removing->id){selection.translation.clear();apply(selection);}
            if(removing&&release(removing->id)){std::wstring error;bool ok=store.Remove(*removing,error);pendingDelete=false;Notice(ok?L"模型已删除。其他文件、字幕及播放列表已保留。":error);statuses.clear();}
        }
        // Deletion callbacks may have changed the selection during this refresh.
        asr=FindCaptionModel(selection.recognition);mt=FindCaptionModel(selection.translation);
        const bool currentUnsupported=mt&&asr&&mt->language!="multi"&&!mt->SupportsTranslation(asr->language,selection.target);
        const std::wstring currentText=L"当前识别   "+(asr?asr->name:L"未选择")+L"\n当前翻译   "+(mt?mt->name+L" → "+CaptionLanguage(selection.target)+(currentUnsupported?L"（方向不支持，显示原文）":L""):L"仅原文");
        SetText(Current,currentText);
    }
    void Action(int id){
        const auto& model=Model();std::wstring error;
        if(id==Install){const int index=(int)SendDlgItemMessageW(hwnd,Source,CB_GETCURSEL,0,0);if(index<0||index>=(int)model.providers.size())return;
            notice.clear();if(!store.Install(model,model.providers[index],error))Notice(error);}
        else if(id==CancelDownload){store.Cancel();Notice(L"正在取消下载，已下载内容可继续。");}
        else if(id==Use){
            if(store.InstalledPath(model).empty())return;
            const auto* asr=FindCaptionModel(selection.recognition);
            if(model.Translation()&&model.language!="multi"&&(!asr||!model.SupportsTranslation(asr->language,selection.target))){Notice(L"此翻译包不支持当前语言方向。NanoSakura 需要先选择日语识别，并将目标设为中文。");return;}
            if(model.Translation())selection.translation=model.id;else selection.recognition=model.id;
            apply(selection);Notice(L"已保存模型选择。实时字幕开启时使用。 ");
        }
        else if(id==Remove){
            const auto text=L"删除“"+model.name+L"”及其未完成下载？\n\n会先停止使用此模型。已保存字幕、播放列表和其他模型将保留。";
            if(MessageBoxW(hwnd,text.c_str(),L"删除字幕模型",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES){pendingDelete=true;pendingModel=model.id;notice.clear();}}
        else if(id==Origin)ShellExecuteW(hwnd,L"open",model.homepage.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        else if(id==OriginalOnly){selection.translation.clear();apply(selection);Notice(L"已选择仅显示识别原文。");}
        Refresh();
    }
};
void FlatFill(HDC dc,const RECT& r,COLORREF color){SetDCBrushColor(dc,color);FillRect(dc,&r,(HBRUSH)GetStockObject(DC_BRUSH));}
void Round(HDC dc,RECT r,int radius,COLORREF fill,COLORREF border){
    SelectObject(dc,GetStockObject(DC_BRUSH));SelectObject(dc,GetStockObject(DC_PEN));SetDCBrushColor(dc,fill);SetDCPenColor(dc,border);
    RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
}
void Label(HDC dc,const wchar_t* text,RECT r,HFONT font,COLORREF color,UINT align=DT_LEFT){
    SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|align);
}
void DrawButton(Dialog& p,const DRAWITEMSTRUCT& item){
    const int saved=SaveDC(item.hDC),id=int(item.CtlID);RECT r=item.rcItem;
    const bool disabled=(item.itemState&ODS_DISABLED)!=0,down=(item.itemState&ODS_SELECTED)!=0,hover=p.hot==item.hwndItem;
    const bool primary=(id==Install&&!p.modelInstalled)||(id==Use&&p.modelInstalled&&!disabled);
    const bool quiet=id==Origin||id==OriginalOnly;
    COLORREF fill=quiet?bg:surface,border=quiet?bg:line,color=id==Remove?RGB(238,161,165):ink;
    if(primary){fill=RGB(56,100,183);border=fill;color=RGB(255,255,255);}
    if(hover&&!disabled){fill=primary?RGB(65,115,204):RGB(39,48,65);border=primary?fill:RGB(87,111,150);}
    if(down&&!disabled)fill=primary?RGB(42,79,147):RGB(32,40,55);
    if(disabled){fill=quiet?bg:RGB(25,29,38);border=quiet?bg:RGB(41,47,60);color=RGB(112,123,143);}
    if(!disabled&&(item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT))border=blue;
    FlatFill(item.hDC,r,bg);Round(item.hDC,r,p.S(12),fill,border);InflateRect(&r,-p.S(8),0);
    Label(item.hDC,p.texts[id-ModelList].c_str(),r,p.font,quiet&&!disabled?blue:color,DT_CENTER);
    RestoreDC(item.hDC,saved);
}
void DrawCombo(Dialog& p,HWND hwnd,HDC dc){
    const int saved=SaveDC(dc);RECT r{};GetClientRect(hwnd,&r);const bool enabled=IsWindowEnabled(hwnd)!=FALSE;
    FlatFill(dc,r,bg);Round(dc,r,p.S(10),surface,enabled&&(GetFocus()==hwnd||p.hot==hwnd)?blue:line);
    const int index=int(SendMessageW(hwnd,CB_GETCURSEL,0,0));wchar_t text[512]{};
    if(index>=0&&SendMessageW(hwnd,CB_GETLBTEXTLEN,index,0)<512)SendMessageW(hwnd,CB_GETLBTEXT,index,(LPARAM)text);
    RECT label=r;label.left+=p.S(10);label.right-=p.S(28);Label(dc,text,label,p.font,enabled?ink:muted);
    const int x=r.right-p.S(16),y=(r.bottom+r.top)/2;POINT arrow[]={{x-p.S(4),y-p.S(2)},{x,y+p.S(2)},{x+p.S(4),y-p.S(2)}};
    SelectObject(dc,GetStockObject(DC_PEN));SetDCPenColor(dc,enabled?muted:line);Polyline(dc,arrow,3);RestoreDC(dc,saved);
}
LRESULT DrawRow(Dialog& p,NMLVCUSTOMDRAW& draw){
    if(draw.nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
    if(draw.nmcd.dwDrawStage!=CDDS_ITEMPREPAINT)return CDRF_DODEFAULT;
    const int saved=SaveDC(draw.nmcd.hdc),row=int(draw.nmcd.dwItemSpec);RECT bounds{};ListView_GetItemRect(p.list,row,&bounds,LVIR_BOUNDS);
    const bool selected=(ListView_GetItemState(p.list,row,LVIS_SELECTED)&LVIS_SELECTED)!=0;
    FlatFill(draw.nmcd.hdc,bounds,selected?RGB(38,57,87):surface);
    if(selected){RECT marker=bounds;marker.right=marker.left+p.S(3);FlatFill(draw.nmcd.hdc,marker,blue);}
    for(int i=0;i<5;++i){RECT r{};ListView_GetSubItemRect(p.list,row,i,LVIR_BOUNDS,&r);if(i==0)r.right=r.left+ListView_GetColumnWidth(p.list,0);
        r.left+=p.S(12);r.right-=p.S(8);wchar_t value[512]{};ListView_GetItemText(p.list,row,i,value,512);
        COLORREF color=i==0?ink:muted;if(i==4&&wcsstr(value,L"已安装"))color=RGB(126,210,174);if(i==4&&wcsstr(value,L"不支持"))color=RGB(241,185,114);
        Label(draw.nmcd.hdc,value,r,i==0?p.font:p.smallFont,color);
    }
    if(selected&&GetFocus()==p.list&&!(SendMessageW(p.list,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){
        InflateRect(&bounds,-p.S(4),-p.S(3));SetTextColor(draw.nmcd.hdc,blue);SetBkColor(draw.nmcd.hdc,RGB(38,57,87));DrawFocusRect(draw.nmcd.hdc,&bounds);}
    RestoreDC(draw.nmcd.hdc,saved);return CDRF_SKIPDEFAULT;
}
LRESULT CALLBACK ControlProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref){
    auto& p=*(Dialog*)ref;const int id=GetDlgCtrlID(hwnd);
    if(id==ModelList&&msg==WM_NOTIFY){auto* h=(NMHDR*)lp;if(h->hwndFrom==ListView_GetHeader(hwnd)&&h->code==NM_CUSTOMDRAW){
        auto& draw=*(NMCUSTOMDRAW*)lp;if(draw.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
        if(draw.dwDrawStage==CDDS_ITEMPREPAINT){const int saved=SaveDC(draw.hdc);FlatFill(draw.hdc,draw.rc,RGB(34,40,53));
            wchar_t text[64]{};HDITEMW item{};item.mask=HDI_TEXT;item.pszText=text;item.cchTextMax=64;Header_GetItem(h->hwndFrom,int(draw.dwItemSpec),&item);
            RECT r=draw.rc;r.left+=p.S(12);Label(draw.hdc,text,r,p.smallFont,muted);RestoreDC(draw.hdc,saved);return CDRF_SKIPDEFAULT;}
    }}
    const bool combo=id==Source||id==Target;
    if(id==Progress&&msg==WM_NCPAINT)return 0;
    if((combo||id==Progress)&&(msg==WM_PAINT||msg==WM_PRINTCLIENT||msg==WM_PRINT)){
        PAINTSTRUCT ps{};HDC dc=msg==WM_PAINT?BeginPaint(hwnd,&ps):(HDC)wp;
        if(combo)DrawCombo(p,hwnd,dc);
        else{const int saved=SaveDC(dc);RECT r{};GetClientRect(hwnd,&r);FlatFill(dc,r,surface);r.right=r.right*std::max(0,p.progressValue)/100;FlatFill(dc,r,blue);RestoreDC(dc,saved);}
        if(msg==WM_PAINT)EndPaint(hwnd,&ps);return 0;}
    if((combo||id==Progress)&&msg==WM_ERASEBKGND)return 1;
    if(id!=ModelList&&id!=Progress){
        if(msg==WM_MOUSEMOVE&&p.hot!=hwnd){HWND old=p.hot;p.hot=hwnd;if(old)InvalidateRect(old,nullptr,FALSE);InvalidateRect(hwnd,nullptr,FALSE);
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);}
        if(msg==WM_MOUSELEAVE&&p.hot==hwnd){p.hot=nullptr;InvalidateRect(hwnd,nullptr,FALSE);}
        if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==WM_ENABLE||msg==WM_SETTEXT)InvalidateRect(hwnd,nullptr,FALSE);
    }
    if(msg==WM_NCDESTROY){if(p.hot==hwnd)p.hot=nullptr;RemoveWindowSubclass(hwnd,ControlProc,1);}
    return DefSubclassProc(hwnd,msg,wp,lp);
}
INT_PTR CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* p=(Dialog*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
    if(msg==WM_INITDIALOG){p=(Dialog*)lp;p->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)p);SetWindowTextW(hwnd,L"实时字幕 · 模型与语言");
        using Dpi=UINT(WINAPI*)(HWND);auto dpi=(Dpi)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");p->scale=dpi?float(dpi(hwnd))/96:1;
        p->background=CreateSolidBrush(bg);p->field=CreateSolidBrush(surface);
        BOOL dark=TRUE;DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));
        p->Child(L"STATIC",L"模型与语言",SS_ENDELLIPSIS,Title);
        p->Child(L"STATIC",L"按需安装，离线使用。为实时字幕选择合适的识别与翻译模型。",SS_ENDELLIPSIS,Intro);
        p->Child(L"STATIC",L"",0,Current);
        p->list=p->Child(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,ModelList);
        ListView_SetExtendedListViewStyle(p->list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        ListView_SetBkColor(p->list,surface);ListView_SetTextBkColor(p->list,surface);ListView_SetTextColor(p->list,ink);
        const wchar_t* headers[]={L"模型",L"用途",L"档位",L"下载大小",L"状态"};
        for(int i=0;i<5;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t*>(headers[i]);column.cx=p->S(120);ListView_InsertColumn(p->list,i,&column);}
        const auto& catalog=CaptionCatalog();for(size_t i=0;i<catalog.size();++i){const auto& m=catalog[i];LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=(int)i;item.pszText=const_cast<wchar_t*>(m.name.c_str());ListView_InsertItem(p->list,&item);
            ListView_SetItemText(p->list,(int)i,1,const_cast<wchar_t*>(m.Translation()?L"翻译":L"识别"));ListView_SetItemText(p->list,(int)i,2,const_cast<wchar_t*>(m.tier.c_str()));auto size=CaptionSize(m.DownloadBytes());ListView_SetItemText(p->list,(int)i,3,size.data());if(m.id==p->selection.recognition)p->selected=(int)i;}
        p->Child(L"STATIC",L"",SS_ENDELLIPSIS,DetailTitle);p->Child(L"STATIC",L"",0,Description);p->Child(L"STATIC",L"下载源",SS_CENTERIMAGE,SourceLabel);p->Child(L"STATIC",L"翻译为",SS_CENTERIMAGE,TargetLabel);
        p->Child(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,Source);p->Child(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,Target);
        for(int i=0;i<5;++i){SendDlgItemMessageW(hwnd,Target,CB_ADDSTRING,0,(LPARAM)CaptionLanguage(targets[i]).c_str());if(p->selection.target==targets[i])SendDlgItemMessageW(hwnd,Target,CB_SETCURSEL,i,0);}
        const wchar_t* buttons[]={L"下载安装",L"取消下载",L"用作识别",L"删除模型",L"来源与许可",L"仅显示原文",L"关闭"};
        for(int id:{Install,Use,CancelDownload,Remove,Origin,OriginalOnly,Close})p->Child(L"BUTTON",buttons[id-Install],WS_TABSTOP|BS_OWNERDRAW,id);
        HWND progress=p->Child(PROGRESS_CLASSW,L"",PBS_SMOOTH,Progress);SetWindowTheme(progress,L"",L"");
        SetWindowLongPtrW(progress,GWL_EXSTYLE,GetWindowLongPtrW(progress,GWL_EXSTYLE)&~(WS_EX_STATICEDGE|WS_EX_CLIENTEDGE));
        SetWindowLongPtrW(progress,GWL_STYLE,GetWindowLongPtrW(progress,GWL_STYLE)&~WS_BORDER);SetWindowPos(progress,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
        SendDlgItemMessageW(hwnd,Progress,PBM_SETRANGE32,0,100);
        SendDlgItemMessageW(hwnd,Progress,PBM_SETBKCOLOR,0,surface);SendDlgItemMessageW(hwnd,Progress,PBM_SETBARCOLOR,0,blue);
        p->Child(L"STATIC",L"首次安装需联网，安装后离线使用。下载不会自动启用模型。",0,Status);
        p->Fonts();MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);GetMonitorInfoW(MonitorFromWindow(GetWindow(hwnd,GW_OWNER),MONITOR_DEFAULTTONEAREST),&monitor);RECT work=monitor.rcWork,owner=work;
        if(HWND parent=GetWindow(hwnd,GW_OWNER))GetWindowRect(parent,&owner);
        const int w=std::min(p->S(880),int(work.right-work.left)-24),h=std::min(p->S(680),int(work.bottom-work.top)-32);
        const int x=std::clamp(int(owner.left+(owner.right-owner.left-w)/2),int(work.left),int(work.right)-w),y=std::clamp(int(owner.top+(owner.bottom-owner.top-h)/2),int(work.top),int(work.bottom)-h);
        SetWindowPos(hwnd,nullptr,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
        ListView_SetItemState(p->list,p->selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);p->Choose();p->Layout();ListView_EnsureVisible(p->list,p->selected,FALSE);SetFocus(p->list);return FALSE;
    }
    if(!p)return FALSE;
    switch(msg){
    case WM_GETMINMAXINFO:{RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);auto* info=(MINMAXINFO*)lp;info->ptMinTrackSize.x=std::min(p->S(580),int(area.right-area.left));info->ptMinTrackSize.y=std::min(p->S(450),int(area.bottom-area.top));return TRUE;}
    case WM_SIZE:p->Layout();return TRUE;
    case WM_DPICHANGED:{p->scale=float(HIWORD(wp))/96;p->Fonts();auto& r=*(RECT*)lp;SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);p->Layout();return TRUE;}
    case WM_TIMER:p->Refresh();return TRUE;
    case WM_NOTIFY:{const auto* h=(NMHDR*)lp;
        if(h->idFrom==ModelList&&h->code==NM_CUSTOMDRAW){SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,DrawRow(*p,*(NMLVCUSTOMDRAW*)lp));return TRUE;}
        if(h->idFrom==ModelList&&h->code==LVN_ITEMCHANGED){const auto* n=(NMLISTVIEW*)lp;if((n->uNewState&LVIS_SELECTED)&&!(n->uOldState&LVIS_SELECTED))p->Choose();}return FALSE;}
    case WM_MEASUREITEM:{auto& item=*(MEASUREITEMSTRUCT*)lp;if(item.CtlType==ODT_COMBOBOX){item.itemHeight=p->S(30);return TRUE;}break;}
    case WM_DRAWITEM:{auto& item=*(DRAWITEMSTRUCT*)lp;if(item.CtlType==ODT_BUTTON){DrawButton(*p,item);return TRUE;}
        if(item.CtlType==ODT_COMBOBOX){const int saved=SaveDC(item.hDC);const bool chosen=item.itemState&ODS_SELECTED;FlatFill(item.hDC,item.rcItem,chosen?RGB(38,57,87):surface);
            wchar_t text[512]{};if(item.itemID!=UINT(-1)&&SendMessageW(item.hwndItem,CB_GETLBTEXTLEN,item.itemID,0)<512)SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,(LPARAM)text);
            RECT r=item.rcItem;r.left+=p->S(10);r.right-=p->S(6);Label(item.hDC,text,r,p->font,ink);RestoreDC(item.hDC,saved);return TRUE;}break;}
    case WM_COMMAND:if(LOWORD(wp)==Close||LOWORD(wp)==IDCANCEL){EndDialog(hwnd,0);return TRUE;}
        if(LOWORD(wp)==Source&&HIWORD(wp)==CBN_SELCHANGE){p->SourceNotice();return TRUE;}
        if(LOWORD(wp)==Target&&HIWORD(wp)==CBN_SELCHANGE){const int i=(int)SendDlgItemMessageW(hwnd,Target,CB_GETCURSEL,0,0);if(i>=0&&i<5){p->selection.target=targets[i];p->apply(p->selection);p->Refresh();}return TRUE;}
        if(HIWORD(wp)==BN_CLICKED){p->Action(LOWORD(wp));return TRUE;}break;
    case WM_CTLCOLORDLG:return (INT_PTR)p->background;
    case WM_CTLCOLORSTATIC:{HDC dc=(HDC)wp;const int id=GetDlgCtrlID((HWND)lp);SetTextColor(dc,id==Title||id==DetailTitle?ink:muted);SetBkColor(dc,bg);return (INT_PTR)p->background;}
    case WM_CTLCOLORLISTBOX:{HDC dc=(HDC)wp;SetTextColor(dc,ink);SetBkColor(dc,surface);return (INT_PTR)p->field;}
    case WM_CLOSE:EndDialog(hwnd,0);return TRUE;
    case WM_DESTROY:KillTimer(hwnd,1);p->FreeDrawing();return TRUE;
    }return FALSE;
}
}
void ShowCaptionModelDialog(HWND owner,CaptionModelStore& store,CaptionSelection& selection,
    const std::function<void(const CaptionSelection&)>& apply,const std::function<bool(const std::string&)>& release){
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
    struct Empty {DLGTEMPLATE dialog;WORD menu=0,klass=0,title=0;} form{};
    form.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|DS_MODALFRAME;form.dialog.cx=480;form.dialog.cy=330;
    Dialog state{store,selection,apply,release};
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&form.dialog,owner,Proc,(LPARAM)&state);
    if(state.font)DeleteObject(state.font);if(state.background)DeleteObject(state.background);
}
