// SPDX-License-Identifier: GPL-3.0-or-later
#include "airplay_pairing_store.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* One blob in one namespace, as the store uses. */
static unsigned char stored[4096];
static size_t stored_len;
static int open_handles;
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h)
{ (void)mode; assert(!strcmp(ns, "tab5_ap_pair")); ++open_handles; *h = 1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { assert(h == 1); --open_handles; }
esp_err_t nvs_commit(nvs_handle_t h) { assert(h == 1); return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    assert(h == 1 && !strcmp(key, "controllers"));
    if (!stored_len) return ESP_ERR_NVS_NOT_FOUND;
    if (!out) { *len = stored_len; return ESP_OK; }
    assert(*len >= stored_len);
    memcpy(out, stored, stored_len); *len = stored_len; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len)
{
    assert(h == 1 && !strcmp(key, "controllers") && len <= sizeof(stored));
    memcpy(stored, data, len); stored_len = len; return ESP_OK;
}
static void id(char out[16], unsigned n) { snprintf(out, 16, "phone-%02u", n); }
static void key(unsigned char out[32], unsigned n) { memset(out, (int)n, 32); }
static bool save(unsigned n)
{
    char name[16]; unsigned char k[32]; id(name, n); key(k, n);
    return airplay_pairing_store_save_callback((const uint8_t *)name, strlen(name), k, NULL);
}
static bool known(unsigned n)
{
    char name[16]; unsigned char k[32], want[32]; id(name, n); key(want, n);
    return airplay_pairing_store_lookup_callback((const uint8_t *)name, strlen(name), k, NULL) &&
           !memcmp(k, want, 32);
}
int main(void)
{
    assert(airplay_pairing_store_init() == ESP_OK);
    for (unsigned n = 1; n <= AIRPLAY_PAIRING_STORE_MAX_CONTROLLERS; ++n) assert(save(n));
    for (unsigned n = 1; n <= AIRPLAY_PAIRING_STORE_MAX_CONTROLLERS; ++n) assert(known(n));
    /* A ninth phone used to be refused forever. Now the oldest gives way. */
    assert(save(9));
    assert(!known(1) && known(2) && known(9));
    size_t count = 0;
    assert(airplay_pairing_store_count(&count) == ESP_OK && count == AIRPLAY_PAIRING_STORE_MAX_CONTROLLERS);
    /* Re-pairing a known phone updates it in place without evicting anyone. */
    assert(save(5) && known(2) && known(5) && known(9));
    assert(save(10) && !known(2) && known(3) && known(10));
    assert(open_handles == 0);
    puts("AirPlay pairing store: lookup, in-place update and oldest-first eviction passed");
}
