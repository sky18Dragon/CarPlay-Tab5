// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "nvs.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Phone trust records in the "tab5_pair" NVS namespace.
 *
 * A record is one ~6.8 KiB identity blob under a 'p' + 14 hex key. NVS keeps the
 * old and the new version of a blob during an update, so the 24 KiB partition
 * cannot rewrite one record while a second phone's record is also stored.
 *
 * - The trust state lives in its own one-entry 't' + 14 hex key, so confirming
 *   Trust never rewrites the identity blob.
 * - When a record does not fit, records of other phones are evicted and the
 *   write is retried once. Those phones only have to confirm Trust again. */
#define TAB5_PAIR_KEY_SIZE 16
/* key is the record key ('p' + 14 hex). evicted reports removed entries. */
esp_err_t tab5_pair_store_save(const char *key, const void *record, size_t size, unsigned *evicted);
esp_err_t tab5_pair_store_set_trusted(const char *key, bool trusted);
/* Returns fallback when no separate trust state exists (records from v1.0.0). */
bool tab5_pair_store_trusted(const char *key, bool fallback);
#ifdef __cplusplus
}
#endif
