#include "ipc.h"
#include <sddl.h>
#include <cstring>
namespace fc {
namespace {
constexpr DWORD Magic=0x31435746,Limit=8*1024*1024;
struct Header{DWORD magic,length;};
bool transfer(HANDLE pipe,void* buffer,DWORD bytes,bool write){
 BYTE* p=static_cast<BYTE*>(buffer);DWORD done=0;
 while(done<bytes){OVERLAPPED op{};op.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!op.hEvent)return false;DWORD count=0;BOOL good=write?WriteFile(pipe,p+done,bytes-done,&count,&op):ReadFile(pipe,p+done,bytes-done,&count,&op);
  if(!good&&GetLastError()==ERROR_IO_PENDING){if(WaitForSingleObject(op.hEvent,3000)==WAIT_OBJECT_0)good=GetOverlappedResult(pipe,&op,&count,FALSE);else{CancelIoEx(pipe,&op);GetOverlappedResult(pipe,&op,&count,TRUE);good=FALSE;}}
  CloseHandle(op.hEvent);if(!good||!count)return false;done+=count;
 }return true;
}
bool receive(HANDLE pipe,Json& value,DWORD limit){Header h{};if(!transfer(pipe,&h,sizeof(h),false)||h.magic!=Magic||h.length==0||h.length>limit)return false;std::string body(h.length,0);if(!transfer(pipe,body.data(),h.length,false))return false;value=Json::parse(body,nullptr,false);return value.is_object();}
bool send(HANDLE pipe,const Json& value){auto body=value.dump(-1,' ',false,Json::error_handler_t::replace);if(body.empty()||body.size()>Limit)return false;Header h{Magic,static_cast<DWORD>(body.size())};return transfer(pipe,&h,sizeof(h),true)&&transfer(pipe,body.data(),h.length,true);}
bool trustedServer(HANDLE pipe){ULONG pid=0;if(!GetNamedPipeServerProcessId(pipe,&pid))return false;SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);if(!manager)return false;SC_HANDLE service=OpenServiceW(manager,ServiceName,SERVICE_QUERY_STATUS);SERVICE_STATUS_PROCESS status{};DWORD size=0;bool good=service&&QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&size)&&status.dwProcessId==pid&&status.dwCurrentState==SERVICE_RUNNING;if(service)CloseServiceHandle(service);CloseServiceHandle(manager);return good;}
}
Json rpc(const Json& request){if(!WaitNamedPipeW(PipeName,1000))return {{"ok",false},{"error","service-unavailable"}};HANDLE pipe=CreateFileW(PipeName,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);if(pipe==INVALID_HANDLE_VALUE)return {{"ok",false},{"error","service-unavailable"}};Json result;bool good=trustedServer(pipe)&&send(pipe,request)&&receive(pipe,result,Limit);if(good){DWORD ack=Magic;good=transfer(pipe,&ack,sizeof(ack),true);}CloseHandle(pipe);if(!good)return {{"ok",false},{"error","service-connection"}};return result;}
void serve(HANDLE stop,const std::function<Json(const Json&)>& handler){
 auto sid=ownerSid();PSID parsed=nullptr;if(sid.empty()||!ConvertStringSidToSidW(sid.c_str(),&parsed)||!IsValidSid(parsed)){if(parsed)LocalFree(parsed);return;}LocalFree(parsed);
 auto sddl=L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;"+sid+L")";PSECURITY_DESCRIPTOR security=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&security,nullptr))return;
 SECURITY_ATTRIBUTES attributes{sizeof(attributes),security,FALSE};
 HANDLE pipe=CreateNamedPipeW(PipeName,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,4096,3000,&attributes);LocalFree(security);if(pipe==INVALID_HANDLE_VALUE)return;
 while(WaitForSingleObject(stop,0)==WAIT_TIMEOUT){OVERLAPPED op{};op.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);BOOL connected=ConnectNamedPipe(pipe,&op);DWORD err=connected?ERROR_SUCCESS:GetLastError();
  if(err==ERROR_IO_PENDING){HANDLE waits[]={stop,op.hEvent};if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)!=WAIT_OBJECT_0+1){CancelIoEx(pipe,&op);DWORD count;GetOverlappedResult(pipe,&op,&count,TRUE);CloseHandle(op.hEvent);break;}DWORD count;connected=GetOverlappedResult(pipe,&op,&count,FALSE);}else connected=err==ERROR_PIPE_CONNECTED||err==ERROR_SUCCESS;
  CloseHandle(op.hEvent);if(connected){Json q,response;try{if(receive(pipe,q,4096))response=handler(q);else response={{"ok",false},{"error","request"}};}catch(...){response={{"ok",false},{"error","invalid-request"}};}if(send(pipe,response)){DWORD ack=0;transfer(pipe,&ack,sizeof(ack),false);}}
  DisconnectNamedPipe(pipe);
 }CloseHandle(pipe);
}
}
