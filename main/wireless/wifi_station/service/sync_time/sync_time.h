#ifndef __SYNC_TIME_H__
#define __SYNC_TIME_H__

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} time_info_t; 

void sync_time_init(void);

// 定义时间同步完成的回调函数类型
typedef void (*time_synced_cb_t)(const time_info_t *time_info);

// 暴露给外部的接口，用于设置时间同步完成的回调函数
void set_time_synced_cb(time_synced_cb_t cb);

#endif /* __SYNC_TIME_H__ */
