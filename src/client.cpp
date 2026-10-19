#include "client.h"
#include "ipc.h"
#include "engine.h"
namespace fc {
void ClientModel::query(const std::wstring& since,const std::wstring& until,const std::wstring& network){std::lock_guard g(mutex);query_={{"protocol",1},{"op","snapshot"},{"since",utf8(since)},{"until",utf8(until)},{"network",utf8(network)}};++generation_;loading=true;wake_=true;condition_.notify_one();}
void ClientModel::pause(bool value){std::lock_guard g(mutex);pause_=value;wake_=true;condition_.notify_one();}
void ClientModel::refreshHealth(){std::lock_guard g(mutex);health_=true;wake_=true;condition_.notify_one();}
void ClientModel::start(HWND notify){thread_=std::thread([this,notify]{while(true){Json q;unsigned generation;int p;bool health;
 {std::unique_lock lock(mutex);condition_.wait_for(lock,std::chrono::seconds(2),[&]{return quit_||wake_;});if(quit_)break;wake_=false;q=query_;q["summaryOnly"]=!visible;generation=generation_;p=pause_;pause_=-1;health=health_;health_=false;}
 if(p>=0)rpc({{"protocol",1},{"op","pause"},{"paused",p!=0}});if(health)rpc({{"protocol",1},{"op","health"}});
 auto result=rpc(q);{std::lock_guard g(mutex);if(generation!=generation_)continue;connected=result.value("ok",false);error=connected?L"":L"采集服务未连接，请在设置中启动或修复安装";if(connected){if(q.value("summaryOnly",false)){for(auto& field:{"rates","network","paused","error","lost","saved","saveError","identifyNetworks"})if(result.contains(field))snapshot[field]=result[field];}else{snapshot=std::move(result);loading=false;}}}PostMessageW(notify,WM_APP+2,0,0);
 }});}
void ClientModel::stop(){{std::lock_guard g(mutex);quit_=true;condition_.notify_one();}if(thread_.joinable())thread_.join();}
std::vector<Row> ClientModel::rows(bool disk,int proxy) const{std::vector<Row> out;if(loading||!snapshot.contains("rows"))return out;for(auto& value:snapshot["rows"]){Row r;r.key=wide(value.value("key",std::string{}));r.name=wide(value.value("name",std::string{}));r.proxy=value.value("proxy",false);r.bytes=bytesFromJson(value["bytes"]);if(r.bytes.total(disk)>0&&(disk||proxy<0||r.proxy==(proxy!=0)))out.push_back(r);}return out;}
Bytes ClientModel::physical() const{return snapshot.contains("physical")?bytesFromJson(snapshot["physical"]):Bytes{};}
Bytes ClientModel::rates() const{return snapshot.contains("rates")?bytesFromJson(snapshot["rates"]):Bytes{};}
std::vector<DiskHealth> ClientModel::health() const{std::vector<DiskHealth> result;if(snapshot.contains("health"))for(auto& v:snapshot["health"]){DiskHealth d;d.name=wide(v.value("name",std::string{}));d.bus=wide(v.value("bus",std::string{}));d.status=wide(v.value("status",std::string{}));d.temperature=wide(v.value("temperature",std::string{}));d.wear=wide(v.value("wear",std::string{}));d.read=wide(v.value("read",std::string{}));d.write=wide(v.value("write",std::string{}));d.hours=wide(v.value("hours",std::string{}));d.errors=wide(v.value("errors",std::string{}));result.push_back(d);}return result;}
}
