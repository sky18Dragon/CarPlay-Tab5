/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* Loads or creates a persistent AirPlay accessory identity and registers the
 * Pair-Setup/Pair-Verify, /info, and Bonjour configuration provider. */
esp_err_t airplay_software_provider_init(void);

/* Selects which drive side /info reports from the next CarPlay session on:
 * right-hand drive puts the CarPlay dock on the right. Safe to call any time. */
void airplay_software_provider_set_right_hand_drive(bool right_hand_drive);
