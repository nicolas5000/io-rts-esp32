#pragma once
#include "sdkconfig.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_OLED_ENABLED

esp_err_t oled_init(void);

/* Screen layout (rows 0-7):
 *  0  "[io-logo] control      □"  title + network-status icon (ETH: RJ45,
 *  1  "--------------------"       WiFi: RSSI bars or ?)
 *  2  "AABBCC ↑CMD ↓FE ███"    device line 0 (separator → 2px gap →)
 *  3  "AABBCC ↓FE      ███"    device line 1
 *  4  "AABBCC ↑CMD"            device line 2
 *  5  "AABBCC ↑CMD ↓FE ███"    device line 3
 *  6  "--------------------"   separator
 *  7  "Ready"                  transient status msg
 *
 * Each device line occupies a full 8-pixel page but only uses the top 6 rows,
 * leaving 2 blank pixel rows at the bottom for vertical spacing.
 * Layout within a line: [device name] [TX icon+cmd] [RX icon+cmd] [RSSI bars]
 *   TX/RX parts hidden when no data available for that direction.
 *   RSSI bars right-aligned, shown with RX.
 */
void oled_show_tx(const char *cmd_name, const char *device_id);
void oled_show_rx(const char *device_id, const char *cmd_hex,
                  int position_pct, int rssi);
void oled_show_status(const char *msg);

#else

static inline esp_err_t oled_init(void)                           { return ESP_OK; }
static inline void      oled_show_tx(const char *n, const char *d)            { (void)n; (void)d; }
static inline void      oled_show_rx(const char *d, const char *c, int p, int r) { (void)d; (void)c; (void)p; (void)r; }
static inline void      oled_show_status(const char *m)                       { (void)m; }

#endif

#ifdef __cplusplus
}
#endif
