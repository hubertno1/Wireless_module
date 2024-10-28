#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "board.h"
#include "uart_manager.h"
#include "wifi_station.h"
#include "get_weather.h"
#include "sync_time.h"
#include "crc32.h"
#include "update_firmware.h"
#include "McuASAN.h"


static void trigger_asan_error() 
{
  int *p = (int *)malloc(sizeof(int));
  *p = 42;
  free(p);
  *p = 43;  // 错误:在释放内存后使用
}


void app_main(void)
{
    // to do: 移植asan
    McuASAN_Init();

    // 触发一个 ASAN 错误
     trigger_asan_error();

    // 初始化板级资源nvs
    board_init();

    // 创建一个默认系统事件调度循环，之后可以注册回调函数来处理系统的一些事件
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 初始化UART模块
    uart_manager_init();

    // 初始化天气模块
    weather_manager_init();

    // 时间同步功能
    sync_time_init();

    // 固件升级功能
    update_fw_init();

    // 初始化WIFI模块
    wifi_station_init();

}