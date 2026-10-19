#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <condition_variable>
#include "collector.h"
#include "store.h"
#include "health.h"
#include "client.h"
#include "ipc.h"
#include "settings.h"
#include "update.h"
#include <wlanapi.h>
using namespace fc;
namespace {
constexpr UINT TRAY=WM_APP+1, TICK=WM_APP+2;
std::unique_ptr<ClientModel> model;
HWND window,list,search,netButton,diskButton,healthButton,periodBox,proxyBox,pauseButton,exportButton,closeButton,networkBox,currentButton,settingsButton,startCheck,updateCheck,speedCheck,diskSpeedCheck,permissionButton,repairButton,updateButton,healthRefreshButton,wifiCheck;
HFONT font;HICON icon;NOTIFYICONDATAW tray{};bool panel=false,dialogOpen=false;int page=0,period=0,sortColumn=1;bool descending=true;std::wstring selectedNetwork;std::vector<std::wstring> networkNames;
std::thread updateThread;std::atomic<bool> updateBusy{false};std::mutex updateMutex;Update offeredUpdate;std::wstring updateStatus=L"尚未检查更新",installerPath;bool manualCheck=false;
ID2D1Factory* d2d=nullptr;IDWriteFactory* dwrite=nullptr;ID2D1HwndRenderTarget* target=nullptr;
std::wstring since(){if(period==0)return dayKey();if(period==1)return dayKey(-1);if(period==2)return dayKey(-6);if(period==3)return dayKey(-29);return L"0000-00-00";}
std::wstring until(){return period==1?dayKey(-1):dayKey();}
void releaseTarget(){if(target){target->Release();target=nullptr;}}
void text(const std::wstring& value,float x,float y,float width,float height,float size,D2D1_COLOR_F color=D2D1::ColorF(0x233044),bool bold=false){
 IDWriteTextFormat* f=nullptr;ID2D1SolidColorBrush* b=nullptr;
 dwrite->CreateTextFormat(L"Segoe UI",nullptr,bold?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"zh-cn",&f);
 if(!f)return;f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);target->CreateSolidColorBrush(color,&b);if(b){target->DrawTextW(value.c_str(),static_cast<UINT32>(value.size()),f,D2D1::RectF(x,y,x+width,y+height),b,D2D1_DRAW_TEXT_OPTIONS_CLIP);b->Release();}f->Release();
}
void paint(){PAINTSTRUCT ps;BeginPaint(window,&ps);RECT rc;GetClientRect(window,&rc);
 if(!target)d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(rc.right,rc.bottom)),&target);
 if(target){target->BeginDraw();target->Clear(D2D1::ColorF(0xf5f7fa));std::lock_guard g(model->mutex);
  text(L"流量喵",24,20,200,30,20,D2D1::ColorF(0x233044),true);
  std::wstring status=!model->connected?L"服务未连接":model->snapshot.value("error",0UL)?L"采集失败 · "+std::to_wstring(model->snapshot.value("error",0UL)):model->paused()?L"已暂停":L"后台记录中";
  text(status,230,25,180,25,12,D2D1::ColorF(0x617086));
  if(page<2){
   auto rows=model->rows(page==1);Bytes total;
   if(page==0)total=model->physical();else for(auto& r:rows)total.merge(r.bytes);
   ID2D1SolidColorBrush* b=nullptr;target->CreateSolidColorBrush(D2D1::ColorF(0xffffff),&b);if(b){target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(20,164,500,294),12,12),b);b->Release();}
   text(page==0?L"网卡累计流量":L"进程磁盘累计读写",36,178,350,20,12,D2D1::ColorF(0x617086));
   text(model->loading?L"正在读取…":volume(total.total(page==1)),36,202,440,46,32,D2D1::ColorF(0x172231),true);
   text((page==0?L"↓ 下载  ":L"读取  ")+volume(page==0?total.down:total.read),36,258,230,22,13);
   text((page==0?L"↑ 上传  ":L"写入  ")+volume(page==0?total.up:total.write),275,258,220,22,13);

   auto r=model->rates();text(page==0?L"实时网卡  ↓ "+volume(r.down)+L"/s     ↑ "+volume(r.up)+L"/s":L"实时读写  读 "+volume(r.read)+L"/s     写 "+volume(r.write)+L"/s",24,308,480,25,13);
  }else if(page==2){
   auto health=model->health();
   text(L"磁盘自身报告的指标",24,116,470,25,16,D2D1::ColorF(0x233044),true);float y=160;
   if(health.empty())text(L"正在读取，或系统未提供可访问的磁盘。",24,y,470,30,13);
   for(auto& d:health){text(d.name,24,y,470,28,15,D2D1::ColorF(0x233044),true);y+=32;
    for(auto& field:std::vector<std::pair<std::wstring,std::wstring>>{{L"连接 / 状态",d.bus+L" / "+d.status},{L"温度",d.temperature},{L"耐久度已用",d.wear},{L"累计主机读取",d.read},{L"累计主机写入",d.write},{L"通电时间",d.hours},{L"介质错误",d.errors}}){text(field.first,24,y,180,24,13,D2D1::ColorF(0x617086));text(field.second,205,y,290,24,13);y+=29;}y+=22;if(y>580)break;
   }
   text(L"未提供 ≠ 0；耐久度已用不等于剩余寿命。",24,600,480,25,12,D2D1::ColorF(0x617086));
  }else{
   text(L"设置",24,122,450,30,21,D2D1::ColorF(0x233044),true);
   text(L"后台采集服务随系统启动；退出托盘不影响记录。",24,158,470,25,13,D2D1::ColorF(0x617086));
   text(L"网络识别",24,340,460,25,16,D2D1::ColorF(0x233044),true);
   text(L"关闭后只记入全部网络；已有历史仍保留。",24,405,470,23,12,D2D1::ColorF(0x617086));
   text(L"应用更新",24,450,460,25,16,D2D1::ColorF(0x233044),true);
   {std::lock_guard u(updateMutex);text(updateStatus,24,481,460,25,13);}
   text(std::wstring(L"流量喵 Windows ")+VersionW+L" · 本地统计 · MIT 开源",24,605,470,25,12,D2D1::ColorF(0x617086));
  }
  std::wstring foot;
  if(!model->connected)foot=model->error;
  else if(!model->snapshot.value("saved",true))foot=L"历史保存失败，已暂停采集";
  else if(model->snapshot.value("lost",0UL))foot=L"ETW 丢失事件："+std::to_wstring(model->snapshot.value("lost",0UL));
  else if(page==3)foot=L"关闭托盘后，后台仍会继续记录。";
  else foot=page==0?L"网卡总量独立统计；应用与代理不可相加。":L"运行后开始统计；进程归属可能不完整。";
  text(foot,24,static_cast<float>(rc.bottom-33),470,23,11,D2D1::ColorF(0x617086));
  if(target->EndDraw()==D2DERR_RECREATE_TARGET)releaseTarget();
 }EndPaint(window,&ps);
}
HWND control(const wchar_t* cls,const wchar_t* title,DWORD style,int x,int y,int w,int h,int id){HWND c=CreateWindowExW(0,cls,title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;}
void columns(){while(ListView_DeleteColumn(list,0));
 std::vector<std::pair<const wchar_t*,int>> cols={{L"进程",194},{page==0?L"总流量":L"写入",88},{page==0?L"下载":L"读取",88},{page==0?L"上传":L"总读写",88}};
 for(int i=0;i<4;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;c.pszText=const_cast<wchar_t*>(cols[i].first);c.cx=cols[i].second;c.fmt=i?LVCFMT_RIGHT:LVCFMT_LEFT;ListView_InsertColumn(list,i,&c);}
}
void updateList(){
 if(!IsWindowVisible(window)||page>=2)return;std::lock_guard g(model->mutex);
 auto rows=model->rows(page==1,page==0?static_cast<int>(SendMessageW(proxyBox,CB_GETCURSEL,0,0)):-1);

 auto metric=[](const Row& r){if(page==0)return sortColumn==2?r.bytes.down:sortColumn==3?r.bytes.up:r.bytes.total(false);return sortColumn==2?r.bytes.read:sortColumn==3?r.bytes.total(true):r.bytes.write;};
 std::sort(rows.begin(),rows.end(),[&](auto& a,auto& b){if(sortColumn==0)return descending?a.name>b.name:a.name<b.name;return descending?metric(a)>metric(b):metric(a)<metric(b);});
 wchar_t query[256];GetWindowTextW(search,query,256);std::wstring q=query;std::transform(q.begin(),q.end(),q.begin(),::towlower);
 SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);int i=0;
 for(auto& r:rows){std::wstring name=r.name;std::transform(name.begin(),name.end(),name.begin(),::towlower);if(!q.empty()&&name.find(q)==std::wstring::npos)continue;
  LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=r.name.data();ListView_InsertItem(list,&item);
  auto values=page==0?std::vector<uint64_t>{r.bytes.total(false),r.bytes.down,r.bytes.up}:std::vector<uint64_t>{r.bytes.write,r.bytes.read,r.bytes.total(true)};
  for(int c=1;c<=3;++c){auto v=volume(values[c-1]);ListView_SetItemText(list,i,c,v.data());}++i;
 }SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
}
void updateTray(){std::lock_guard g(model->mutex);std::wstring t=L"流量喵 · "+std::wstring(model->paused()?L"已暂停":L"记录中")+(preference(L"NetworkSpeed",true)?L"\n↓ "+volume(model->rates().down)+L"/s  ↑ "+volume(model->rates().up)+L"/s":L"")+(preference(L"DiskSpeed",true)?L"\n读 "+volume(model->rates().read)+L"/s  写 "+volume(model->rates().write)+L"/s":L"");wcsncpy_s(tray.szTip,t.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&tray);}
void refreshQuery(){model->query(since(),until(),page==0?selectedNetwork:L"");}
void switchPage(int p){page=p;bool stats=page<2;for(HWND c:{list,search,periodBox,exportButton})ShowWindow(c,stats?SW_SHOW:SW_HIDE);for(HWND c:{proxyBox,networkBox,currentButton})ShowWindow(c,page==0?SW_SHOW:SW_HIDE);ShowWindow(healthRefreshButton,page==2?SW_SHOW:SW_HIDE);for(HWND c:{startCheck,updateCheck,speedCheck,diskSpeedCheck,permissionButton,repairButton,updateButton,wifiCheck})ShowWindow(c,page==3?SW_SHOW:SW_HIDE);columns();refreshQuery();updateList();for(HWND c:{netButton,diskButton,healthButton,settingsButton})InvalidateRect(c,nullptr,TRUE);InvalidateRect(window,nullptr,FALSE);}
void updateNetworks(){std::vector<std::wstring> names{L"全部网络"};{std::lock_guard g(model->mutex);if(model->snapshot.contains("networks"))for(auto& n:model->snapshot["networks"])names.push_back(wide(n.get<std::string>()));SetWindowTextW(currentButton,(L"当前连接 · "+model->network()+L"  ↗").c_str());SetWindowTextW(pauseButton,model->paused()?L"继续记录":L"暂停记录");SendMessageW(wifiCheck,BM_SETCHECK,model->snapshot.value("identifyNetworks",true)?BST_CHECKED:BST_UNCHECKED,0);}if(names==networkNames)return;networkNames=names;SendMessageW(networkBox,CB_RESETCONTENT,0,0);int selected=0;for(size_t i=0;i<names.size();++i){SendMessageW(networkBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(names[i].c_str()));if(names[i]==selectedNetwork)selected=static_cast<int>(i);}SendMessageW(networkBox,CB_SETCURSEL,selected,0);}
void showPanel(){POINT cursor;GetCursorPos(&cursor);HMONITOR monitor=MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST);MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);int width=520,height=std::min(740L,info.rcWork.bottom-info.rcWork.top-16);int x=std::clamp(cursor.x-width+20,info.rcWork.left+8,info.rcWork.right-width-8);int y=info.rcWork.bottom-height-8;
 SetWindowPos(window,HWND_TOPMOST,x,y,width,height,SWP_SHOWWINDOW);SetForegroundWindow(window);panel=true;model->visible=true;refreshQuery();updateList();InvalidateRect(window,nullptr,FALSE);}
void exportCSV(){wchar_t path[MAX_PATH]=L"FlowCat.csv";OPENFILENAMEW of{sizeof(of)};of.hwndOwner=window;of.lpstrFilter=L"CSV 文件\0*.csv\0\0";of.lpstrFile=path;of.nMaxFile=MAX_PATH;of.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;of.lpstrDefExt=L"csv";dialogOpen=true;bool chosen=GetSaveFileNameW(&of)!=FALSE;dialogOpen=false;if(chosen){auto result=rpc({{"protocol",1},{"op","csv"},{"since",utf8(since())},{"until",utf8(until())},{"network",utf8(page==0?selectedNetwork:L"")}});bool good=result.value("ok",false);if(good){std::ofstream f(std::filesystem::path(path),std::ios::binary);auto csv=result.value("csv",std::string{});f.write(csv.data(),static_cast<std::streamsize>(csv.size()));good=f.good();}if(!good)MessageBoxW(window,L"导出失败，请检查服务状态和目标目录。",L"流量喵",MB_ICONERROR);}}
void requestWifi(){HANDLE client=nullptr;DWORD version=0;bool denied=false,readable=false;if(WlanOpenHandle(2,nullptr,&version,&client)==ERROR_SUCCESS){PWLAN_INTERFACE_INFO_LIST interfaces=nullptr;if(WlanEnumInterfaces(client,nullptr,&interfaces)==ERROR_SUCCESS){for(DWORD i=0;i<interfaces->dwNumberOfItems;++i){DWORD size=0;void* data=nullptr;WLAN_OPCODE_VALUE_TYPE type;auto code=WlanQueryInterface(client,&interfaces->InterfaceInfo[i].InterfaceGuid,wlan_intf_opcode_current_connection,nullptr,&size,&data,&type);if(code==ERROR_SUCCESS){readable=true;WlanFreeMemory(data);}else if(code==ERROR_ACCESS_DENIED)denied=true;}WlanFreeMemory(interfaces);}WlanCloseHandle(client,nullptr);}if(denied){rpc({{"protocol",1},{"op","wifi"},{"enabled",false}});selectedNetwork=L"";ShellExecuteW(window,L"open",L"ms-settings:privacy-location",nullptr,nullptr,SW_SHOWNORMAL);}else if(readable)rpc({{"protocol",1},{"op","wifi"},{"enabled",true}});refreshQuery();}
void beginUpdate(bool install,bool manual){if(updateBusy.exchange(true))return;if(updateThread.joinable())updateThread.join();manualCheck=manual;{std::lock_guard g(updateMutex);updateStatus=install?L"正在下载并验证更新…":L"正在检查更新…";}updateThread=std::thread([install]{try{if(install){Update update;{std::lock_guard g(updateMutex);update=offeredUpdate;}auto path=downloadUpdate(update);{std::lock_guard g(updateMutex);installerPath=path;}PostMessageW(window,WM_APP+4,0,0);}else{auto update=checkUpdate();{std::lock_guard g(updateMutex);offeredUpdate=update;updateStatus=update.available?L"可更新至 "+wide(update.version):L"已经是最新版本";}PostMessageW(window,WM_APP+3,0,0);}}catch(...){std::lock_guard g(updateMutex);updateStatus=L"更新失败，请检查网络后重试";PostMessageW(window,WM_APP+3,0,0);}updateBusy=false;});InvalidateRect(window,nullptr,FALSE);}
void togglePause(){bool paused;{std::lock_guard g(model->mutex);paused=model->paused();}model->pause(!paused);InvalidateRect(window,nullptr,FALSE);updateTray();}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){
 case WM_CREATE:{window=h;font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  netButton=control(L"BUTTON",L"网络",BS_OWNERDRAW,20,66,110,34,101);diskButton=control(L"BUTTON",L"磁盘读写",BS_OWNERDRAW,138,66,110,34,102);healthButton=control(L"BUTTON",L"磁盘状态",BS_OWNERDRAW,256,66,110,34,103);
  pauseButton=control(L"BUTTON",L"暂停记录",BS_OWNERDRAW,380,66,120,34,104);
  periodBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,20,112,136,200,105);for(auto s:{L"今日",L"昨日",L"近 7 天",L"近 30 天",L"累计"})SendMessageW(periodBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));SendMessageW(periodBox,CB_SETCURSEL,0,0);
  proxyBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,364,112,136,120,106);for(auto s:{L"应用排行",L"代理排行"})SendMessageW(proxyBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));SendMessageW(proxyBox,CB_SETCURSEL,0,0);
  search=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,20,344,360,30,107);SendMessageW(search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"搜索进程"));
  exportButton=control(L"BUTTON",L"导出 CSV",BS_OWNERDRAW,390,344,110,30,108);
  settingsButton=control(L"BUTTON",L"设置",BS_OWNERDRAW,424,18,76,28,110);
  networkBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,174,112,178,220,111);SendMessageW(networkBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"全部网络"));SendMessageW(networkBox,CB_SETCURSEL,0,0);
  currentButton=control(L"BUTTON",L"当前连接",BS_OWNERDRAW,20,140,480,20,112);
  startCheck=control(L"BUTTON",L"登录时显示托盘图标",BS_AUTOCHECKBOX,24,204,460,28,301);SendMessageW(startCheck,BM_SETCHECK,autoStart()?BST_CHECKED:BST_UNCHECKED,0);
  updateCheck=control(L"BUTTON",L"每天自动检查更新",BS_AUTOCHECKBOX,24,243,460,28,302);SendMessageW(updateCheck,BM_SETCHECK,preference(L"AutoUpdate",true)?BST_CHECKED:BST_UNCHECKED,0);
  speedCheck=control(L"BUTTON",L"托盘提示显示网速",BS_AUTOCHECKBOX,24,282,228,28,303);SendMessageW(speedCheck,BM_SETCHECK,preference(L"NetworkSpeed",true)?BST_CHECKED:BST_UNCHECKED,0);
  diskSpeedCheck=control(L"BUTTON",L"托盘提示显示磁盘读写",BS_AUTOCHECKBOX,260,282,240,28,304);SendMessageW(diskSpeedCheck,BM_SETCHECK,preference(L"DiskSpeed",true)?BST_CHECKED:BST_UNCHECKED,0);
  wifiCheck=control(L"BUTTON",L"识别 Wi-Fi 名称并分别记录",BS_AUTOCHECKBOX,24,365,270,28,309);SendMessageW(wifiCheck,BM_SETCHECK,BST_CHECKED,0);
  permissionButton=control(L"BUTTON",L"授权 / 位置设置",BS_OWNERDRAW,306,365,194,28,305);
  repairButton=control(L"BUTTON",L"启动采集服务",BS_OWNERDRAW,306,173,194,26,306);
  updateButton=control(L"BUTTON",L"检查更新",BS_OWNERDRAW,24,521,260,34,307);
  healthRefreshButton=control(L"BUTTON",L"刷新",BS_OWNERDRAW,400,114,100,28,308);
  list=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,20,386,480,280,109);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);SetWindowTheme(list,L"Explorer",nullptr);HIMAGELIST rows=ImageList_Create(1,27,ILC_COLOR32,1,1);ListView_SetImageList(list,rows,LVSIL_SMALL);columns();return 0;}
 case WM_DRAWITEM:{auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item->CtlType!=ODT_BUTTON)return DefWindowProcW(h,m,w,l);bool chosen=(item->CtlID==101&&page==0)||(item->CtlID==102&&page==1)||(item->CtlID==103&&page==2);bool pressed=(item->itemState&ODS_SELECTED)!=0;RECT r=item->rcItem;HBRUSH bg=CreateSolidBrush(pressed?RGB(224,232,243):chosen?RGB(255,255,255):RGB(239,242,247));FillRect(item->hDC,&r,bg);DeleteObject(bg);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,chosen?RGB(38,99,235):RGB(55,67,85));SelectObject(item->hDC,font);wchar_t label[64];GetWindowTextW(item->hwndItem,label,64);DrawTextW(item->hDC,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(chosen){RECT line=r;line.top=line.bottom-2;HBRUSH accent=CreateSolidBrush(RGB(38,99,235));FillRect(item->hDC,&line,accent);DeleteObject(accent);}if(item->itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(item->hDC,&r);}return TRUE;}
 case WM_PAINT:paint();return 0;
 case WM_SIZE:releaseTarget();{RECT r;GetClientRect(h,&r);MoveWindow(list,20,386,std::max(1L,r.right-40),std::max(40L,r.bottom-430),TRUE);}return 0;
 case WM_ERASEBKGND:return 1;
 case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE&&panel&&!dialogOpen){ShowWindow(h,SW_HIDE);panel=false;model->visible=false;}return 0;
 case WM_QUERYENDSESSION:return TRUE;
 case WM_ENDSESSION:if(w)PostQuitMessage(0);return 0;
 case WM_CLOSE:ShowWindow(h,SW_HIDE);panel=false;model->visible=false;return 0;
 case WM_COMMAND:{int id=LOWORD(w);if(id==101)switchPage(0);if(id==102)switchPage(1);if(id==103)switchPage(2);if(id==110||id==204)switchPage(3);if(id==104)togglePause();if(id==105&&HIWORD(w)==CBN_SELCHANGE){period=static_cast<int>(SendMessageW(periodBox,CB_GETCURSEL,0,0));refreshQuery();updateList();InvalidateRect(h,nullptr,FALSE);}if(id==106&&HIWORD(w)==CBN_SELCHANGE)updateList();if(id==107&&HIWORD(w)==EN_CHANGE)updateList();if(id==108)exportCSV();
 if(id==111&&HIWORD(w)==CBN_SELCHANGE){auto index=SendMessageW(networkBox,CB_GETCURSEL,0,0);if(index>=0&&static_cast<size_t>(index)<networkNames.size()){selectedNetwork=index==0?L"":networkNames[index];refreshQuery();}}
 if(id==112){std::wstring n;{std::lock_guard g(model->mutex);n=model->network();}if(!n.empty()&&n!=L"全部网络"){selectedNetwork=n;auto i=std::find(networkNames.begin(),networkNames.end(),n);if(i!=networkNames.end())SendMessageW(networkBox,CB_SETCURSEL,i-networkNames.begin(),0);refreshQuery();}}
 if(id==301){if(!setAutoStart(SendMessageW(startCheck,BM_GETCHECK,0,0)==BST_CHECKED))MessageBoxW(h,L"无法修改登录启动设置。",L"流量喵",MB_ICONERROR);}
 if(id==302)setPreference(L"AutoUpdate",SendMessageW(updateCheck,BM_GETCHECK,0,0)==BST_CHECKED);
 if(id==303)setPreference(L"NetworkSpeed",SendMessageW(speedCheck,BM_GETCHECK,0,0)==BST_CHECKED);
 if(id==304)setPreference(L"DiskSpeed",SendMessageW(diskSpeedCheck,BM_GETCHECK,0,0)==BST_CHECKED);
 if(id==305)requestWifi();
 if(id==306){auto path=std::filesystem::path(executablePath()).parent_path()/L"FlowCatService.exe";ShellExecuteW(h,L"runas",path.c_str(),L"--start-service",nullptr,SW_SHOWNORMAL);}
 if(id==307||id==205){bool install;{std::lock_guard g(updateMutex);install=offeredUpdate.available;}beginUpdate(install,true);}
 if(id==308)model->refreshHealth();if(id==309){bool enabled=SendMessageW(wifiCheck,BM_GETCHECK,0,0)==BST_CHECKED;if(enabled)requestWifi();else{rpc({{"protocol",1},{"op","wifi"},{"enabled",false}});selectedNetwork=L"";SendMessageW(networkBox,CB_SETCURSEL,0,0);refreshQuery();}}if(id==201)showPanel();if(id==202)togglePause();if(id==203)PostQuitMessage(0);return 0;}
 case WM_NOTIFY:{auto* n=reinterpret_cast<NMHDR*>(l);if(n->hwndFrom==list&&n->code==LVN_COLUMNCLICK){int col=reinterpret_cast<NMLISTVIEW*>(l)->iSubItem;if(sortColumn==col)descending=!descending;else{sortColumn=col;descending=true;}updateList();}return 0;}
 case TRAY:if(LOWORD(l)==WM_LBUTTONUP||LOWORD(l)==NIN_SELECT){if(IsWindowVisible(h)){ShowWindow(h,SW_HIDE);panel=false;model->visible=false;}else showPanel();}else if(LOWORD(l)==WM_RBUTTONUP||LOWORD(l)==WM_CONTEXTMENU){bool paused;{std::lock_guard g(model->mutex);paused=model->paused();}HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,201,L"打开流量喵");AppendMenuW(menu,MF_STRING,202,paused?L"继续记录":L"暂停记录");AppendMenuW(menu,MF_STRING,204,L"设置");AppendMenuW(menu,MF_STRING,205,L"检查更新");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,203,L"退出托盘（继续记录）");POINT p;GetCursorPos(&p);SetForegroundWindow(h);TrackPopupMenu(menu,TPM_RIGHTBUTTON,p.x,p.y,0,h,nullptr);DestroyMenu(menu);PostMessageW(h,WM_NULL,0,0);}return 0;
 case TICK:updateNetworks();updateTray();if(IsWindowVisible(h)){updateList();InvalidateRect(h,nullptr,FALSE);}return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;
 case WM_APP+3:{Update u;{std::lock_guard g(updateMutex);u=offeredUpdate;}SetWindowTextW(updateButton,u.available?(L"安装 "+wide(u.version)+L" 并重启").c_str():L"检查更新");if(manualCheck){switchPage(3);showPanel();}InvalidateRect(h,nullptr,FALSE);return 0;}
 case WM_APP+4:{std::wstring path;{std::lock_guard g(updateMutex);path=installerPath;}Update u;{std::lock_guard g(updateMutex);u=offeredUpdate;}if(launchUpdate(path,u))PostQuitMessage(0);else{std::lock_guard g(updateMutex);updateStatus=L"安装未启动，可稍后重试";}return 0;}
 case WM_TIMER:if(w==1&&preference(L"AutoUpdate",true))beginUpdate(false,false);return 0;
 default:return DefWindowProcW(h,m,w,l);
 }}
HICON catIcon(){
 constexpr int n=32;BITMAPV5HEADER bi{};bi.bV5Size=sizeof(bi);bi.bV5Width=n;bi.bV5Height=-n;bi.bV5Planes=1;bi.bV5BitCount=32;bi.bV5Compression=BI_BITFIELDS;bi.bV5RedMask=0xff0000;bi.bV5GreenMask=0xff00;bi.bV5BlueMask=0xff;bi.bV5AlphaMask=0xff000000;
 void* pixels=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP color=CreateDIBSection(dc,reinterpret_cast<BITMAPINFO*>(&bi),DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(dc,color);memset(pixels,0,n*n*4);HPEN pen=CreatePen(PS_SOLID,2,RGB(38,99,235));auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));POINT points[]={{7,14},{6,5},{13,10},{19,10},{26,5},{25,14},{28,19},{25,25},{20,28},{12,28},{7,25},{4,19},{7,14}};Polyline(dc,points,13);for(int x=11;x<=21;x+=5){MoveToEx(dc,x,22,nullptr);LineTo(dc,x,22-(x-6)/2);}
 auto* p=static_cast<DWORD*>(pixels);for(int i=0;i<n*n;++i)if(p[i]&0xffffff)p[i]|=0xff000000;
 SelectObject(dc,oldBrush);SelectObject(dc,oldPen);SelectObject(dc,old);DeleteObject(pen);DeleteDC(dc);HBITMAP mask=CreateBitmap(n,n,1,1,nullptr);ICONINFO ii{};ii.fIcon=TRUE;ii.hbmColor=color;ii.hbmMask=mask;HICON result=CreateIconIndirect(&ii);DeleteObject(color);DeleteObject(mask);return result;
}

}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool show=false;int initialPage=0;
 for(int i=1;i<argc;++i){std::wstring arg=argv[i];if(arg==L"--show")show=true;else if(arg==L"--page"&&i+1<argc)initialPage=std::clamp(_wtoi(argv[++i]),0,3);}LocalFree(argv);
 HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\FlowCat.Windows");if(GetLastError()==ERROR_ALREADY_EXISTS){if(singleton)CloseHandle(singleton);return 0;}
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
 D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&d2d);DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&dwrite));if(!d2d||!dwrite)return 3;
 initializePreferences();model=std::make_unique<ClientModel>();icon=catIcon();WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"FlowCatWindowsPanel";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=icon;RegisterClassW(&wc);
 window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"流量喵 Windows",WS_POPUP|WS_BORDER,0,0,520,740,nullptr,nullptr,instance,nullptr);
 tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;tray.uCallbackMessage=TRAY;tray.hIcon=icon;wcscpy_s(tray.szTip,L"流量喵 · 正在启动");Shell_NotifyIconW(NIM_ADD,&tray);tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);
 model->start(window);SetTimer(window,1,24*60*60*1000,nullptr);if(preference(L"AutoUpdate",true))beginUpdate(false,false);switchPage(initialPage);if(show)showPanel();MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 model->stop();if(updateThread.joinable())updateThread.join();Shell_NotifyIconW(NIM_DELETE,&tray);model.reset();releaseTarget();dwrite->Release();d2d->Release();DestroyIcon(icon);DeleteObject(font);CoUninitialize();if(singleton)CloseHandle(singleton);return 0;
}
