#pragma once
#include <Arduino.h>
typedef enum { SNTP_SYNC_STATUS_RESET = 0, SNTP_SYNC_STATUS_COMPLETED = 1, SNTP_SYNC_STATUS_IN_PROGRESS = 2 } sntp_sync_status_t;
sntp_sync_status_t sntp_get_sync_status();
