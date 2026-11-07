p='main/video_output.cpp'; s=open(p).read()
a='''    if(!worker_task || !data || !size || size>512*1024) return false;
'''
assert a in s
s=s.replace(a,a+'''    { static int64_t la; static uint64_t lp; static size_t ls; static uint32_t n,big; const int64_t t=esp_timer_get_time(); ++n; if(size>20000) ++big;
      if(la && (t-la>=95000 || (int64_t)(pts-lp)>=95)) ESP_LOGW("GAP","n=%lu arr=%lld snd=%lld size=%u prev=%u big=%lu",(unsigned long)n,(long long)((t-la)/1000),(long long)(pts-lp),(unsigned)size,(unsigned)ls,(unsigned long)big);
      la=t; lp=pts; ls=size; }  /* TEMP */
''',1)
open(p,'w').write(s)
