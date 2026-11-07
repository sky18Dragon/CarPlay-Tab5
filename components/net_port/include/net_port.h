// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_netif.h"
#include "lwip/sockets.h"
// Legacy upstream name; this adapter returns the wired NCM interface, never Wi-Fi.
esp_netif_t *net_port_get_ap_netif(void);
bool net_port_local_address(struct sockaddr_in6 *address,uint16_t port);
