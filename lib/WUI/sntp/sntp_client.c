#include "sntp.h"
#include "sntp_client.h"
#include "netif_settings.h"
#include "netdev.h"
#include "tcpip.h"

#include <lwip/dhcp.h>
#include <lwip/netif.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <option/has_esp.h>

static const char default_ntp_server[] = "prusa3d.pool.ntp.org";

// lwip's sntp stores only the pointer passed to sntp_setservername(),
// so the custom server name needs static storage.
static char custom_ntp_server[NTP_SERVER_LEN + 1] = "";

static bool config_applied = false;
static ntp_mode_t applied_mode = NTP_MODE_PRUSA;

static uint32_t sntp_running = 0; // describes if sntp is currently running or not

void sntp_client_init(void) {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);

    sntp_init();
}

// Renegotiate DHCP leases so a fresh ACK delivers the NTP server
// (option 42) immediately instead of at the next lease renewal.
// Must be called with the tcpip core locked.
static void renew_dhcp_leases(void) {
    struct netif *netif;
    NETIF_FOREACH(netif) {
        if (dhcp_supplied_address(netif)) {
            dhcp_renew(netif);
        }
    }
}

// Must be called with the tcpip core locked.
static void apply_config(ntp_mode_t mode, const char *custom_server) {
    if (sntp_running) {
        // Stop the client so the re-init below sends a request to the new server right away.
        sntp_stop();
        sntp_running = 0;
    }

    // Wipe whatever server was in use before (including a DHCP-provided address).
    sntp_setserver(0, NULL);
    sntp_servermode_dhcp(mode == NTP_MODE_DHCP);

    switch (mode) {
    case NTP_MODE_DHCP:
        renew_dhcp_leases();
        break;
    case NTP_MODE_CUSTOM:
        snprintf(custom_ntp_server, sizeof(custom_ntp_server), "%s", custom_server);
        sntp_setservername(0, custom_ntp_server);
        break;
    case NTP_MODE_PRUSA:
    default:
        sntp_setservername(0, default_ntp_server);
        break;
    }

    applied_mode = mode;
    config_applied = true;
}

static bool config_changed(ntp_mode_t mode, const char *custom_server) {
    if (!config_applied || mode != applied_mode) {
        return true;
    }

    return mode == NTP_MODE_CUSTOM && strcmp(custom_server, custom_ntp_server) != 0;
}

void sntp_client_step(ntp_mode_t mode, const char *custom_server) {
    if (config_changed(mode, custom_server)) {
        LOCK_TCPIP_CORE();
        apply_config(mode, custom_server);
        UNLOCK_TCPIP_CORE();
    }

    bool netif_up = netdev_get_status(NETDEV_ETH_ID) == NETDEV_NETIF_UP;
#if HAS_ESP()
    netif_up |= netdev_get_status(NETDEV_ESP_ID) == NETDEV_NETIF_UP;
#endif

    if (!sntp_running && netif_up) {
        LOCK_TCPIP_CORE();
        sntp_client_init();
        UNLOCK_TCPIP_CORE();
        sntp_running = 1;
    } else if (sntp_running && !netif_up) {
        LOCK_TCPIP_CORE();
        sntp_stop();
        if (mode == NTP_MODE_DHCP) {
            // A DHCP-provided server is only valid for the network that provided it -
            // invalidate it; the next network's DHCP will deliver a fresh one.
            sntp_setserver(0, NULL);
        }
        UNLOCK_TCPIP_CORE();
        sntp_running = 0;
    }
}
