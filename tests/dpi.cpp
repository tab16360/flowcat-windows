#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wincodec.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

void capture(HWND window,const std::filesystem::path& path){
 IWICImagingFactory* factory=nullptr;
 if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return;
 RECT r{};GetWindowRect(window,&r);HDC dc=GetDC(window),memory=CreateCompatibleDC(dc);
 HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right-r.left,r.bottom-r.top);auto old=SelectObject(memory,bitmap);PrintWindow(window,memory,2);
 IWICBitmap* source=nullptr;IWICStream* stream=nullptr;IWICBitmapEncoder* encoder=nullptr;IWICBitmapFrameEncode* frame=nullptr;
 if(SUCCEEDED(factory->CreateBitmapFromHBITMAP(bitmap,nullptr,WICBitmapIgnoreAlpha,&source))&&SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))&&SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream,WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr))){frame->Initialize(nullptr);frame->SetSize(r.right-r.left,r.bottom-r.top);WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGR;frame->SetPixelFormat(&format);frame->WriteSource(source,nullptr);frame->Commit();encoder->Commit();}
 if(frame)frame->Release();if(encoder)encoder->Release();if(stream)stream->Release();if(source)source->Release();factory->Release();SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,dc);
}
RECT clientRect(HWND window,HWND control){RECT r{};GetWindowRect(control,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);return r;}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<2)return 1;std::filesystem::path directory=argv[1];LocalFree(argv);std::filesystem::create_directories(directory);
 HWND window=FindWindowW(L"FlowCatWindowsPanel",nullptr);if(!window)return 2;
 DWORD pid=0;GetWindowThreadProcessId(window,&pid);HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,pid);DWORD before=process?GetGuiResources(process,GR_GDIOBJECTS):0;
 RECT original{};GetWindowRect(window,&original);
 std::vector<RECT> monitors;
 EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL{MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);reinterpret_cast<std::vector<RECT>*>(data)->push_back(info.rcWork);return TRUE;},reinterpret_cast<LPARAM>(&monitors));
 std::ofstream report(directory/L"dpi-layout.json");report<<"{\"monitors\":[";bool first=true,good=true;
 for(size_t index=0;index<monitors.size();++index){
  auto work=monitors[index];SetWindowPos(window,HWND_TOPMOST,work.left+8,work.top+8,0,0,SWP_NOSIZE|SWP_SHOWWINDOW);Sleep(250);
  UINT dpi=GetDpiForWindow(window);auto px=[dpi](int dip){return MulDiv(dip,dpi,96);};
  int width=px(520),height=std::min(px(740),static_cast<int>(work.bottom-work.top)-16);
  SetWindowPos(window,HWND_TOPMOST,work.left+8,work.top+8,width,height,SWP_SHOWWINDOW);Sleep(300);
  for(int page=0;page<4;++page){
   SendMessageW(window,WM_COMMAND,page==3?110:101+page,0);
   // Settings opens at the cursor; explicitly return to the monitor being tested.
   SetWindowPos(window,HWND_TOPMOST,work.left+8,work.top+8,width,height,SWP_SHOWWINDOW);Sleep(500);
   RECT client{};GetClientRect(window,&client);
   auto nav=clientRect(window,GetDlgItem(window,101));auto search=clientRect(window,GetDlgItem(window,107));auto list=clientRect(window,GetDlgItem(window,109));auto footer=clientRect(window,GetDlgItem(window,108));
   LOGFONTW font{};GetObjectW(reinterpret_cast<HFONT>(SendMessageW(GetDlgItem(window,101),WM_GETFONT,0,0)),sizeof(font),&font);
   bool navFits=std::abs(nav.left-px(20))<=1&&std::abs(nav.top-px(66))<=1&&std::abs((nav.right-nav.left)-px(110))<=1;
   bool contentFits=page>=2||(std::abs(search.top-px(382))<=1&&std::abs(list.top-px(428))<=1&&search.bottom<list.top&&list.bottom<footer.top&&footer.bottom<=client.bottom);
   bool fontFits=std::abs(-font.lfHeight-px(14))<=1;
   bool pass=navFits&&contentFits&&fontFits;good=good&&pass;
   if(!first)report<<",";first=false;
   report<<"{\"monitor\":"<<index<<",\"page\":"<<page<<",\"dpi\":"<<dpi<<",\"width\":"<<width<<",\"height\":"<<height<<",\"navFits\":"<<(navFits?"true":"false")<<",\"contentFits\":"<<(contentFits?"true":"false")<<",\"fontFits\":"<<(fontFits?"true":"false")<<",\"pass\":"<<(pass?"true":"false")<<"}";
   capture(window,directory/(L"dpi-monitor-"+std::to_wstring(index)+L"-page-"+std::to_wstring(page)+L".png"));
  }
 }
 // Repeat actual cross-monitor moves to expose stale font/image-list allocations.
 if(monitors.size()>1)for(int i=0;i<20;++i){auto r=monitors[i%monitors.size()];SetWindowPos(window,nullptr,r.left+8,r.top+8,0,0,SWP_NOSIZE|SWP_NOZORDER);Sleep(50);}
 DWORD after=process?GetGuiResources(process,GR_GDIOBJECTS):0;if(process)CloseHandle(process);
 bool resourcesStable=after<=before+12;good=good&&resourcesStable;
 report<<"],\"gdiBefore\":"<<before<<",\"gdiAfter\":"<<after<<",\"resourcesStable\":"<<(resourcesStable?"true":"false")<<",\"pass\":"<<(good?"true":"false")<<"}";report.close();
 SendMessageW(window,WM_COMMAND,102,0);SetWindowPos(window,HWND_TOPMOST,original.left,original.top,original.right-original.left,original.bottom-original.top,SWP_SHOWWINDOW);
 CoUninitialize();return good?0:3;
}
