#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "pb_encode.h"
#include "pb_decode.h"
#include "message.pb.h"
#include "uart_manager.h"
#include "sync_time.h"
#include "crc32.h"          
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define UART_BAUD_RATE                          115200
#define UART_DATA_BITS                          UART_DATA_8_BITS
#define UART_PARITY                             UART_PARITY_DISABLE
#define UART_STOP_BITS                          UART_STOP_BITS_1
#define UART_FLOW_CTRL                          UART_HW_FLOWCTRL_DISABLE
#define UART_SOURCE_CLK                         UART_SCLK_DEFAULT

#define UART_PORT_NUM                           UART_NUM_1
#define UART_BUFFER_SIZE                        1024

#define UART_TX_PIN                             GPIO_NUM_0
#define UART_RX_PIN                             GPIO_NUM_1
#define UART_RTS_PIN                            UART_PIN_NO_CHANGE
#define UART_CTS_PIN                            UART_PIN_NO_CHANGE

#define UART_RX_TIMEOUT_MS                      20  // UART接收超时时间 (ms)

static const char *TAG = "UART_Manager";

// 定义帧格式中的常量
#define FRAME_HEADER                            0xAA
#define OPCODE_SYNC_TIME                        0x10

// 定义一个互斥锁和最新的时间信息结构体
static SemaphoreHandle_t time_info_mutex;
static time_info_t latest_time_info;

// 时间同步完成的回调函数
static void uart_time_sync_cb(const time_info_t *time_info)
{
    if (xSemaphoreTake(time_info_mutex, portMAX_DELAY) == pdTRUE)
    {
        // 更新最新的时间信息
        latest_time_info = *time_info;
        xSemaphoreGive(time_info_mutex);
        ESP_LOGI(TAG, "Time information updated from sync_time module");
    }
}

// 发送数据到UART
static int uart_send_data(uint8_t *data_buffer, int data_len)
{
    return uart_write_bytes(UART_PORT_NUM, (const char *) data_buffer, data_len);
}

// UART管理任务，处理UART数据的接收和发送
static void uart_manager_task(void *pvParameters)
{
    // 防止编译器警告未使用参数
    (void) pvParameters;

    // 分配用于发送的缓冲区
    uint8_t *uart_send_buffer = (uint8_t *) malloc(UART_BUFFER_SIZE);
    if (uart_send_buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for UART buffer");
        vTaskDelete(NULL);
        return;
    }
    memset(uart_send_buffer, 0, UART_BUFFER_SIZE);

    while (1)
    {
        // 等待最新的时间信息
        if (xSemaphoreTake(time_info_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            // 检查时间信息是否已更新
            if (latest_time_info.year == 0)
            {
                ESP_LOGW(TAG, "Time information not yet available");
                xSemaphoreGive(time_info_mutex);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            // **1. 编码 DATA 部分（Protobuf 编码）**
            uint8_t data_buffer[UART_BUFFER_SIZE];
            size_t data_length = 0;
            {
                TimeSync message = TimeSync_init_zero;

                // 设置时间字段为最新的时间信息
                message.year = latest_time_info.year;
                message.month = latest_time_info.month;
                message.day = latest_time_info.day;
                message.hour = latest_time_info.hour;
                message.minute = latest_time_info.minute;
                message.second = latest_time_info.second;

                // 创建输出流
                pb_ostream_t stream = pb_ostream_from_buffer(data_buffer, sizeof(data_buffer));

                // 编码消息
                if (!pb_encode(&stream, TimeSync_fields, &message))
                {
                    ESP_LOGE(TAG, "Encoding failed: %s", PB_GET_ERROR(&stream));
                    xSemaphoreGive(time_info_mutex);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    continue;
                }

                data_length = stream.bytes_written;
                ESP_LOGI(TAG, "Encoded DATA length: %d", (int)data_length);
            }

            // **2. 构建帧**
            size_t frame_length = 0;
            {
                uint8_t *ptr = uart_send_buffer;

                // HEADER (1 byte)
                *ptr++ = FRAME_HEADER;

                // OPCODE (1 byte)
                *ptr++ = OPCODE_SYNC_TIME;

                // LENGTH (2 bytes, big-endian)
                *ptr++ = (data_length >> 8) & 0xFF;
                *ptr++ = data_length & 0xFF;

                // DATA (n bytes)
                memcpy(ptr, data_buffer, data_length);
                ptr += data_length;

                // 计算 CRC32 校验和（从 HEADER 到 DATA 的所有内容）
                int content_length = ptr - uart_send_buffer;
                uint32_t crc = crc32((const char *)uart_send_buffer, content_length);

                // CRC32 (4 bytes, big-endian)
                *ptr++ = (crc >> 24) & 0xFF;
                *ptr++ = (crc >> 16) & 0xFF;
                *ptr++ = (crc >> 8) & 0xFF;
                *ptr++ = crc & 0xFF;

                frame_length = ptr - uart_send_buffer;
                ESP_LOGI(TAG, "Constructed frame length: %d", (int)frame_length);
            }

            // **3. 发送帧到 UART**
            {
                int bytes_sent = uart_send_data(uart_send_buffer, frame_length);
                if (bytes_sent < 0)
                {
                    ESP_LOGE(TAG, "Failed to send data to UART");
                }
                else
                {
                    ESP_LOGI(TAG, "Sent frame to UART, bytes: %d", bytes_sent);
                }
            }

            xSemaphoreGive(time_info_mutex);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to take time_info_mutex");
        }

        // 等待一段时间
        vTaskDelay(pdMS_TO_TICKS(20000));
    }

    // 释放内存
    free(uart_send_buffer);
    vTaskDelete(NULL);
}

// 初始化UART模块
void uart_manager_init(void)
{
    // 创建互斥锁
    time_info_mutex = xSemaphoreCreateMutex();
    if (time_info_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create time_info_mutex");
        return;
    }

    // 初始化最新时间信息
    memset(&latest_time_info, 0, sizeof(time_info_t));

    // 注册时间同步完成的回调函数
    set_time_synced_cb(uart_time_sync_cb);

    // 配置UART参数
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_BITS,
        .parity    = UART_PARITY,
        .stop_bits = UART_STOP_BITS,
        .flow_ctrl = UART_FLOW_CTRL,
        .source_clk = UART_SOURCE_CLK,
    };

    // 中断分配标志（默认为0）
    int intr_alloc_flags = 0;

    // 安装UART驱动
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUFFER_SIZE * 2, 0, 0, NULL, intr_alloc_flags));

    // 配置UART端口
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));

    // 设置UART引脚
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // 创建UART任务
    xTaskCreate(uart_manager_task, "uart_manager_task", 4096, NULL, 10, NULL);
}
