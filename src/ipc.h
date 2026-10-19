#pragma once
#include "common.h"
#include <functional>
namespace fc {
Json rpc(const Json& request);
void serve(HANDLE stop,const std::function<Json(const Json&)>& handler);
}
