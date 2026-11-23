#include "pairing_reset.h"
#include "nvs.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* Index 0 is the Lockdown trust namespace, 1 the AirPlay controller table. */
static const char *const names[2]={"tab5_pair","tab5_ap_pair"};
static bool records[2]={true,false},leave_entries;
static int finds,erases,commits,closes,fail_find,fail_open,fail_erase,fail_commit;
static unsigned other_namespace=0x12345678;
int64_t esp_timer_get_time(void) { return 1000000; }
const char *esp_err_to_name(esp_err_t e) { return e==ESP_OK?"OK":"error"; }
static int index_of(const char *ns)
{
    for(int i=0;i<2;++i) if(!strcmp(ns,names[i])) return i;
    assert(!"unexpected namespace"); return -1;
}
esp_err_t nvs_entry_find(const char *partition,const char *ns,int type,nvs_iterator_t *it)
{
    assert(!strcmp(partition,"nvs") && type==NVS_TYPE_ANY);
    const int i=index_of(ns);
    ++finds; *it=NULL;
    return fail_find?ESP_FAIL:records[i]?ESP_OK:ESP_ERR_NVS_NOT_FOUND;
}
void nvs_release_iterator(nvs_iterator_t it) { assert(!it); }
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h)
{ assert(mode==NVS_READWRITE); *h=42+index_of(ns); return fail_open?ESP_FAIL:ESP_OK; }
esp_err_t nvs_erase_all(nvs_handle_t h)
{
    assert(h==42 || h==43); ++erases;
    assert(tab5_pair_reset_status()==TAB5_PAIR_CLEARING);
    assert(!tab5_pair_reset_request()); /* Duplicate click during the actual erase. */
    if(!fail_erase && !leave_entries) records[h-42]=false;
    return fail_erase?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { assert(h==42 || h==43); ++commits; return fail_commit?ESP_FAIL:ESP_OK; }
void nvs_close(nvs_handle_t h) { assert(h==42 || h==43); ++closes; }
static void request_and_poll(void)
{
    tab5_pair_reset_poll(true);
    assert(tab5_pair_reset_ready());
    assert(tab5_pair_reset_request());
    assert(!tab5_pair_reset_request());
    tab5_pair_reset_poll(true);
}
int main(void)
{
    assert(!tab5_pair_reset_ready() && !tab5_pair_reset_request());
    tab5_pair_reset_poll(true);
    assert(tab5_pair_reset_request());
    tab5_pair_reset_poll(false); /* Phone arrives between confirmation and owner processing. */
    assert(tab5_pair_reset_status()==TAB5_PAIR_BLOCKED && finds==0 && erases==0);
    request_and_poll();
    assert(tab5_pair_reset_status()==TAB5_PAIR_CLEARED && !records[0] && !records[1]);
    assert(erases==1 && commits==1 && closes==1 && other_namespace==0x12345678);
    request_and_poll(); /* Empty is idempotent and doesn't open/write either namespace. */
    assert(tab5_pair_reset_status()==TAB5_PAIR_EMPTY && erases==1 && closes==1);
    /* Only AirPlay controllers left: they are cleared too. */
    records[1]=true; request_and_poll();
    assert(tab5_pair_reset_status()==TAB5_PAIR_CLEARED && !records[1] && erases==2 && closes==2);
    /* Both present: both cleared in one request. */
    records[0]=records[1]=true; request_and_poll();
    assert(tab5_pair_reset_status()==TAB5_PAIR_CLEARED && !records[0] && !records[1] && erases==4 && closes==4);
    records[0]=true; fail_find=1; request_and_poll();
    assert(tab5_pair_reset_status()==TAB5_PAIR_FAILED && erases==4);
    fail_find=0; fail_open=1; request_and_poll();
    assert(tab5_pair_reset_status()==TAB5_PAIR_FAILED && erases==4 && closes==4);
    fail_open=0; fail_erase=1; request_and_poll();
    assert(tab5_pair_reset_status()==TAB5_PAIR_FAILED && closes==5);
    fail_erase=0; fail_commit=1; request_and_poll();
    assert(tab5_pair_reset_status()==TAB5_PAIR_FAILED && closes==6);
    fail_commit=0; records[0]=true; leave_entries=true; request_and_poll();
    assert(tab5_pair_reset_status()==TAB5_PAIR_FAILED && records[0]); /* Verify, not just commit. */
    leave_entries=false; request_and_poll();
    assert(tab5_pair_reset_status()==TAB5_PAIR_CLEARED && !records[0]);
    int before=erases;
    tab5_pair_reset_poll(true); tab5_pair_reset_poll(false); tab5_pair_reset_poll(true);
    assert(erases==before); /* No auto-clear on poll, reconnect, or restart-like idle. */
    puts("pairing reset scope (trust and AirPlay), race, idempotence and storage errors: OK");
}
