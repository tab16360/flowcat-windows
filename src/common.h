#pragma once
#include <windows.h>
#include <string>
#include "../vendor/json.hpp"
namespace fc {
using Json=nlohmann::json;
inline constexpr wchar_t ServiceName[]=L"FlowCatCollector";
inline constexpr wchar_t PipeName[]=L"\\\\.\\pipe\\FlowCatWindows.v1";
inline constexpr char Version[]="0.2.3";
inline constexpr wchar_t VersionW[]=L"0.2.3";
std::string utf8(const std::wstring& s);
std::wstring wide(const std::string& s);
std::wstring userSid();
std::wstring executablePath();
std::wstring sharedDataDirectory();
std::wstring ownerSid();
bool isAdministrator();
}
