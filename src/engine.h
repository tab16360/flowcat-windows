#pragma once
#include "collector.h"
#include "store.h"
#include "health.h"
#include "common.h"
namespace fc {
struct Pending {std::map<std::wstring,Row> rows;Bytes physical;};
class Engine {
 public:
 explicit Engine(const std::wstring& directory);
 void start();void stop();Json request(const Json& query);
 Collector collector;
 private:
 Store store_;std::mutex mutex_;std::thread worker_;std::atomic<bool> quit_{false};
 std::map<std::pair<std::wstring,std::wstring>,Pending> pending_;
 Bytes rates_;std::wstring network_=L"全部网络";std::vector<DiskHealth> health_;bool saved_=true;
 void sample();void flush();
 std::vector<Row> rows(const std::wstring& since,const std::wstring& until,const std::wstring& network);
 Bytes physical(const std::wstring& since,const std::wstring& until,const std::wstring& network);
};
Json bytesJson(const Bytes& b);Bytes bytesFromJson(const Json& j);
}
