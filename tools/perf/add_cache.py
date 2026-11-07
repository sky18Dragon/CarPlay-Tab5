s=open('main/offline_demo.c').read()
a='static void bench(void)\n{\n'
assert a in s
s=s.replace(a,'''#include "esp32p4/rom/cache.h"
static void cache_mode(int m)
{
    Cache_Disable_L2_Cache_Autoload(); Cache_Disable_L1_DCache_Autoload();
    struct l1_dcache_l2_autoload_config c={.gid=0,.order=0,.trigger=0,.ena0=1,.addr0=0x48000000,.size0=32*1024*1024};
    switch(m) {
    case 1: c.trigger=CACHE_AUTOLOAD_MISS_TRIGGER; Cache_Config_L2_Cache_Autoload(&c); Cache_Enable_L2_Cache_Autoload(); break;
    case 2: c.trigger=CACHE_AUTOLOAD_BOTH_TRIGGER; Cache_Config_L2_Cache_Autoload(&c); Cache_Enable_L2_Cache_Autoload(); break;
    case 3: c.trigger=CACHE_AUTOLOAD_MISS_TRIGGER; Cache_Config_L1_DCache_Autoload(&c); Cache_Enable_L1_DCache_Autoload(); break;
    case 4: c.trigger=CACHE_AUTOLOAD_MISS_TRIGGER; Cache_Config_L1_DCache_Autoload(&c); Cache_Enable_L1_DCache_Autoload(); Cache_Config_L2_Cache_Autoload(&c); Cache_Enable_L2_Cache_Autoload(); break;
    case 5: c.trigger=CACHE_AUTOLOAD_BOTH_TRIGGER; Cache_Config_L1_DCache_Autoload(&c); Cache_Enable_L1_DCache_Autoload(); c.trigger=CACHE_AUTOLOAD_MISS_TRIGGER; Cache_Config_L2_Cache_Autoload(&c); Cache_Enable_L2_Cache_Autoload(); break;
    default: break;
    }
}
'''+a,1)
a='with_crc=round<2; strength=round>=4?128:0;\n'
assert a in s
s=s.replace(a,'with_crc=false; strength=128; { static const int modes[]={0,1,2,3,4,5,0}; cache_mode(modes[round]); ESP_LOGW("BENCH","mode %d",modes[round]); }\n',1)
s=s.replace('for(int round=0;round<6;++round) {','for(int round=0;round<7;++round) {',1)
open('main/offline_demo.c','w').write(s)
