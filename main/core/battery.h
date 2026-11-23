#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Tab5 battery: one 2-cell (2S) Li-ion pack such as the NP-F550, measured on the
 * battery rail by the INA226. Single owner; time is monotonic milliseconds. */
typedef enum { CP_BATTERY_UNKNOWN, CP_BATTERY_OK, CP_BATTERY_LOW, CP_BATTERY_EMPTY } cp_battery_level;
typedef struct {
    uint32_t smoothed_mv;     /* Filtered pack voltage; 0 until the first sample. */
    unsigned empty_samples;   /* Consecutive samples at or below the empty line. */
    bool charging;
    bool warned;              /* Stays set until the pack recovers past the low line. */
    cp_battery_level level;
} cp_battery;
/* Discharging pack voltage that counts as empty: 3.15 V per cell under load,
 * above the board's own 6.0 V cut-off. */
#define CP_BATTERY_EMPTY_MV 6300u
/* Samples in a row at or below the empty line before shutting down, so a load
 * spike or a bad reading cannot power the Tab5 off. */
#define CP_BATTERY_EMPTY_SAMPLES 6u
#define CP_BATTERY_LOW_PERCENT 15u
void cp_battery_init(cp_battery *b);
/* Feeds one reading. charging: the pack is taking charge (USB-C power present). */
cp_battery_level cp_battery_sample(cp_battery *b, uint32_t pack_mv, bool charging);
/* State of charge from the filtered voltage, 0 to 100, or -1 before a sample. */
int cp_battery_percent(const cp_battery *b);
/* Same curve for any pack voltage. */
unsigned cp_battery_percent_for_mv(uint32_t pack_mv);
#ifdef __cplusplus
}
#endif
