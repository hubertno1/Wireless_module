#ifndef __WIFI_STATION_H__
#define __WIFI_STATION_H__

#define MAX_WIFI_CONNECTED_CALLBACKS 5          // 定义最大回调函数数量

// 定义WiFi连接状态的回调函数类型
typedef void (*wifi_station_connected_cb_t)(void);
typedef void (*wifi_station_disconnected_cb_t)(void);


// 初始化 WiFi模块
void wifi_station_init(void);

// 暴露给外部的接口，用于设置WiFi连接成功的回调函数
void wifi_station_set_connected_cb(wifi_station_connected_cb_t cb);

// 暴露给外部的接口，用于设置WiFi断开连接的回调函数
void wifi_station_set_disconnected_cb(wifi_station_disconnected_cb_t cb);

#endif /* __WIFI_STATION_H__ */
