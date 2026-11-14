#include "airplay_media_keys.h"
#include <string.h>

airplay_media_key_state_t airplay_media_keys_check(const airplay_media_keys_t *keys, uint64_t id, uint64_t type)
{
    for (unsigned k = 0; k < keys->count; ++k) {
        if (keys->keys[k].id != id) continue;
        return keys->keys[k].active && keys->keys[k].type == type ? AIRPLAY_MEDIA_KEY_ACTIVE : AIRPLAY_MEDIA_KEY_REJECTED;
    }
    return AIRPLAY_MEDIA_KEY_NEW;
}

bool airplay_media_keys_have_room(const airplay_media_keys_t *keys, unsigned extra)
{
    unsigned active = 0;
    for (unsigned k = 0; k < keys->count; ++k) active += keys->keys[k].active;
    return active + extra <= AIRPLAY_MEDIA_KEYS_MAX;
}

static bool evict_oldest_retired(airplay_media_keys_t *keys)
{
    for (unsigned k = 0; k < keys->count; ++k) {
        if (keys->keys[k].active) continue;
        memmove(&keys->keys[k], &keys->keys[k + 1], (keys->count - k - 1) * sizeof(keys->keys[0]));
        --keys->count;
        return true;
    }
    return false;
}

bool airplay_media_keys_record(airplay_media_keys_t *keys, uint64_t id, uint64_t type)
{
    bool known = false;
    for (unsigned k = 0; k < keys->count; ++k) {
        if (keys->keys[k].id == id) known = true;
        else if (type >= 110 && keys->keys[k].type == type) keys->keys[k].active = false;
    }
    if (known) return true;
    if (keys->count == AIRPLAY_MEDIA_KEYS_MAX && !evict_oldest_retired(keys)) return false;
    keys->keys[keys->count++] = (airplay_media_key_t){ .id = id, .type = type, .active = true };
    return true;
}

void airplay_media_keys_retire_type(airplay_media_keys_t *keys, uint64_t type)
{
    for (unsigned k = 0; k < keys->count; ++k) if (keys->keys[k].type == type) keys->keys[k].active = false;
}

void airplay_media_keys_retire_all(airplay_media_keys_t *keys)
{
    for (unsigned k = 0; k < keys->count; ++k) keys->keys[k].active = false;
}
