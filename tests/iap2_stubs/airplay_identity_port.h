#pragma once
#include "esp_err.h"
typedef struct { const char *device_identifier,*public_key,*source_version; } airplay_identity_config_t;
esp_err_t airplay_identity_port_get_config(airplay_identity_config_t *);
