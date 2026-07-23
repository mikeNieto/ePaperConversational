#ifndef USER_CONFIG_SECRETS_H
#define USER_CONFIG_SECRETS_H

/* Multi-WiFi list. Device scans and connects to the strongest known network.
   Format: { "SSID", "password" },
   Add as many entries as needed. Update WIFI_NETWORK_COUNT to match. */
#define WIFI_NETWORKS \
    {"MyHomeWiFi", "password1"}, \
    {"MyOfficeWiFi", "password2"}, \
    {"MyPhoneHotspot", "password3"}

#define WIFI_NETWORK_COUNT 3

#define API_BASE_URL "MY_API_BASE_URL"

#endif
