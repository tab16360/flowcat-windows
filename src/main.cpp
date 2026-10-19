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
HWND appButton,proxyButton,sortBox;int filterMode=0;std::vector<uint64_t> listTotals;
HWND window,list,search,netButton,diskButton,healthButton,pauseButton,exportButton,closeButton,networkBox,currentButton,settingsButton,startCheck,updateCheck,permissionButton,repairButton,updateButton,healthRefreshButton,wifiCheck;
HFONT font,strongFont;HICON icon;NOTIFYICONDATAW tray{};bool panel=false,dialogOpen=false,exitRequested=false;UINT taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");ULONGLONG lastActivation=0;HWND periodButtons[5]{};HWND ioButton;std::wstring actionStatus=L"";std::thread serviceThread;std::atomic<bool> serviceBusy{false};int page=0,period=0,networkPeriod=0,diskPeriod=0,sortColumn=1;bool descending=true;std::wstring selectedNetwork;std::vector<std::wstring> networkNames;
std::thread updateThread;std::atomic<bool> updateBusy{false};std::mutex updateMutex;Update offeredUpdate;std::wstring updateStatus=L"尚未检查更新",installerPath;bool manualCheck=false;
ID2D1Factory* d2d=nullptr;IDWriteFactory* dwrite=nullptr;ID2D1HwndRenderTarget* target=nullptr;
std::wstring since(){if(period==0)return dayKey();if(period==1)return dayKey(-1);if(period==2)return dayKey(-6);if(period==3)return dayKey(-29);if(period==5)return dayKey().substr(0,8)+L"01";return L"0000-00-00";}
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
  text(L"流量喵",56,20,200,30,20,D2D1::ColorF(0x233044),true);
  std::wstring status=!model->connected?L"服务未连接":model->snapshot.value("error",0UL)?L"采集失败 · "+std::to_wstring(model->snapshot.value("error",0UL)):model->paused()?L"已暂停":L"后台记录中";
  text(status,230,25,180,25,12,D2D1::ColorF(0x617086));
  if(page<2){
   auto rows=model->rows(page==1);Bytes total;
   if(page==0)total=model->physical();else for(auto& r:rows)total.merge(r.bytes);
   ID2D1SolidColorBrush* b=nullptr;target->CreateSolidColorBrush(D2D1::ColorF(0xffffff),&b);if(b){target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(20,164,500,294),12,12),b);b->Release();}
   text(page==0?(period==0?L"今日累计":period==1?L"昨日累计":L"本月累计"):L"进程磁盘累计读写",36,178,350,20,12,D2D1::ColorF(0x617086));
   text(model->loading?L"正在读取…":volume(total.total(page==1)),36,202,440,46,32,D2D1::ColorF(0x172231),true);
   text((page==0?L"↓ 下载  ":L"读取  ")+volume(page==0?total.down:total.read),36,258,230,22,13);
   text((page==0?L"↑ 上传  ":L"写入  ")+volume(page==0?total.up:total.write),275,258,220,22,13);

   text(page==0?L"应用排行":L"进程排行",24,350,260,24,13,D2D1::ColorF(0x233044),true);
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
   text(L"采集服务",24,158,470,25,13,D2D1::ColorF(0x617086));
   text(actionStatus,24,288,470,24,12,D2D1::ColorF(0x617086));
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
  else if(page==3)foot=L"完全退出会保存统计并停止后台采集。";
  else foot=page==0?L"网卡总量独立统计；应用与代理不可相加。":L"运行后开始统计；进程归属可能不完整。";
  text(foot,24,static_cast<float>(rc.bottom-33),350,23,11,D2D1::ColorF(0x617086));
  if(target->EndDraw()==D2DERR_RECREATE_TARGET)releaseTarget();
 }DrawIconEx(ps.hdc,24,23,icon,22,22,0,nullptr,DI_NORMAL);EndPaint(window,&ps);
}
HWND control(const wchar_t* cls,const wchar_t* title,DWORD style,int x,int y,int w,int h,int id){HWND c=CreateWindowExW(0,cls,title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;}
void columns(){while(ListView_DeleteColumn(list,0));std::vector<std::pair<const wchar_t*,int>> cols={{L"进程",194},{page==0?L"总流量":L"写入",88},{page==0?L"下载":L"读取",88},{page==0?L"上传":L"总读写",88}};for(int i=0;i<4;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(cols[i].first);c.cx=cols[i].second;ListView_InsertColumn(list,i,&c);}}
void updateList(){
 if(!IsWindowVisible(window)||page>=2)return;std::lock_guard g(model->mutex);
 auto rows=model->rows(page==1,page==0?filterMode:-1);

 auto metric=[](const Row& r){if(page==0)return sortColumn==2?r.bytes.down:sortColumn==3?r.bytes.up:r.bytes.total(false);return sortColumn==2?r.bytes.read:sortColumn==3?r.bytes.total(true):r.bytes.write;};
 std::sort(rows.begin(),rows.end(),[&](auto& a,auto& b){if(sortColumn==0)return descending?a.name>b.name:a.name<b.name;return descending?metric(a)>metric(b):metric(a)<metric(b);});
 wchar_t query[256];GetWindowTextW(search,query,256);std::wstring q=query;std::transform(q.begin(),q.end(),q.begin(),::towlower);
 SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);listTotals.clear();int i=0;
 for(auto& r:rows){std::wstring name=r.name;std::transform(name.begin(),name.end(),name.begin(),::towlower);if(!q.empty()&&name.find(q)==std::wstring::npos)continue;
  LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=r.name.data();ListView_InsertItem(list,&item);
  auto values=page==0?std::vector<uint64_t>{r.bytes.total(false),r.bytes.down,r.bytes.up}:std::vector<uint64_t>{r.bytes.write,r.bytes.read,r.bytes.total(true)};
  listTotals.push_back(values[0]);for(int c=1;c<=3;++c){auto v=volume(values[c-1]);ListView_SetItemText(list,i,c,v.data());}++i;
 }SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
}
void updateTray(){std::lock_guard g(model->mutex);std::wstring t=L"流量喵 · "+std::wstring(!model->connected?L"服务未连接":model->paused()?L"已暂停":L"记录中")+L"\n↓ "+volume(model->rates().down)+L"/s  ↑ "+volume(model->rates().up)+L"/s";wcsncpy_s(tray.szTip,t.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&tray);}
void refreshQuery(){model->query(since(),until(),page==0?selectedNetwork:L"");}
void switchPage(int p){if(page==0)networkPeriod=period;else if(page==1)diskPeriod=period;page=p;period=page==0?networkPeriod:diskPeriod;bool stats=page<2;for(HWND c:{list,search,exportButton,sortBox})ShowWindow(c,stats?SW_SHOW:SW_HIDE);for(HWND c:{appButton,proxyButton,networkBox,currentButton})ShowWindow(c,page==0?SW_SHOW:SW_HIDE);ShowWindow(healthRefreshButton,page==2?SW_SHOW:SW_HIDE);ShowWindow(ioButton,page==1||page==2?SW_SHOW:SW_HIDE);ShowWindow(healthButton,page==1||page==2?SW_SHOW:SW_HIDE);for(int i=0;i<5;++i){const wchar_t* labels[]={L"今日",page==0?L"昨日":L"7 天",page==0?L"本月":L"30 天",L"累计",L""};SetWindowTextW(periodButtons[i],labels[i]);ShowWindow(periodButtons[i],stats&&i<(page==0?3:4)?SW_SHOW:SW_HIDE);InvalidateRect(periodButtons[i],nullptr,TRUE);}for(HWND c:{startCheck,updateCheck,permissionButton,repairButton,updateButton,wifiCheck})ShowWindow(c,page==3?SW_SHOW:SW_HIDE);SendMessageW(sortBox,CB_RESETCONTENT,0,0);for(auto label:page==0?std::vector<const wchar_t*>{L"总流量 ↓",L"下载 ↓",L"上传 ↓"}:std::vector<const wchar_t*>{L"写入 ↓",L"读取 ↓",L"总读写 ↓"})SendMessageW(sortBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));sortColumn=1;descending=true;SendMessageW(sortBox,CB_SETCURSEL,0,0);columns();refreshQuery();updateList();for(HWND c:{netButton,diskButton,healthButton,ioButton,settingsButton})InvalidateRect(c,nullptr,TRUE);InvalidateRect(window,nullptr,FALSE);}
void updateNetworks(){std::vector<std::wstring> names{L"全部网络"};{std::lock_guard g(model->mutex);if(model->snapshot.contains("networks"))for(auto& n:model->snapshot["networks"])names.push_back(wide(n.get<std::string>()));SetWindowTextW(currentButton,(L"当前连接 · "+model->network()+L"  ↗").c_str());SetWindowTextW(pauseButton,model->paused()?L"继续":L"暂停");if(!serviceBusy){SetWindowTextW(repairButton,model->connected?L"服务运行中":L"启动采集服务");EnableWindow(repairButton,!model->connected);} SendMessageW(wifiCheck,BM_SETCHECK,model->snapshot.value("identifyNetworks",true)?BST_CHECKED:BST_UNCHECKED,0);}if(names==networkNames)return;networkNames=names;SendMessageW(networkBox,CB_RESETCONTENT,0,0);int selected=0;for(size_t i=0;i<names.size();++i){SendMessageW(networkBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(names[i].c_str()));if(names[i]==selectedNetwork)selected=static_cast<int>(i);}SendMessageW(networkBox,CB_SETCURSEL,selected,0);}
void showPanel(){POINT cursor;GetCursorPos(&cursor);HMONITOR monitor=MonitorFromPoint(cursor,MONITOR_DEFAULTTONEAREST);MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);int width=520,height=std::min(740L,info.rcWork.bottom-info.rcWork.top-16);int x=std::clamp(cursor.x-width+20,info.rcWork.left+8,info.rcWork.right-width-8);int y=info.rcWork.bottom-height-8;
 SetWindowPos(window,HWND_TOPMOST,x,y,width,height,SWP_SHOWWINDOW);SetForegroundWindow(window);panel=true;model->visible=true;refreshQuery();updateList();InvalidateRect(window,nullptr,FALSE);}
void exportCSV(){wchar_t path[MAX_PATH]=L"FlowCat.csv";OPENFILENAMEW of{sizeof(of)};of.hwndOwner=window;of.lpstrFilter=L"CSV 文件\0*.csv\0\0";of.lpstrFile=path;of.nMaxFile=MAX_PATH;of.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;of.lpstrDefExt=L"csv";dialogOpen=true;bool chosen=GetSaveFileNameW(&of)!=FALSE;dialogOpen=false;if(chosen){auto result=rpc({{"protocol",1},{"op","csv"},{"since",utf8(since())},{"until",utf8(until())},{"network",utf8(page==0?selectedNetwork:L"")}});bool good=result.value("ok",false);if(good){std::ofstream f(std::filesystem::path(path),std::ios::binary);auto csv=result.value("csv",std::string{});f.write(csv.data(),static_cast<std::streamsize>(csv.size()));good=f.good();}if(!good)MessageBoxW(window,L"导出失败，请检查服务状态和目标目录。",L"流量喵",MB_ICONERROR);}}
void requestWifi(){auto result=rpc({{"protocol",1},{"op","wifi"},{"enabled",true}});actionStatus=result.value("ok",false)?L"已开启网络识别；系统不允许时显示全部网络。":L"服务未连接，请先启动采集服务。";refreshQuery();InvalidateRect(window,nullptr,FALSE);}
void serviceAction(bool stop){if(serviceBusy.exchange(true)){if(stop)exitRequested=true;return;}if(serviceThread.joinable())serviceThread.join();actionStatus=stop?L"正在保存统计并停止服务…":L"正在启动采集服务…";EnableWindow(repairButton,FALSE);dialogOpen=true;serviceThread=std::thread([stop]{DWORD error=0;SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);SC_HANDLE svc=manager?OpenServiceW(manager,ServiceName,SERVICE_QUERY_STATUS|(stop?SERVICE_STOP:SERVICE_START)):nullptr;if(!svc)error=GetLastError();else{SERVICE_STATUS_PROCESS status{};DWORD size=0;if(!QueryServiceStatusEx(svc,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&size))error=GetLastError();else if(stop&&status.dwCurrentState!=SERVICE_STOPPED){SERVICE_STATUS ignored{};if(!ControlService(svc,SERVICE_CONTROL_STOP,&ignored))error=GetLastError();}else if(!stop&&status.dwCurrentState==SERVICE_STOPPED&&!StartServiceW(svc,0,nullptr))error=GetLastError();if(!error){bool ready=false;for(int i=0;i<150;++i){if(QueryServiceStatusEx(svc,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&size)&&status.dwCurrentState==(stop?SERVICE_STOPPED:SERVICE_RUNNING)){ready=true;break;}Sleep(200);}if(!ready)error=ERROR_SERVICE_REQUEST_TIMEOUT;}CloseServiceHandle(svc);}if(manager)CloseServiceHandle(manager);PostMessageW(window,WM_APP+5,error,stop);});InvalidateRect(window,nullptr,FALSE);}
void beginUpdate(bool install,bool manual){if(updateBusy.exchange(true))return;if(updateThread.joinable())updateThread.join();manualCheck=manual;{std::lock_guard g(updateMutex);updateStatus=install?L"正在下载并验证更新…":L"正在检查更新…";}updateThread=std::thread([install]{try{if(install){Update update;{std::lock_guard g(updateMutex);update=offeredUpdate;}auto path=downloadUpdate(update);{std::lock_guard g(updateMutex);installerPath=path;}PostMessageW(window,WM_APP+4,0,0);}else{auto update=checkUpdate();{std::lock_guard g(updateMutex);offeredUpdate=update;updateStatus=update.available?L"可更新至 "+wide(update.version):L"已经是最新版本";}PostMessageW(window,WM_APP+3,0,0);}}catch(...){std::lock_guard g(updateMutex);updateStatus=L"更新失败，请检查网络后重试";PostMessageW(window,WM_APP+3,0,0);}updateBusy=false;});InvalidateRect(window,nullptr,FALSE);}
void togglePause(){bool paused;{std::lock_guard g(model->mutex);paused=model->paused();}model->pause(!paused);InvalidateRect(window,nullptr,FALSE);updateTray();}
LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){
 case WM_CREATE:{window=h;strongFont=CreateFontW(-14,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  netButton=control(L"BUTTON",L"网络",BS_OWNERDRAW,20,66,110,34,101);diskButton=control(L"BUTTON",L"磁盘",BS_OWNERDRAW,138,66,90,34,102);ioButton=control(L"BUTTON",L"读写统计",BS_OWNERDRAW,282,66,108,34,113);healthButton=control(L"BUTTON",L"磁盘状态",BS_OWNERDRAW,392,66,108,34,103);
  pauseButton=control(L"BUTTON",L"暂停",BS_OWNERDRAW,344,18,72,28,104);
  for(int i=0;i<5;++i){const wchar_t* labels[]={L"今日",L"昨日",L"7 天",L"30 天",L"累计"};periodButtons[i]=control(L"BUTTON",labels[i],BS_OWNERDRAW,20+i*56,112,56,30,120+i);}
  appButton=control(L"BUTTON",L"应用",BS_OWNERDRAW,118,344,58,28,130);proxyButton=control(L"BUTTON",L"代理",BS_OWNERDRAW,178,344,58,28,131);
  sortBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,358,344,142,120,114);
  search=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,20,382,480,30,107);SendMessageW(search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"搜索进程"));
  exportButton=control(L"BUTTON",L"导出 CSV",BS_OWNERDRAW,390,696,110,26,108);
  settingsButton=control(L"BUTTON",L"设置",BS_OWNERDRAW,424,18,76,28,110);
  networkBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,318,112,182,220,111);SendMessageW(networkBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"全部网络"));SendMessageW(networkBox,CB_SETCURSEL,0,0);
  currentButton=control(L"BUTTON",L"当前连接",BS_OWNERDRAW,224,144,276,18,112);
  startCheck=control(L"BUTTON",L"登录时显示托盘图标",BS_AUTOCHECKBOX,24,204,460,28,301);SendMessageW(startCheck,BM_SETCHECK,autoStart()?BST_CHECKED:BST_UNCHECKED,0);
  updateCheck=control(L"BUTTON",L"每天自动检查更新",BS_AUTOCHECKBOX,24,243,460,28,302);SendMessageW(updateCheck,BM_SETCHECK,preference(L"AutoUpdate",true)?BST_CHECKED:BST_UNCHECKED,0);
  wifiCheck=control(L"BUTTON",L"识别 Wi-Fi 名称并分别记录",BS_AUTOCHECKBOX,24,365,270,28,309);SendMessageW(wifiCheck,BM_SETCHECK,BST_CHECKED,0);
  permissionButton=control(L"BUTTON",L"系统位置设置",BS_OWNERDRAW,306,365,194,28,305);
  repairButton=control(L"BUTTON",L"启动采集服务",BS_OWNERDRAW,306,158,194,28,306);
  updateButton=control(L"BUTTON",L"检查更新",BS_OWNERDRAW,24,521,260,34,307);
  healthRefreshButton=control(L"BUTTON",L"刷新",BS_OWNERDRAW,400,114,100,28,308);
  list=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOCOLUMNHEADER,20,428,480,250,109);ListView_SetBkColor(list,RGB(245,247,250));ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);SetWindowTheme(list,L"Explorer",nullptr);HIMAGELIST rows=ImageList_Create(1,58,ILC_COLOR32,1,1);ListView_SetImageList(list,rows,LVSIL_SMALL);columns();return 0;}
 case WM_DRAWITEM:{auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item->CtlType!=ODT_BUTTON)return DefWindowProcW(h,m,w,l);bool nav=item->CtlID==101||item->CtlID==102;bool chosen=(item->CtlID==101&&page==0)||(item->CtlID==102&&(page==1||page==2))||(item->CtlID==113&&page==1)||(item->CtlID==103&&page==2)||(item->CtlID==130&&filterMode==0)||(item->CtlID==131&&filterMode==1)||(item->CtlID>=120&&item->CtlID<=124&&period==(page==0?(item->CtlID==122?5:item->CtlID-120):(item->CtlID==120?0:item->CtlID-119)));bool pressed=(item->itemState&ODS_SELECTED)!=0;RECT r=item->rcItem;HBRUSH base=CreateSolidBrush(RGB(245,247,250));FillRect(item->hDC,&r,base);DeleteObject(base);HBRUSH bg=CreateSolidBrush(pressed?RGB(224,232,243):chosen&&!nav?RGB(255,255,255):RGB(245,247,250));auto oldBrush=SelectObject(item->hDC,bg);auto oldPen=SelectObject(item->hDC,GetStockObject(NULL_PEN));RoundRect(item->hDC,r.left,r.top,r.right,r.bottom,10,10);SelectObject(item->hDC,oldBrush);SelectObject(item->hDC,oldPen);DeleteObject(bg);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,chosen?RGB(38,99,235):RGB(55,67,85));SelectObject(item->hDC,font);wchar_t label[64];GetWindowTextW(item->hwndItem,label,64);DrawTextW(item->hDC,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(chosen&&nav){RECT line=r;line.top=line.bottom-2;HBRUSH accent=CreateSolidBrush(RGB(38,99,235));FillRect(item->hDC,&line,accent);DeleteObject(accent);}if(item->itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(item->hDC,&r);}return TRUE;}
 case WM_PAINT:paint();return 0;
 case WM_SIZE:releaseTarget();{RECT r;GetClientRect(h,&r);MoveWindow(list,20,428,std::max(1L,r.right-40),std::max(40L,r.bottom-484),TRUE);MoveWindow(exportButton,r.right-130,r.bottom-38,110,26,TRUE);}return 0;
 case WM_ERASEBKGND:return 1;
 case WM_ACTIVATE:if(LOWORD(w)==WA_INACTIVE&&panel&&!dialogOpen){ShowWindow(h,SW_HIDE);panel=false;model->visible=false;}return 0;
 case WM_QUERYENDSESSION:return TRUE;
 case WM_ENDSESSION:if(w)PostQuitMessage(0);return 0;
 case WM_CLOSE:ShowWindow(h,SW_HIDE);panel=false;model->visible=false;return 0;
 case WM_COMMAND:{int id=LOWORD(w);if(id==101)switchPage(0);if(id==102)switchPage(1);if(id==103)switchPage(2);if(id==113)switchPage(1);if(id>=120&&id<=124){period=page==0?(id==122?5:id-120):(id==120?0:id-119);refreshQuery();updateList();for(auto c:periodButtons)InvalidateRect(c,nullptr,TRUE);InvalidateRect(h,nullptr,FALSE);}if(id==110||id==204){switchPage(3);showPanel();}if(id==104)togglePause();if(id==114&&HIWORD(w)==CBN_SELCHANGE){sortColumn=1+static_cast<int>(SendMessageW(sortBox,CB_GETCURSEL,0,0));descending=true;updateList();}if(id==130||id==131){filterMode=id-130;updateList();InvalidateRect(appButton,nullptr,TRUE);InvalidateRect(proxyButton,nullptr,TRUE);}if(id==107&&HIWORD(w)==EN_CHANGE)updateList();if(id==108)exportCSV();
 if(id==111&&HIWORD(w)==CBN_SELCHANGE){auto index=SendMessageW(networkBox,CB_GETCURSEL,0,0);if(index>=0&&static_cast<size_t>(index)<networkNames.size()){selectedNetwork=index==0?L"":networkNames[index];refreshQuery();}}
 if(id==112){std::wstring n;{std::lock_guard g(model->mutex);n=model->network();}if(!n.empty()&&n!=L"全部网络"){selectedNetwork=n;auto i=std::find(networkNames.begin(),networkNames.end(),n);if(i!=networkNames.end())SendMessageW(networkBox,CB_SETCURSEL,i-networkNames.begin(),0);refreshQuery();}}
 if(id==301){if(!setAutoStart(SendMessageW(startCheck,BM_GETCHECK,0,0)==BST_CHECKED))MessageBoxW(h,L"无法修改登录启动设置。",L"流量喵",MB_ICONERROR);}
 if(id==302)setPreference(L"AutoUpdate",SendMessageW(updateCheck,BM_GETCHECK,0,0)==BST_CHECKED);
 if(id==305){auto code=reinterpret_cast<INT_PTR>(ShellExecuteW(h,L"open",L"ms-settings:privacy-location",nullptr,nullptr,SW_SHOWNORMAL));actionStatus=code>32?L"已打开 Windows 位置设置；未授权仍可统计全部网络。":L"无法打开位置设置，请在 Windows 设置中手动打开。";InvalidateRect(h,nullptr,FALSE);}
 if(id==306)serviceAction(false);
 if(id==307||id==205){bool install;{std::lock_guard g(updateMutex);install=offeredUpdate.available;}beginUpdate(install,true);}
 if(id==308)model->refreshHealth();if(id==309){bool enabled=SendMessageW(wifiCheck,BM_GETCHECK,0,0)==BST_CHECKED;if(enabled)requestWifi();else{rpc({{"protocol",1},{"op","wifi"},{"enabled",false}});selectedNetwork=L"";SendMessageW(networkBox,CB_SETCURSEL,0,0);refreshQuery();}}if(id==201)showPanel();if(id==202)togglePause();if(id==203)PostQuitMessage(0);if(id==206){switchPage(3);showPanel();serviceAction(true);}return 0;}
 case WM_CTLCOLORBTN:case WM_CTLCOLORSTATIC:SetBkColor(reinterpret_cast<HDC>(w),RGB(245,247,250));SetTextColor(reinterpret_cast<HDC>(w),RGB(55,67,85));SetDCBrushColor(reinterpret_cast<HDC>(w),RGB(245,247,250));return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
 case WM_NOTIFY:{auto* n=reinterpret_cast<NMHDR*>(l);
 if(n->hwndFrom==list&&n->code==NM_CUSTOMDRAW){auto* draw=reinterpret_cast<NMLVCUSTOMDRAW*>(l);if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){int index=static_cast<int>(draw->nmcd.dwItemSpec);RECT r=draw->nmcd.rc;HDC dc=draw->nmcd.hdc;HBRUSH bg=CreateSolidBrush(ListView_GetItemState(list,index,LVIS_SELECTED)&LVIS_SELECTED?RGB(230,237,249):RGB(245,247,250));FillRect(dc,&r,bg);DeleteObject(bg);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(35,48,68));SelectObject(dc,strongFont);wchar_t name[256],value[128],second[128],third[128];ListView_GetItemText(list,index,0,name,256);ListView_GetItemText(list,index,1,value,128);ListView_GetItemText(list,index,2,second,128);ListView_GetItemText(list,index,3,third,128);RECT label{r.left+28,r.top+7,r.right-120,r.top+27};DrawTextW(dc,name,-1,&label,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);RECT total{r.right-120,r.top+7,r.right-8,r.top+27};DrawTextW(dc,value,-1,&total,DT_RIGHT|DT_SINGLELINE);SelectObject(dc,font);SetTextColor(dc,RGB(97,112,134));auto rank=std::to_wstring(index+1);RECT number{r.left+1,r.top+8,r.left+24,r.top+28};DrawTextW(dc,rank.c_str(),-1,&number,DT_LEFT|DT_SINGLELINE);std::wstring detail=(page==0?L"↓  ":L"读取  ")+std::wstring(second)+(page==0?L"     ↑  ":L"     总读写  ")+third;RECT details{r.left+28,r.top+33,r.right-8,r.bottom-2};DrawTextW(dc,detail.c_str(),-1,&details,DT_LEFT|DT_SINGLELINE);RECT bar{r.left+28,r.top+29,r.right-8,r.top+31};HBRUSH track=CreateSolidBrush(RGB(222,230,243));FillRect(dc,&bar,track);DeleteObject(track);if(index>=0&&static_cast<size_t>(index)<listTotals.size()){auto maximum=*std::max_element(listTotals.begin(),listTotals.end());bar.right=bar.left+static_cast<LONG>((bar.right-bar.left)*(maximum?static_cast<double>(listTotals[index])/maximum:0));HBRUSH fill=CreateSolidBrush(RGB(38,99,235));FillRect(dc,&bar,fill);DeleteObject(fill);}return CDRF_SKIPDEFAULT;}}
if(n->hwndFrom==list&&n->code==LVN_COLUMNCLICK){int col=reinterpret_cast<NMLISTVIEW*>(l)->iSubItem;if(sortColumn==col)descending=!descending;else{sortColumn=col;descending=true;}updateList();}return 0;}
 case TRAY:if(LOWORD(l)==NIN_SELECT||LOWORD(l)==NIN_KEYSELECT){auto now=GetTickCount64();if(now-lastActivation<250)return 0;lastActivation=now;if(IsWindowVisible(h)){ShowWindow(h,SW_HIDE);panel=false;model->visible=false;}else showPanel();}else if(LOWORD(l)==WM_CONTEXTMENU){bool paused;{std::lock_guard g(model->mutex);paused=model->paused();}HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,201,L"打开流量喵");AppendMenuW(menu,MF_STRING,202,paused?L"继续记录":L"暂停记录");AppendMenuW(menu,MF_STRING,204,L"设置(&S)");AppendMenuW(menu,MF_STRING,205,L"检查更新");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,203,L"退出托盘（继续记录）");AppendMenuW(menu,MF_STRING,206,L"完全退出（停止采集）");POINT p;GetCursorPos(&p);dialogOpen=true;SetForegroundWindow(h);auto command=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,p.x,p.y,0,h,nullptr);DestroyMenu(menu);dialogOpen=false;PostMessageW(h,WM_NULL,0,0);if(command)PostMessageW(h,WM_COMMAND,command,0);}return 0;
 case WM_APP+5:serviceBusy=false;dialogOpen=false;if(serviceThread.joinable())serviceThread.join();if(!w&&l){PostQuitMessage(0);return 0;}if(exitRequested&&!l){exitRequested=false;serviceAction(true);return 0;}actionStatus=w?L"操作失败（错误 "+std::to_wstring(w)+L"）；请重新安装最新版修复服务权限。":L"采集服务已启动";EnableWindow(repairButton,TRUE);refreshQuery();InvalidateRect(h,nullptr,FALSE);return 0;
 case TICK:updateNetworks();updateTray();if(IsWindowVisible(h)){updateList();InvalidateRect(h,nullptr,FALSE);}return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;
 case WM_APP+3:{Update u;{std::lock_guard g(updateMutex);u=offeredUpdate;}SetWindowTextW(updateButton,u.available?(L"安装 "+wide(u.version)+L" 并重启").c_str():L"检查更新");if(manualCheck){switchPage(3);showPanel();}InvalidateRect(h,nullptr,FALSE);return 0;}
 case WM_APP+4:{std::wstring path;{std::lock_guard g(updateMutex);path=installerPath;}Update u;{std::lock_guard g(updateMutex);u=offeredUpdate;}if(launchUpdate(path,u))PostQuitMessage(0);else{std::lock_guard g(updateMutex);updateStatus=L"安装未启动，可稍后重试";}return 0;}
 case WM_TIMER:if(w==1&&preference(L"AutoUpdate",true))beginUpdate(false,false);return 0;
 default:if(m==taskbarCreated){Shell_NotifyIconW(NIM_ADD,&tray);tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);return 0;}return DefWindowProcW(h,m,w,l);
 }}
HICON catIcon(){
 constexpr int n=32;BITMAPV5HEADER bi{};bi.bV5Size=sizeof(bi);bi.bV5Width=n;bi.bV5Height=-n;bi.bV5Planes=1;bi.bV5BitCount=32;bi.bV5Compression=BI_BITFIELDS;bi.bV5RedMask=0xff0000;bi.bV5GreenMask=0xff00;bi.bV5BlueMask=0xff;bi.bV5AlphaMask=0xff000000;
 void* pixels=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP color=CreateDIBSection(dc,reinterpret_cast<BITMAPINFO*>(&bi),DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(dc,color);memset(pixels,0,n*n*4);HPEN pen=CreatePen(PS_SOLID,2,RGB(38,99,235));auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));POINT points[]={{7,14},{6,5},{13,10},{19,10},{26,5},{25,14},{28,19},{25,25},{20,28},{12,28},{7,25},{4,19},{7,14}};Polyline(dc,points,13);for(int x=11;x<=21;x+=5){MoveToEx(dc,x,22,nullptr);LineTo(dc,x,22-(x-6)/2);}
 auto* p=static_cast<DWORD*>(pixels);for(int i=0;i<n*n;++i)if(p[i]&0xffffff)p[i]|=0xff000000;
 SelectObject(dc,oldBrush);SelectObject(dc,oldPen);SelectObject(dc,old);DeleteObject(pen);DeleteDC(dc);HBITMAP mask=CreateBitmap(n,n,1,1,nullptr);ICONINFO ii{};ii.fIcon=TRUE;ii.hbmColor=color;ii.hbmMask=mask;HICON result=CreateIconIndirect(&ii);DeleteObject(color);DeleteObject(mask);return result;
}

}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool show=true;int initialPage=0;
 for(int i=1;i<argc;++i){std::wstring arg=argv[i];if(arg==L"--show")show=true;else if(arg==L"--background")show=false;else if(arg==L"--page"&&i+1<argc)initialPage=std::clamp(_wtoi(argv[++i]),0,3);}LocalFree(argv);
 HANDLE singleton=CreateMutexW(nullptr,TRUE,L"Local\\FlowCat.Windows");if(GetLastError()==ERROR_ALREADY_EXISTS){if(show){HWND existing=FindWindowW(L"FlowCatWindowsPanel",nullptr);if(existing){DWORD pid=0;GetWindowThreadProcessId(existing,&pid);AllowSetForegroundWindow(pid);PostMessageW(existing,WM_COMMAND,201,0);}}if(singleton)CloseHandle(singleton);return 0;}
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&common);
 D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,&d2d);DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(&dwrite));if(!d2d||!dwrite)return 3;
 initializePreferences();model=std::make_unique<ClientModel>();icon=catIcon();WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=instance;wc.lpszClassName=L"FlowCatWindowsPanel";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=icon;RegisterClassW(&wc);
 window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"流量喵 Windows",WS_POPUP|WS_BORDER,0,0,520,740,nullptr,nullptr,instance,nullptr);
 tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP|NIF_SHOWTIP;tray.uCallbackMessage=TRAY;tray.hIcon=icon;wcscpy_s(tray.szTip,L"流量喵 · 正在启动");Shell_NotifyIconW(NIM_ADD,&tray);tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);
 model->start(window);SetTimer(window,1,24*60*60*1000,nullptr);if(preference(L"AutoUpdate",true))beginUpdate(false,false);switchPage(initialPage);serviceAction(false);if(show)showPanel();MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 if(serviceThread.joinable())serviceThread.join();model->stop();if(updateThread.joinable())updateThread.join();Shell_NotifyIconW(NIM_DELETE,&tray);model.reset();releaseTarget();dwrite->Release();d2d->Release();DestroyIcon(icon);DeleteObject(strongFont);DeleteObject(font);CoUninitialize();if(singleton)CloseHandle(singleton);return 0;
}
