#ifndef MCU_LOG_H_
#define MCU_LOG_H_

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_TAG "MCU_LOG"

#define McuLog_fatal(format, ...)   ESP_LOGE(LOG_TAG, format, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* MCU_LOG_H_ */
