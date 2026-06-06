#ifndef SNTP_HANDLE_H
#define SNTP_HANDLE_H

#ifdef __cplusplus
extern "C" {
#endif

// Where the SNTP client gets the NTP server from.
// Values are persisted in config_store().ntp_mode.
typedef enum {
    NTP_MODE_PRUSA = 0, // the default prusa3d.pool.ntp.org (needs internet access)
    NTP_MODE_DHCP = 1, // NTP server provided by DHCP (option 42)
    NTP_MODE_CUSTOM = 2, // user-configured hostname or IP address
} ntp_mode_t;

void sntp_client_init(void);
void sntp_client_step(ntp_mode_t mode, const char *custom_server);

#ifdef __cplusplus
}
#endif

#endif // SNTP_HANDLE_H
