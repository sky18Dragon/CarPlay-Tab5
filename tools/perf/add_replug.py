p='main/app_ui.c'; s=open(p).read()
a='    ESP_LOGW("VIDEO","MOTION end");\n'
assert a in s
s=s.replace(a,a+'''    for(int k=0;k<2;++k) { vTaskDelay(pdMS_TO_TICKS(5000)); ESP_LOGW("REPLUG","off %d",k); bsp_feature_enable(BSP_FEATURE_USB,false); vTaskDelay(pdMS_TO_TICKS(3000));
        bsp_feature_enable(BSP_FEATURE_USB,true); ESP_LOGW("REPLUG","on %d",k); vTaskDelay(pdMS_TO_TICKS(30000)); }  /* TEMP */
''',1)
open(p,'w').write(s)
