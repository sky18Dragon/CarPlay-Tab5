// SPDX-License-Identifier: GPL-3.0-or-later
#include "net_port.h"
#include <string.h>
extern esp_netif_t *carplay_netif_handle(void);
esp_netif_t *net_port_get_ap_netif(void) { return carplay_netif_handle(); }
bool net_port_local_address(struct sockaddr_in6 *address,uint16_t port) {
    esp_netif_t *netif=carplay_netif_handle();
    esp_ip6_addr_t ip;
    if (!netif || !address || esp_netif_get_ip6_linklocal(netif,&ip)!=ESP_OK) return false;
    memset(address,0,sizeof(*address));
    address->sin6_family=AF_INET6; address->sin6_port=htons(port);
    address->sin6_scope_id=esp_netif_get_netif_impl_index(netif);
    memcpy(&address->sin6_addr,ip.addr,16);
    return true;
}
