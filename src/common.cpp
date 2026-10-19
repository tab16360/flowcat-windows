#include "common.h"
#include <sddl.h>
#include <shlobj.h>
namespace fc {
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);if(!n)return {};std::string r(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)return {};std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n);return r;}
std::wstring userSid(){HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return {};DWORD size=0;GetTokenInformation(token,TokenUser,nullptr,0,&size);std::vector<unsigned char> b(size);std::wstring result;if(GetTokenInformation(token,TokenUser,b.data(),size,&size)){LPWSTR s=nullptr;if(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid,&s)){result=s;LocalFree(s);}}CloseHandle(token);return result;}
std::wstring executablePath(){wchar_t p[32768];DWORD n=GetModuleFileNameW(nullptr,p,32768);return std::wstring(p,n);}
std::wstring sharedDataDirectory(){PWSTR p=nullptr;std::wstring result;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData,0,nullptr,&p))){result=std::wstring(p)+L"\\FlowCatWindows";CoTaskMemFree(p);}return result;}
std::wstring ownerSid(){wchar_t value[256]{};DWORD size=sizeof(value);if(RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\FlowCatWindows",L"OwnerSid",RRF_RT_REG_SZ,nullptr,value,&size)!=ERROR_SUCCESS)return {};return value;}
bool isAdministrator(){SID_IDENTIFIER_AUTHORITY auth=SECURITY_NT_AUTHORITY;PSID sid=nullptr;BOOL result=FALSE;if(AllocateAndInitializeSid(&auth,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)){CheckTokenMembership(nullptr,sid,&result);FreeSid(sid);}return result!=FALSE;}
}
