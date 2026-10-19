#include "engine.h"
namespace fc {
Json bytesJson(const Bytes& b){return {{"down",b.down},{"up",b.up},{"read",b.read},{"write",b.write}};}
Bytes bytesFromJson(const Json& j){return {j.value("down",uint64_t{}),j.value("up",uint64_t{}),j.value("read",uint64_t{}),j.value("write",uint64_t{})};}
}
