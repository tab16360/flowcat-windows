#pragma once
#include "common.h"
namespace fc {
struct Update {std::string version,url,digest;uint64_t size=0;bool available=false;};
Update selectUpdate(const Json& release,const std::string& current);
bool newerVersion(const std::string& a,const std::string& b);
Update checkUpdate(const std::string& current=Version);
std::wstring downloadUpdate(const Update& update);
bool launchUpdate(const std::wstring& path,const Update& update);
}
