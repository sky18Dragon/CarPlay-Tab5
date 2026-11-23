#include "battery.h"
#include <string.h>
/* Per-cell voltage against remaining charge for a Li-ion cell under a moderate
 * load. Coarse by nature: it is for a percentage display and warning, not a fuel
 * gauge. Doubled for the 2S pack. */
static const struct { uint16_t cell_mv; uint8_t percent; } curve[] = {
    {3300, 0}, {3500, 8}, {3600, 15}, {3700, 35}, {3800, 55},
    {3900, 70}, {4000, 84}, {4100, 96}, {4150, 100},
};
#define CURVE_POINTS (sizeof(curve) / sizeof(curve[0]))
void cp_battery_init(cp_battery *b)
{
    memset(b, 0, sizeof(*b));
    b->level = CP_BATTERY_UNKNOWN;
}
unsigned cp_battery_percent_for_mv(uint32_t pack_mv)
{
    const uint32_t cell = pack_mv / 2u;
    if (cell <= curve[0].cell_mv) return 0;
    if (cell >= curve[CURVE_POINTS - 1].cell_mv) return 100;
    for (unsigned i = 1; i < CURVE_POINTS; ++i) {
        if (cell > curve[i].cell_mv) continue;
        const uint32_t span = curve[i].cell_mv - curve[i - 1].cell_mv;
        const uint32_t into = cell - curve[i - 1].cell_mv;
        return curve[i - 1].percent + (curve[i].percent - curve[i - 1].percent) * into / span;
    }
    return 100;
}
int cp_battery_percent(const cp_battery *b)
{
    return b->smoothed_mv ? (int)cp_battery_percent_for_mv(b->smoothed_mv) : -1;
}
cp_battery_level cp_battery_sample(cp_battery *b, uint32_t pack_mv, bool charging)
{
    /* Out-of-range readings (no pack, bus error) are ignored, not averaged in. */
    if (pack_mv < 3000u || pack_mv > 9500u) return b->level;
    b->charging = charging;
    /* Load changes move the voltage by hundreds of millivolts within seconds;
     * a quarter-weight average keeps the percentage from jumping. */
    b->smoothed_mv = b->smoothed_mv ? (b->smoothed_mv * 3u + pack_mv) / 4u : pack_mv;
    /* Shutting down uses the raw reading, held for several samples in a row:
     * the average would lag a pack that is genuinely collapsing. */
    if (!charging && pack_mv <= CP_BATTERY_EMPTY_MV) ++b->empty_samples;
    else b->empty_samples = 0;
    const unsigned percent = cp_battery_percent_for_mv(b->smoothed_mv);
    /* Hysteresis: once warned, stay warned until 5 points above the line. */
    if (charging || percent >= CP_BATTERY_LOW_PERCENT + 5u) b->warned = false;
    else if (percent < CP_BATTERY_LOW_PERCENT) b->warned = true;
    if (b->empty_samples >= CP_BATTERY_EMPTY_SAMPLES) b->level = CP_BATTERY_EMPTY;
    else if (b->warned) b->level = CP_BATTERY_LOW;
    else b->level = CP_BATTERY_OK;
    return b->level;
}
