#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "get_weather.h"
#include "wifi_station.h"
#include "freertos/semphr.h"
#include "esp_crt_bundle.h"
// #include "McuASAN.c"


static SemaphoreHandle_t location_semaphore = NULL;     // 用于同步位置和天气任务的信号量


static const char* TAG_LOCATION = "LOCATION";
static const char* TAG_HTTP = "HTTP";

#define CONFIG_WEATHER_API_KEY "S_8gKKyCi8HmNXw_U"  // 心知天气API密钥
// 心知天气私钥，用来构成请求URL获取天气信息
// #define WEATHER_PRIVATE_KEY "S_8gKKyCi8HmNXw_U"
// https://api.seniverse.com/v3/weather/now.json?key=S_8gKKyCi8HmNXw_U&location=Huangshan&language=zh-Hans&unit=c


#define MAX_OUTPUT_BUFFER_LEN 2048
static char output_buffer[MAX_OUTPUT_BUFFER_LEN] = {0};
static int output_len = 0;

static char city_name[128];       // 用来保存城市名称

#define CLIENT_HTTP_RECEIVE_BIT BIT0
static  EventGroupHandle_t s_client_http_event_group = NULL;            // 用于同步HTTP接收完成的事件组


static esp_err_t http_client_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id)
    {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_HEADER");
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // 将接收到的数据拷贝到output_buffer中
            if (output_len + evt->data_len < MAX_OUTPUT_BUFFER_LEN)
            {
                memcpy(output_buffer + output_len, evt->data, evt->data_len);
                output_len += evt->data_len;
            }
            else
            {
                ESP_LOGE(TAG_HTTP, "output buffer is full, data will be truncated");
                output_len = MAX_OUTPUT_BUFFER_LEN;

            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_ON_FINISH");
            // 在缓冲区末尾添加字符串结束符
            output_buffer[output_len] = '\0';
            xEventGroupSetBits(s_client_http_event_group, CLIENT_HTTP_RECEIVE_BIT);
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_HTTP, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
        

    }
    return ESP_OK;
}


esp_err_t init_http_client(void)
{
    // 定义http配置结构体
    esp_http_client_config_t config = {
        .url = "http://ip-api.com/json/?lang=en",           // 获取地理位置
        .skip_cert_common_name_check = true,                // 跳过证书检查
        .method = HTTP_METHOD_GET,                          // 使用GET方法请求数据
        .event_handler = http_client_event_handler,         // 设置事件处理函数
    };

    // 1. 初始化HTTP客户端
    esp_http_client_handle_t http_client = esp_http_client_init(&config);
    if (http_client == NULL)
    {
        ESP_LOGE(TAG_HTTP, "Failed to init HTTP client!");
        return ESP_FAIL;
    }

    // 2. 发起HTTP请求
    esp_err_t err = esp_http_client_perform(http_client);
    if (err !=  ESP_OK)
    {
        ESP_LOGE(TAG_HTTP, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(http_client);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// 解析位置信息
static esp_err_t parse_location(char* location_js)
{
    cJSON *lo_js = cJSON_Parse(location_js);
    if(!lo_js)
    {
        ESP_LOGI(TAG_LOCATION,"invaild json format");
        return ESP_FAIL;
    }
    cJSON *city_js = cJSON_GetObjectItem(lo_js,"city");
    if(!city_js)
    {
        ESP_LOGI(TAG_LOCATION, "City not found in JSON");
        cJSON_Delete(lo_js);
        return ESP_FAIL;
    }
    snprintf(city_name,sizeof(city_name),"%s",cJSON_GetStringValue(city_js));
    cJSON_Delete(lo_js);
    return ESP_OK;
}

// 解析天气信息
static esp_err_t parse_weather(char *weather_json)
{
    cJSON *root = cJSON_Parse(weather_json);
    if (!root)
    {
        ESP_LOGE(TAG_HTTP, "Invalid JSON format");
        return ESP_FAIL;
    }

    cJSON *results = cJSON_GetObjectItem(root, "results");
    if (!results || !cJSON_IsArray(results))
    {
        ESP_LOGE(TAG_HTTP, "No 'results' array found in JSON");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *first_result = cJSON_GetArrayItem(results, 0);
    if (!first_result)
    {
        ESP_LOGE(TAG_HTTP, "No result item found");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *now = cJSON_GetObjectItem(first_result, "now");
    if (!now)
    {
        ESP_LOGE(TAG_HTTP, "No 'now' field found");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *text = cJSON_GetObjectItem(now, "text");
    cJSON *temperature = cJSON_GetObjectItem(now, "temperature");

    if (text && temperature)
    {
        const char *weather_text = cJSON_GetStringValue(text);
        const char *temp = cJSON_GetStringValue(temperature);
        ESP_LOGI(TAG_HTTP, "Current weather in %s: %s, %s°C", city_name, weather_text, temp);
    }
    else
    {
        ESP_LOGE(TAG_HTTP, "Failed to parse weather data");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t fetch_weather(const char *city)
{
    // 构建请求 URL
    char request_url[256];
    snprintf(request_url, sizeof(request_url),
             "https://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=zh-Hans&unit=c",
             CONFIG_WEATHER_API_KEY, city);

    // 创建事件组，用于同步 HTTP 接收完成的事件
    s_client_http_event_group = xEventGroupCreate();
    if (s_client_http_event_group == NULL)
    {
        ESP_LOGE(TAG_HTTP, "Failed to create event group");
        return ESP_FAIL;
    }

    // 清空接收缓冲区和长度
    memset(output_buffer, 0, MAX_OUTPUT_BUFFER_LEN);
    output_len = 0;

    // 定义 HTTP 配置结构体
    esp_http_client_config_t config = {
        .url = request_url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_client_event_handler,
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,  // 使用证书捆绑包
#endif
    };

    // 初始化 HTTP 客户端
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG_HTTP, "Failed to init HTTP client!");
        vEventGroupDelete(s_client_http_event_group);
        return ESP_FAIL;
    }

    // 发起 HTTP 请求
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_HTTP, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        vEventGroupDelete(s_client_http_event_group);
        return ESP_FAIL;
    }

    // 等待数据接收完成
    EventBits_t bits = xEventGroupWaitBits(s_client_http_event_group, CLIENT_HTTP_RECEIVE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & CLIENT_HTTP_RECEIVE_BIT)
    {
        ESP_LOGI(TAG_HTTP, "Weather data received, len: %d", output_len);
        // 解析天气数据
        if (parse_weather(output_buffer) == ESP_OK)
        {
            ESP_LOGI(TAG_HTTP, "Weather data parsed successfully");
            err = ESP_OK;
        }
        else
        {
            ESP_LOGE(TAG_HTTP, "Failed to parse weather data");
            err = ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGE(TAG_HTTP, "Data receive timeout");
        err = ESP_FAIL;
    }

    // 清理资源
    esp_http_client_cleanup(client);
    vEventGroupDelete(s_client_http_event_group);
    s_client_http_event_group = NULL;

    return err;
}



static void fetch_location_task(void *pvParameters)
{
    // 创建一个事件组，用于同步HTTP接收完成的事件
    s_client_http_event_group = xEventGroupCreate();
    if (s_client_http_event_group == NULL)
    {
        ESP_LOGE(TAG_LOCATION, "Failed to create event group");
        vTaskDelete(NULL);
        return;
    }

    //清空接收缓冲区和长度
    memset(output_buffer, 0, MAX_OUTPUT_BUFFER_LEN);
    output_len = 0;

    // 1. 初始化HTTP客户端，发起HTTP请求
    ESP_ERROR_CHECK(init_http_client());

    // 2. 等待数据接收完成，解析接收到的数据
    EventBits_t bits = xEventGroupWaitBits(s_client_http_event_group, CLIENT_HTTP_RECEIVE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & CLIENT_HTTP_RECEIVE_BIT)
    {
        ESP_LOGI(TAG_LOCATION, "Data received, len: %d", output_len);
        // 解析接收到的数据
        if (parse_location(output_buffer) == ESP_OK)
        {
            ESP_LOGI(TAG_LOCATION, "Location: %s", city_name);
        
            // 释放信号量，通知天气任务可以继续执行
            xSemaphoreGive(location_semaphore);
        }
        else
        {
            ESP_LOGE(TAG_LOCATION, "Failed to parse location");
        }

    }
    else
    {
        ESP_LOGE(TAG_LOCATION, "Data receive timeout");
    } 

    // 4. 清除事件组
    vEventGroupDelete(s_client_http_event_group);
    s_client_http_event_group = NULL;
    // 5. 删除任务
    vTaskDelete(NULL);
}

static void fetch_weather_task(void *pvParameters)
{
    // 等待信号量，超时时间设置为无限等待
    if (xSemaphoreTake(location_semaphore, portMAX_DELAY) == pdTRUE)
    {
        ESP_LOGI(TAG_HTTP, "Start to fetch weather for city: %s", city_name);

        // 获取天气信息
        if (fetch_weather(city_name) == ESP_OK)
        {
            ESP_LOGI(TAG_HTTP, "Weather fetched successfully");
        }
        else
        {
            ESP_LOGE(TAG_HTTP, "Failed to fetch weather");
        }
    }
    else
    {
        ESP_LOGE(TAG_HTTP, "Failed to get location");

    }

    if (location_semaphore != NULL)
    {
        vSemaphoreDelete(location_semaphore);
        location_semaphore = NULL;
    }

    // 删除任务
    vTaskDelete(NULL);
}


/**
 * @brief wifi连接成功的回调函数
 * 
 */
static void wifi_connected_handler(void)
{
    // 处理wifi连接成功的逻辑
    ESP_LOGI(TAG_HTTP, "wifi connected, start to fetch location");

    // 创建一个信号量，用于同步位置和天气任务
    location_semaphore = xSemaphoreCreateBinary();
    if (location_semaphore == NULL)
    {
        ESP_LOGE(TAG_HTTP, "Failed to create semaphore");
        return;
    }

    // 创建一个任务，用来获取位置信息
    xTaskCreate(fetch_location_task, "fetch_location_task", 4096, NULL, 8, NULL);

    // 创建一个任务，用来获取天气信息
    xTaskCreate(fetch_weather_task, "fetch_weather_task", 4096, NULL, 8, NULL);

}


void weather_manager_init(void)
{
    // 注册wifi连接成功的回调函数
    wifi_station_set_connected_cb(wifi_connected_handler);

}
