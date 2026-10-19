#include "collector.h"
#include <ctime>
#include <winrt/Windows.Networking.Connectivity.h>
namespace fc {
std::wstring dayKey(int offset) {
 std::time_t t=std::time(nullptr); std::tm tm{};localtime_s(&tm,&t);tm.tm_mday+=offset;tm.tm_hour=12;tm.tm_isdst=-1;std::mktime(&tm);
 wchar_t b[16];wcsftime(b,16,L"%Y-%m-%d",&tm);return b;
}
std::wstring currentNetwork() {
 try {auto profile=winrt::Windows::Networking::Connectivity::NetworkInformation::GetInternetConnectionProfile();if(profile&&profile.IsWlanConnectionProfile()){auto ssid=profile.WlanConnectionProfileDetails().GetConnectedSsid();if(!ssid.empty())return std::wstring(ssid);}}catch(...){}
 return L"全部网络";
}
}
