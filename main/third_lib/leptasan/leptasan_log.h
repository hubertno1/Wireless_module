#ifndef __LEPTASAN_LOG_H__
#define __LEPTASAN_LOG_H__

#include "esp_log.h"

#define LEPTASAN_LOG_TAG            "LEPTASAN_REPORT_ERROR"
#define LEPTASAN_LOG(format, ...)   ESP_LOGE(LEPTASAN_LOG_TAG, format, ##__VA_ARGS__)


#endif /* __LEPTASAN_LOG_H__ */
