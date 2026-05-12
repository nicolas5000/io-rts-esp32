#pragma once

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_WEB_ENABLED
void web_server_start(void *ioRtsManager);
void web_server_broadcast_position(const char *device_id, int position, bool is_stopped);
void web_server_broadcast_log(const char *message);
#else
static inline void web_server_start(void *m) { (void)m; }
static inline void web_server_broadcast_position(const char *id, int pos, bool s) { (void)id; (void)pos; (void)s; }
static inline void web_server_broadcast_log(const char *msg) { (void)msg; }
#endif

#ifdef __cplusplus
}
#endif
