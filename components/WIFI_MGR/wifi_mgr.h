#ifndef WIFI_MGR_H
#define WIFI_MGR_H

#include <stdbool.h>

void wifi_mgr_init(void);
bool wifi_mgr_is_connected(void);
bool wifi_mgr_is_time_synced(void);

#endif
