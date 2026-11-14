#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Offline log recorder for field tests without a computer.
 * Every ESP_LOG line is kept in a 128 KB PSRAM ring that survives a software
 * reset, and a background task appends it to /sdcard/TAB5LOG/LOGnnnnn.TXT.
 * A panic records its reason and registers in internal RAM before the reboot;
 * the next boot logs them first. Without a card the firmware runs unchanged. */
void tab5_log_init(void);
/* Marks a notable moment (disconnect, recovery) and saves the ring at once. */
void tab5_log_event(const char *what);
#ifdef __cplusplus
}
#endif
