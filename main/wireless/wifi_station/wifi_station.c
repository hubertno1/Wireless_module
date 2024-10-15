#include "stdbool.h"
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "wifi_station.h"


#define DEFAULT_WIFI_SSID           "CU_2-403"
#define DEFAULT_WIFI_PASSWORD       "88888888"

// 测试用wifi，便于利用wireshark抓包分析
//#define DEFAULT_WIFI_SSID           "laptop-y7000"
//#define DEFAULT_WIFI_PASSWORD       "H5/a9170"

static const char *TAG = "WiFi Station";

static TimerHandle_t wifi_retry_timer;                                         // 定义一个定时器句柄
// static wifi_station_connected_cb_t wifi_station_connected_cb = NULL;           // 定义一个wifi连接成功的回调函数  
static wifi_station_disconnected_cb_t wifi_station_disconnected_cb = NULL;     // 定义一个wifi断开连接的回调函数

// 定义回调函数数组和计数器
static wifi_station_connected_cb_t wifi_station_connected_cbs[MAX_WIFI_CONNECTED_CALLBACKS] = {0};
static int wifi_station_connected_cb_cnt = 0;


static void wifi_retry_timer_handler(TimerHandle_t timer)
{
    ESP_LOGI(TAG, "retry to connect wifi");
    esp_wifi_connect();
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{   
    // 被触发当wifi驱动成功启动（esp_wifi_start()被成功调用）
    // 调用esp_wifi_connect()开启连接
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Connecting to AP" DEFAULT_WIFI_SSID);
        esp_wifi_connect();
    }
    // 被触发当wifi sta模式连接断开
    // 调用已注册的断开连接的回调函数
    // 启动重新连接定时器，在5s后尝试重新连接
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Connected failed, retry in 5s");

        if (wifi_station_disconnected_cb)
            wifi_station_disconnected_cb();

        xTimerStart(wifi_retry_timer, 0);
    }
    // 被触发当wifi sta模式成功获取ip地址
    // 从event_data中获取ip地址，并记录日志
    // 调用已注册的连接成功的回调函数
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));

        // 调用所有注册的回调函数
        for (int  i = 0; i < wifi_station_connected_cb_cnt; i++)
        {
            // 在回调函数指针不为NULL时，调用该函数，避免引用空指针，跑飞程序。
            if (wifi_station_connected_cbs[i])
                wifi_station_connected_cbs[i]();
        }
        
        //if (wifi_station_connected_cb)
        //    wifi_station_connected_cb(); 
    }

}

void wifi_station_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi Station");

    // 初始化底层的TCP/IP协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 注册wifi事件处理函数
    // 注册wifi_event_handler对于WIFI_EVENT with ESP_EVENT_ANY_ID事件以处理所有与WiFi相关的事件
    // 注册wifi_event_handler对于IP_EVENT with IP_EVENT_STA_GOT_IP事件以处理WiFi连接成功后获取到IP地址的事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));


    // 配置wifi
    const wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_config = 
    {
        .sta = 
        {
            .ssid = DEFAULT_WIFI_SSID,
            .password = DEFAULT_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,       // 这意味着STA将仅连接到具有WPA/WPA2安全性的网络
        },
    };

    // 创建一个具有5秒超时的wifi_retry_timer定时器，用于断开连接后安排重新连接尝试
    // 注意：这个timer是非重复的，因此每次都需要重新启动
    wifi_retry_timer = xTimerCreate("wifi_retry_timer", pdMS_TO_TICKS(5000), pdFALSE, NULL, wifi_retry_timer_handler);
    
    // 创建默认的wifi station网络接口
    esp_netif_create_default_wifi_sta();

    // 初始化wifi驱动程序
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    // 设置wifi模式为STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // 配置wifi接口用提供的wifi_config
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // 启动wifi, 这个函数会触发WIFI_EVENT_STA_START事件
    ESP_ERROR_CHECK(esp_wifi_start());

}

void wifi_station_set_connected_cb(wifi_station_connected_cb_t cb)
{

    // 添加回调函数到数组中
    wifi_station_connected_cbs[wifi_station_connected_cb_cnt++] = cb;

}

void wifi_station_set_disconnected_cb(wifi_station_disconnected_cb_t cb)
{
    wifi_station_disconnected_cb = cb;
}

