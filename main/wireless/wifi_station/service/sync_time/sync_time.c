#include <stdbool.h>
#include "sync_time.h"
#include "wifi_station.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_netif.h"
#include "lwip/ip_addr.h"
#include "time.h"


static const char* TAG = "SYNC_TIME";

static time_info_t synced_time_info;

static SemaphoreHandle_t sntp_semaphore = NULL;
static SemaphoreHandle_t synced_time_mutex = NULL;

// 定义回调函数指针
static time_synced_cb_t time_synced_cb;

// 实现了注册回调函数的接口
void set_time_synced_cb(time_synced_cb_t cb)
{
    time_synced_cb = cb;
}

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronization event received");

    if (sntp_semaphore)
        xSemaphoreGive(sntp_semaphore);
}


static void sync_time_task(void *pvParameters)
{
    // const char* servers[] = {"xxx.xxx", "pool.ntp.org", "ntp.ntsc.ac.cn", "cn.pool.ntp.org"};        // 测试用NTP服务器，第一个是无效的，用来测试失败的情况
    const char* servers[] = {"cn.pool.ntp.org", "ntp.ntsc.ac.cn", "pool.ntp.org"};
    int server_index = 0;                                               
    const int server_count = sizeof(servers) / sizeof(servers[0]);      
    bool time_synced = false;                                           // 时间同步标志位   

    // 创建信号量，用于同步时间同步成功的事件
    sntp_semaphore = xSemaphoreCreateBinary();
    if (sntp_semaphore == NULL)
    {
        ESP_LOGE(TAG, "Failed to create semaphore");
        vTaskDelete(NULL);
    }

    // 创建互斥锁
    synced_time_mutex = xSemaphoreCreateMutex();
    if (synced_time_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        vSemaphoreDelete(sntp_semaphore);
        vTaskDelete(NULL);
    }


    // 循环，尝试与每一个NTP服务器同步时间，直到成功或者尝试完所有服务器
    while (server_index < server_count && !time_synced)
    {
        ESP_LOGI(TAG, "Attempting to sync time with server: %s", servers[server_index]);

        // 配置SNTP
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);                        // 将SNTP配置为轮询模式
        esp_sntp_setservername(0, servers[server_index]);                   // 配置多个主NTP服务器 
        sntp_set_time_sync_notification_cb(time_sync_notification_cb);      // 注册系统时间同步回调函数

        // 启动SNTP
        esp_sntp_init();

        // 等待时间同步完成
        if (xSemaphoreTake(sntp_semaphore, pdMS_TO_TICKS(5000)) == pdTRUE)
        {
            time_synced = true;
            ESP_LOGI(TAG, "Time synchronized with server: %s", servers[server_index]);

            // 设置时区，后面最好改成根据GPS信息，自动设置时区
            setenv("TZ", "CST-8", 1);
            tzset();

            time_t now = 0;
            struct tm timeinfo = {0};
            time(&now);
            localtime_r(&now, &timeinfo);

            // 获取互斥锁，更新时间信息
            if (xSemaphoreTake(synced_time_mutex, portMAX_DELAY) == pdTRUE)
            {
                synced_time_info.year = timeinfo.tm_year + 1900;
                synced_time_info.month = timeinfo.tm_mon + 1;
                synced_time_info.day = timeinfo.tm_mday;
                synced_time_info.hour = timeinfo.tm_hour;
                synced_time_info.minute = timeinfo.tm_min;
                synced_time_info.second = timeinfo.tm_sec;

                // 打印同步的时间信息
                ESP_LOGI(TAG, "Time synchronized: %02d-%02d-%02d %02d:%02d:%02d", 
                        synced_time_info.year, synced_time_info.month, synced_time_info.day, 
                        synced_time_info.hour, synced_time_info.minute, synced_time_info.second);

                // 调用回调函数，将同步的时间信息传递给uart_manager模块
                if (time_synced_cb)
                    time_synced_cb(&synced_time_info);

                // 释放互斥锁
                xSemaphoreGive(synced_time_mutex);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to take mutex");
            }

        }
        else
        {
            ESP_LOGW(TAG, "Failed to sync time with server: %s", servers[server_index]);
            server_index++;   // 切换到下一个服务器
        }

        // 停止SNTP
        esp_sntp_stop();
    }

    // 所有的NTP服务器都尝试完了，但是时间还是没有同步
    if (!time_synced)
    {
        ESP_LOGE(TAG, "Failed to synchronize time with all servers");
    }

    // 删除互斥锁
    if (synced_time_mutex)
    {
        vSemaphoreDelete(synced_time_mutex);
        synced_time_mutex = NULL;
    }

    // 删除任务
    vTaskDelete(NULL);    
}

static void sync_time_handler(void)
{
    // 处理WiFi成功连接后的逻辑
    ESP_LOGI(TAG, "WiFi connected, start to sync time");

    // 创建一个任务，用来同步时间
    xTaskCreate(sync_time_task, "sync_time_task", 4096, NULL, 8, NULL);
}

void sync_time_init(void)
{
    // 注册WiFi连接成功的回调函数
    wifi_station_set_connected_cb(sync_time_handler);
}
