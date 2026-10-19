#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include "common.h"
#include "core.h"
#include "health.h"
namespace fc {
class ClientModel {
 public:
 std::mutex mutex;Json snapshot=Json::object();std::wstring error=L"正在连接采集服务";bool connected=false,loading=true;
 std::atomic<bool> visible{false};
 void start(HWND notify);void stop();void query(const std::wstring& since,const std::wstring& until,const std::wstring& network);void pause(bool value);void refreshHealth();
 std::vector<Row> rows(bool disk,int proxy=-1) const;
 Bytes physical() const;Bytes rates() const;
 bool paused() const{return snapshot.value("paused",false);}std::wstring network() const{return wide(snapshot.value("network",std::string{}));}
 std::vector<DiskHealth> health() const;
 private:
 std::thread thread_;std::condition_variable condition_;bool quit_=false,wake_=true;unsigned generation_=0;Json query_={{"protocol",1},{"op","snapshot"}};int pause_=-1;bool health_=false;
};
}
