#include "core/battery.h"
#include <assert.h>
#include <stdio.h>
static cp_battery_level feed(cp_battery *b, uint32_t mv, bool charging, unsigned times)
{
    cp_battery_level level = CP_BATTERY_UNKNOWN;
    for (unsigned i = 0; i < times; ++i) level = cp_battery_sample(b, mv, charging);
    return level;
}
int main(void)
{
    /* Curve: ends, midpoints, monotonic. */
    assert(cp_battery_percent_for_mv(6000) == 0 && cp_battery_percent_for_mv(6600) == 0);
    assert(cp_battery_percent_for_mv(8300) == 100 && cp_battery_percent_for_mv(9000) == 100);
    assert(cp_battery_percent_for_mv(7400) == 35 && cp_battery_percent_for_mv(7600) == 55);
    for (uint32_t mv = 6000, last = 0; mv <= 8400; mv += 10) {
        const unsigned p = cp_battery_percent_for_mv(mv);
        assert(p >= last && p <= 100); last = p;
    }
    cp_battery b; cp_battery_init(&b);
    assert(cp_battery_percent(&b) == -1 && b.level == CP_BATTERY_UNKNOWN);
    /* Bad readings are ignored. */
    assert(cp_battery_sample(&b, 0, false) == CP_BATTERY_UNKNOWN && cp_battery_percent(&b) == -1);
    assert(feed(&b, 8100, false, 1) == CP_BATTERY_OK && cp_battery_percent(&b) == 90);
    /* Smoothing: one dip does not swing the percentage or warn. */
    assert(cp_battery_sample(&b, 7000, false) == CP_BATTERY_OK && cp_battery_percent(&b) > 70);
    /* Sustained discharge to low warns; hysteresis keeps it until clearly above. */
    assert(feed(&b, 7150, false, 20) == CP_BATTERY_LOW);
    assert(feed(&b, 7240, false, 20) == CP_BATTERY_LOW);   /* About 19%: still warned. */
    assert(feed(&b, 7400, false, 20) == CP_BATTERY_OK);    /* 35%: cleared. */
    /* Charging clears the warning at once. */
    assert(feed(&b, 7150, false, 20) == CP_BATTERY_LOW);
    assert(cp_battery_sample(&b, 7150, true) == CP_BATTERY_OK);
    /* Empty needs CP_BATTERY_EMPTY_SAMPLES discharging readings in a row. */
    cp_battery_init(&b);
    assert(feed(&b, 6200, false, CP_BATTERY_EMPTY_SAMPLES - 1) == CP_BATTERY_LOW);
    assert(cp_battery_sample(&b, 6500, false) == CP_BATTERY_LOW);       /* Recovery resets the count. */
    assert(feed(&b, 6200, false, CP_BATTERY_EMPTY_SAMPLES - 1) == CP_BATTERY_LOW);
    assert(cp_battery_sample(&b, 6200, false) == CP_BATTERY_EMPTY);
    /* Never empty while charging, however low the reading. */
    cp_battery_init(&b);
    assert(feed(&b, 6100, true, 50) == CP_BATTERY_OK);
    puts("battery: curve, smoothing, low hysteresis, charging and sustained-empty shutdown passed");
}
