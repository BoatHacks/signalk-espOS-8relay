// Wired Ethernet over the board's W5500 (SPI), reported into espOS's network
// layer the same way espOS's own espos_eth does for an internal MAC. espOS
// prefers Ethernet over WiFi whenever this link has an address.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up SPI, the W5500 driver, its netif and DHCP. Call after
// espos_start(). Returns once the driver runs; the link and address arrive
// later. A missing cable is not an error. Idempotent.
esp_err_t eth_w5500_start(void);

// True while the W5500 reports a link, whether or not it has an address.
bool eth_w5500_link_up(void);

#ifdef __cplusplus
}
#endif
