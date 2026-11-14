#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Stream connection IDs seen in one RTSP session. Each ID derives a stream key,
 * so an ID that was torn down must not be set up again. Navigation prompts open
 * a new type-101 stream every time, so retired IDs are evicted oldest first when
 * the table is full; only active streams limit how many can be admitted. */
#define AIRPLAY_MEDIA_KEYS_MAX 32u
typedef struct { uint64_t id, type; bool active; } airplay_media_key_t;
typedef struct { airplay_media_key_t keys[AIRPLAY_MEDIA_KEYS_MAX]; unsigned count; } airplay_media_keys_t;
typedef enum { AIRPLAY_MEDIA_KEY_NEW, AIRPLAY_MEDIA_KEY_ACTIVE, AIRPLAY_MEDIA_KEY_REJECTED } airplay_media_key_state_t;
/* NEW: unseen ID. ACTIVE: same live stream set up again. REJECTED: retired ID or type change. */
airplay_media_key_state_t airplay_media_keys_check(const airplay_media_keys_t *keys, uint64_t id, uint64_t type);
/* True if `extra` new IDs fit once retired entries are evicted. */
bool airplay_media_keys_have_room(const airplay_media_keys_t *keys, unsigned extra);
/* Records a started stream. A new screen stream (type >= 110) retires older keys of its type. */
bool airplay_media_keys_record(airplay_media_keys_t *keys, uint64_t id, uint64_t type);
void airplay_media_keys_retire_type(airplay_media_keys_t *keys, uint64_t type);
void airplay_media_keys_retire_all(airplay_media_keys_t *keys);
