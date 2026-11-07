s=open('main/offline_demo.c').read()
a='with_crc=round<2; strength=round>=4?128:0;\n'
assert a in s
s=s.replace(a,'with_crc=round<3; strength=128; if(round==0) { extern void tab5_test_preload(void); tab5_test_preload(); }\n',1)
s=s.replace('static void bench(void)\n{\n','''#include "esp32p4/rom/cache.h"
#include "soc/soc.h"
void tab5_test_preload(void) { const struct l1_dcache_l2_autoload_config c={.trigger=CACHE_AUTOLOAD_MISS_TRIGGER,.ena0=1,.addr0=SOC_EXTRAM_LOW,.size0=SOC_EXTRAM_HIGH-SOC_EXTRAM_LOW}; Cache_Config_L2_Cache_Autoload(&c); Cache_Enable_L2_Cache_Autoload(); }
static void bench(void)\n{\n''',1)
open('main/offline_demo.c','w').write(s)
