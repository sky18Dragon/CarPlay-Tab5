#include <assert.h>
#include <stdio.h>
#include "airplay_media_keys.h"

int main(void)
{
    airplay_media_keys_t keys = {0};
    /* A session: main audio, screen, then many navigation prompts. */
    assert(airplay_media_keys_record(&keys, 1, 100));
    assert(airplay_media_keys_record(&keys, 2, 110));
    for (uint64_t prompt = 0; prompt < 200; ++prompt) {
        const uint64_t id = 1000 + prompt;
        assert(airplay_media_keys_check(&keys, id, 101) == AIRPLAY_MEDIA_KEY_NEW);
        assert(airplay_media_keys_have_room(&keys, 1));
        assert(airplay_media_keys_record(&keys, id, 101));
        assert(airplay_media_keys_check(&keys, id, 101) == AIRPLAY_MEDIA_KEY_ACTIVE);
        airplay_media_keys_retire_type(&keys, 101);
        assert(airplay_media_keys_check(&keys, id, 101) == AIRPLAY_MEDIA_KEY_REJECTED);
    }
    /* Active streams are never evicted. */
    assert(airplay_media_keys_check(&keys, 1, 100) == AIRPLAY_MEDIA_KEY_ACTIVE);
    assert(airplay_media_keys_check(&keys, 2, 110) == AIRPLAY_MEDIA_KEY_ACTIVE);
    /* The most recent retired IDs stay blocked. */
    assert(airplay_media_keys_check(&keys, 1199, 101) == AIRPLAY_MEDIA_KEY_REJECTED);
    /* Type change on a live ID is refused. */
    assert(airplay_media_keys_check(&keys, 1, 101) == AIRPLAY_MEDIA_KEY_REJECTED);
    /* Only active streams count against capacity. */
    airplay_media_keys_t full = {0};
    for (uint64_t id = 1; id <= AIRPLAY_MEDIA_KEYS_MAX; ++id) assert(airplay_media_keys_record(&full, id, 101));
    assert(!airplay_media_keys_have_room(&full, 1));
    assert(!airplay_media_keys_record(&full, 99, 101));
    airplay_media_keys_retire_all(&full);
    assert(airplay_media_keys_have_room(&full, AIRPLAY_MEDIA_KEYS_MAX));
    assert(airplay_media_keys_record(&full, 99, 101));
    /* A new screen stream retires the previous one of its type. */
    airplay_media_keys_t screen = {0};
    assert(airplay_media_keys_record(&screen, 5, 110));
    assert(airplay_media_keys_record(&screen, 6, 110));
    assert(airplay_media_keys_check(&screen, 5, 110) == AIRPLAY_MEDIA_KEY_REJECTED);
    puts("media keys: 200 navigation prompts, eviction, capacity and reuse checks passed");
    return 0;
}
