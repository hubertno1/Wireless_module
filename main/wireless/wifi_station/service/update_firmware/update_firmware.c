#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "update_firmware.h"
#include "wifi_station.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "esp_spiffs.h"


static const char* TAG_FW = "FIRMWARE_UPDATE";

#define FIRMWARE_VERSION                "0.9.9"                                                     // 当前固件版本号
#define FIRMWARE_VERSION_URL            "HTTP://www.stopwatchb.top/firmware/version.json"           // 用于获取最新固件版本号的URL

static char current_version[32] = FIRMWARE_VERSION;                                                 // 获取当前固件版本号
static char latest_version[16];                                                                     // 获取最新固件版本号
static char firmware_url[256];                                                                      // 获取固件下载地址
static char firmware_checksum[65];                                                                  // 获取固件校验和

static FILE *fw_file = NULL;

void init_filesystem() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/fw_bin",
        .partition_label = "fw_bin",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        // 处理错误
    }
}


// HTTP 事件处理函数
static esp_err_t fw_version_http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // 接收数据的缓冲区
    static int output_len;       // 已接收数据的长度

    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                // 动态分配内存，存储接收到的数据
                if (output_buffer == NULL) {
                    output_buffer = (char *) malloc(evt->data_len + 1);
                    output_len = 0;
                    if (output_buffer == NULL) {
                        ESP_LOGE(TAG_FW, "Failed to allocate memory for output buffer");
                        return ESP_FAIL;
                    }
                } else {
                    char *new_buffer = realloc(output_buffer, output_len + evt->data_len + 1);
                    if (new_buffer == NULL) {
                        ESP_LOGE(TAG_FW, "Failed to reallocate memory for output buffer");
                        free(output_buffer);
                        output_buffer = NULL;
                        return ESP_FAIL;
                    }
                    output_buffer = new_buffer;
                }
                memcpy(output_buffer + output_len, evt->data, evt->data_len);
                output_len += evt->data_len;
                output_buffer[output_len] = 0;  // 添加字符串结束符
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            if (output_buffer != NULL) {
                // 解析版本信息
                cJSON *root = cJSON_Parse(output_buffer);
                if (root == NULL) {
                    ESP_LOGE(TAG_FW, "Failed to parse JSON");
                    free(output_buffer);
                    output_buffer = NULL;
                    return ESP_FAIL;
                }

                cJSON *version_item = cJSON_GetObjectItem(root, "version");
                cJSON *url_item = cJSON_GetObjectItem(root, "url");
                cJSON *checksum_item = cJSON_GetObjectItem(root, "checksum");

                if (version_item && url_item && checksum_item) {
                    snprintf(latest_version, sizeof(latest_version), "%s", version_item->valuestring);
                    snprintf(firmware_url, sizeof(firmware_url), "%s", url_item->valuestring);
                    snprintf(firmware_checksum, sizeof(firmware_checksum), "%s", checksum_item->valuestring);

                    ESP_LOGI(TAG_FW, "Latest version: %s", latest_version);
                    ESP_LOGI(TAG_FW, "Firmware URL: %s", firmware_url);
                    ESP_LOGI(TAG_FW, "Firmware checksum: %s", firmware_checksum);
                } else {
                    ESP_LOGE(TAG_FW, "Invalid JSON format");
                    cJSON_Delete(root);
                    free(output_buffer);
                    output_buffer = NULL;
                    return ESP_FAIL;
                }

                cJSON_Delete(root);
                free(output_buffer);
                output_buffer = NULL;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// 从服务器获取最新的固件版本号
// 使用HTTP客户端从服务器获取version.json文件，解析出其中的最新固件版本号
static esp_err_t fetch_fw_version(void)
{
    // 配置HTTP客户端参数
    esp_http_client_config_t config = {
        .url = FIRMWARE_VERSION_URL,
        .method = HTTP_METHOD_GET,
        .event_handler = fw_version_http_event_handler,
    };

    // 初始化HTTP客户端
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG_FW, "Failed to init HTTP client");
        return ESP_FAIL;
    }

    // 发起HTTP请求
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG_FW, "HTTP GET status = %d", status_code);
        if (status_code == 200)
        {
            // 成功获取版本信息
            esp_http_client_cleanup(client);
            return ESP_OK;
        }
        else
        {
            ESP_LOGE(TAG_FW, "failed to get version info, HTTP status code: %d", status_code);
        }
    }
    else
    {
        ESP_LOGE(TAG_FW, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return ESP_FAIL;
}

static int compare_version(const char *version1, const char *version2)
{
    int major1, minor1, patch1;
    int major2, minor2, patch2;

    sscanf(version1, "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(version2, "%d.%d.%d", &major2, &minor2, &patch2);

    if (major1 != major2) {
        return major1 - major2;
    } else if (minor1 != minor2) {
        return minor1 - minor2;
    } else {
        return patch1 - patch2;
    }
}

// 比较最新的固件版本号和当前固件版本号，计算是否需要升级
static bool compare_fw_version(void)
{
    int cmp = compare_version(latest_version, current_version);
    if (cmp > 0) {
        ESP_LOGI(TAG_FW, "New firmware version available: %s", latest_version);
        return true;
    } else if (cmp == 0) {
        ESP_LOGI(TAG_FW, "Firmware is up to date.");
        return false;
    } else {
        ESP_LOGW(TAG_FW, "Current firmware version (%s) is newer than server version (%s)", current_version, latest_version);
        return false;
    }
}

static bool check_fw_update(void)
{
    // 检查固件是否需要升级

    // 1. 获取最新的固件版本号
    fetch_fw_version();

    // 2. 运算，比较最新的固件版本号和当前固件版本号
    bool need_update = compare_fw_version();

    // 如果需要升级，返回true；否则返回false
    return need_update;

}

static esp_err_t download_fw_http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG_FW, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG_FW, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADERS_SENT: // 确认后保留其中之一
            ESP_LOGI(TAG_FW, "HTTP_EVENT_HEADERS_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG_FW, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                if (fw_file == NULL) {
                    // 打开文件
                    fw_file = fopen("/fw_bin/firmware.bin", "wb");
                    if (fw_file == NULL) {
                        ESP_LOGE(TAG_FW, "Failed to open file for writing");
                        return ESP_FAIL;
                    }
                }
                // 写入数据
                fwrite(evt->data, 1, evt->data_len, fw_file);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            if (fw_file != NULL) {
                fclose(fw_file);
                fw_file = NULL;
            }
            ESP_LOGI(TAG_FW, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_FW, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG_FW, "HTTP_EVENT_REDIRECT");
            break;
        default:
            ESP_LOGW(TAG_FW, "Unhandled HTTP event: %d", evt->event_id);
            break;
    }
    return ESP_OK;
}



void calculate_sha256(const char *file_path, char *output_buffer) {
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG_FW, "Failed to open file for calculating SHA256");
        return;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 表示 SHA256

    uint8_t buffer[1024];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        mbedtls_sha256_update(&ctx, buffer, read_bytes);
    }

    uint8_t hash[32];
    mbedtls_sha256_finish(&ctx, hash);

    mbedtls_sha256_free(&ctx);
    fclose(file);

    // 将哈希值转换为十六进制字符串
    for (int i = 0; i < 32; i++) {
        sprintf(&output_buffer[i * 2], "%02x", hash[i]);
    }
}


esp_err_t download_fw(void)
{
    ESP_LOGI(TAG_FW, "Starting downloading firmware from %s", firmware_url);

    // 配置HTTP客户端以获取固件文件
    esp_http_client_config_t config = {
        .url = firmware_url,
        .event_handler = download_fw_http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // 发送 GET 请求
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG_FW, "HTTP GET Status = %d, Content Length = %d", status_code, content_length);
    } else {
        ESP_LOGE(TAG_FW, "Failed to perform HTTP request: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    // 计算下载的文件的校验和
    char calculated_checksum[65];
    calculate_sha256("/fw_bin/firmware.bin", calculated_checksum);

    // 打印计算得到的校验和
    ESP_LOGI(TAG_FW, "Calculated checksum: %s", calculated_checksum);

    if (strcmp(calculated_checksum, firmware_checksum) == 0) {
        ESP_LOGI(TAG_FW, "Checksum verification succeeded");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG_FW, "Checksum verification failed");
        // 校验失败时删除错误的固件文件
        remove("/fw_bin/firmware.bin");
        return ESP_FAIL;
    }
}


// 固件升级任务
static void update_fw_task(void *pvParameters)
{
    // 根据版本号检查固件是否需要升级
    bool need_update = check_fw_update();
    // 如果需要升级，升级固件
    if (need_update)
    {
        ESP_LOGI(TAG_FW, "Firmware needs to be updated");

        // 下载固件
        esp_err_t download_result = download_fw();
        if (download_result == ESP_OK)
        {
            // 保存固件文件
            ESP_LOGI(TAG_FW, "Firmware update success");
        }
        else
        {
            // 下载或校验失败
            ESP_LOGE(TAG_FW, "Firmware update failed");
        }
    }
    // 如果不需要升级，打印日志，固件是最新的
    else
    {
        ESP_LOGI(TAG_FW, "Firmware is up to date");
    }

    // 升级任务完成后，删除任务
    vTaskDelete(NULL);
}


// wifi连接成功的回调函数，用于检查固件升级
static void update_fw_handler(void)
{
    // 检查固件升级
    ESP_LOGI(TAG_FW, "checking firmware update...");

    // 创建固件升级检查任务
    xTaskCreate(update_fw_task, "update_fw_task", 8192, NULL, 10, NULL);

}

void update_fw_init(void)
{
    // 初始化文件系统
    init_filesystem();

    // 注册wifi连接成功的回调函数
    wifi_station_set_connected_cb(update_fw_handler);

}
