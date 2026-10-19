#include "engine.h"
#include <winrt/base.h>
#include <set>
namespace fc {
Engine::Engine(const std::wstring& directory):store_(directory){}
void Engine::sample(){auto b=collector.take();std::lock_guard g(mutex_);network_=b.network;rates_={b.physical.down/2,b.physical.up/2,0,0};pending_[{dayKey(),network_}].physical.merge(b.physical);for(auto& r:b.rows){auto& row=pending_[{r.day.empty()?dayKey():r.day,network_}].rows[r.key];row.key=r.key;row.name=r.name;row.proxy=r.proxy;row.bytes.merge(r.bytes);rates_.read=add(rates_.read,r.bytes.read/2);rates_.write=add(rates_.write,r.bytes.write/2);}}
void Engine::flush(){std::lock_guard g(mutex_);if(!store_.ok()){saved_=false;collector.setPaused(true);return;}for(auto it=pending_.begin();it!=pending_.end();){std::vector<Row> rows;for(auto& [k,r]:it->second.rows)rows.push_back(r);if(!store_.put(it->first.first,it->first.second,rows,it->second.physical)){saved_=false;collector.setPaused(true);return;}it=pending_.erase(it);}saved_=true;}
std::vector<Row> Engine::rows(const std::wstring& since,const std::wstring& until,const std::wstring& network){auto rows=store_.rows(since,until,false,-1,network);auto disk=store_.rows(since,until,true,-1,network);std::map<std::wstring,Row> combined;for(auto& r:rows)combined[r.key]=r;for(auto& r:disk)combined[r.key]=r;for(auto& [k,p]:pending_)if(k.first>=since&&k.first<=until&&(network.empty()||k.second==network))for(auto& [name,r]:p.rows){auto& a=combined[name];a.key=r.key;a.name=r.name;a.proxy=r.proxy;a.bytes.merge(r.bytes);}rows.clear();for(auto& [k,r]:combined)rows.push_back(r);return rows;}
Bytes Engine::physical(const std::wstring& since,const std::wstring& until,const std::wstring& network){auto b=store_.physical(since,until,network);for(auto& [k,p]:pending_)if(k.first>=since&&k.first<=until&&(network.empty()||k.second==network))b.merge(p.physical);return b;}
void Engine::start(){collector.start();DWORD paused=0,size=sizeof(paused);RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\FlowCatWindows",L"Paused",RRF_RT_REG_DWORD,nullptr,&paused,&size);collector.setPaused(paused!=0);DWORD wifi=1;size=sizeof(wifi);RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\FlowCatWindows",L"WifiNames",RRF_RT_REG_DWORD,nullptr,&wifi,&size);collector.identifyNetworks=wifi!=0;worker_=std::thread([this]{winrt::init_apartment();auto health=diskHealth();{std::lock_guard g(mutex_);health_=std::move(health);}unsigned tick=0;while(!quit_){for(int i=0;i<20&&!quit_;++i)Sleep(100);if(quit_)break;sample();++tick;if(tick%30==0)flush();if(tick%300==0){auto refreshed=diskHealth();std::lock_guard g(mutex_);health_=std::move(refreshed);}}winrt::uninit_apartment();});}
void Engine::stop(){quit_=true;if(worker_.joinable())worker_.join();collector.stop();sample();flush();}
static bool date(const std::string& s){if(s.size()!=10)return false;for(size_t i=0;i<10;++i)if(i==4||i==7){if(s[i]!='-')return false;}else if(s[i]<'0'||s[i]>'9')return false;return true;}
Json Engine::request(const Json& q){
 if(q.value("protocol",0)!=1)return {{"ok",false},{"error","protocol"}};
 auto op=q.value("op",std::string("snapshot"));
 if(op=="pause"){bool paused=q.at("paused").get<bool>();collector.setPaused(paused);HKEY key;if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\FlowCatWindows",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS){DWORD v=paused;RegSetValueExW(key,L"Paused",0,REG_DWORD,reinterpret_cast<BYTE*>(&v),sizeof(v));RegCloseKey(key);}return {{"ok",true}};}
 if(op=="wifi"){bool enabled=q.at("enabled").get<bool>();collector.identifyNetworks=enabled;HKEY key;if(RegCreateKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\FlowCatWindows",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS){DWORD v=enabled;RegSetValueExW(key,L"WifiNames",0,REG_DWORD,reinterpret_cast<BYTE*>(&v),sizeof(v));RegCloseKey(key);}return {{"ok",true}};}
 if(op=="health"){auto health=diskHealth();std::lock_guard g(mutex_);health_=std::move(health);return {{"ok",true}};}
 if(op!="snapshot"&&op!="csv")return {{"ok",false},{"error","operation"}};
 auto start=q.value("since",std::string("0000-00-00")),end=q.value("until",utf8(dayKey())),net=q.value("network",std::string{});
 if(!date(start)||!date(end)||start>end||net.size()>128)return {{"ok",false},{"error","query"}};
 if(op=="csv"){flush();std::lock_guard g(mutex_);auto csv=store_.csvData(wide(start),wide(end),wide(net));if(csv.size()>7*1024*1024)return {{"ok",false},{"error","export-too-large"}};return {{"ok",saved_},{"csv",csv}};}
 std::lock_guard g(mutex_);Json response={{"ok",true},{"paused",collector.paused.load()},{"identifyNetworks",collector.identifyNetworks.load()},{"error",collector.error.load()},{"lost",collector.lost.load()},{"saved",saved_&&store_.ok()},{"saveError",utf8(store_.error())},{"network",utf8(network_)},{"rates",bytesJson(rates_)},{"events",collector.eventCount.load()},{"parseErrors",collector.parseErrors.load()},{"unattributedDisk",collector.unattributedDisk.load()}};
 if(q.value("summaryOnly",false))return response;
 response["physical"]=bytesJson(physical(wide(start),wide(end),wide(net)));response["rows"]=Json::array();
 for(auto& r:rows(wide(start),wide(end),wide(net))){if(response["rows"].size()>=10000){response["truncated"]=true;break;}response["rows"].push_back({{"key",utf8(r.key)},{"name",utf8(r.name)},{"proxy",r.proxy},{"bytes",bytesJson(r.bytes)}});}
 std::set<std::wstring> networks;for(auto& s:store_.networks())if(s!=L"全部网络")networks.insert(s);for(auto& [k,p]:pending_)if(k.second!=L"全部网络")networks.insert(k.second);response["networks"]=Json::array();for(auto& n:networks)response["networks"].push_back(utf8(n));
 response["health"]=Json::array();for(auto& d:health_)response["health"].push_back({{"name",utf8(d.name)},{"bus",utf8(d.bus)},{"status",utf8(d.status)},{"temperature",utf8(d.temperature)},{"wear",utf8(d.wear)},{"read",utf8(d.read)},{"write",utf8(d.write)},{"hours",utf8(d.hours)},{"errors",utf8(d.errors)}});return response;
}
}
