#include "../src/core.h"
#include "../src/dpi.h"
#include <cstdio>
#include <cstdlib>
using namespace fc;
void check(bool good,const char* name){if(!good){printf("FAIL: %s\n",name);exit(1);}printf("PASS: %s\n",name);}
int main(){
 check(add(UINT64_MAX-1,8)==UINT64_MAX,"byte accumulation saturates");
 check(delta(20,10)==10,"monotonic deltas");check(delta(1,UINT64_MAX)==0,"counter reset cannot inflate traffic");
 Bytes b{1,2,3,4};b.merge({5,6,7,8});check(b.down==6&&b.up==8&&b.read==10&&b.write==12,"independent network and disk counters");
 check(b.total(false)==14&&b.total(true)==22,"network and disk scopes do not mix");
 check(relay(L"Mihomo.exe")&&!relay(L"my-mihomo-editor.exe"),"proxy matching uses exact case-insensitive names");
 check(volume(1000)==L"1.00 KB"&&volume(999)==L"999 B","decimal byte units");
 check(volume(200000000000ULL)==L"200.00 GB","64-bit values preserve large histories");
 for(unsigned dpi:{96,120,144,168,192,240,288}){
  DpiScale scale{dpi};
  check(scale.pixels(520)==static_cast<int>(std::lround(520.0*dpi/96)),"panel uses monitor DPI at 100-300 percent");
  check(std::abs(scale.dips(scale.pixels(740))-740)<0.5f,"drawing height converts pixels back to DIPs");
  check(scale.pixels(428)>scale.pixels(382+30)&&scale.pixels(740-38)>scale.pixels(428+250),"search, list and footer remain separate at each DPI");
 }
 puts("All 29 core checks passed.");
}
