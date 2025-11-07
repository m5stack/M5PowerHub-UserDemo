/**
 * @file blufi.h
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-11-03
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef BLUFI_H
#define BLUFI_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#if CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
#include "esp_bt.h"
#endif

#include "esp_blufi_api.h"
#include "esp_blufi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 宏定义 */
#define BLUFI_TAG                     "BLUFI"
#define WIFI_CONNECTION_MAXIMUM_RETRY 3
#define EXAMPLE_INVALID_REASON        255
#define EXAMPLE_INVALID_RSSI          -128
#define WIFI_LIST_NUM                 10
#define BLUFI_INFO(fmt, ...)          ESP_LOGI(BLUFI_TAG, fmt, ##__VA_ARGS__)
#define BLUFI_ERROR(fmt, ...)         ESP_LOGE(BLUFI_TAG, fmt, ##__VA_ARGS__)

/* WiFi认证模式配置 */
// #define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
// #define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
// #define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
// #define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
// #define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
// #define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
// #define WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK

/* 事件位定义 */
extern const int CONNECTED_BIT;

/* 全局变量声明 */
extern EventGroupHandle_t wifi_event_group;

/* 函数声明 */

/**
 * @brief 初始化WiFi
 */
void initialise_wifi(void);

/**
 * @brief WiFi连接函数
 */
void example_wifi_connect(void);

/**
 * @brief WiFi重连函数
 * @return true 重连中, false 重连失败
 */
bool example_wifi_reconnect(void);

/**
 * @brief 获取SoftAP当前连接数
 * @return 连接数
 */
int softap_get_current_connection_number(void);

/**
 * @brief 记录WiFi连接信息
 * @param rssi 信号强度
 * @param reason 断开原因
 */
void example_record_wifi_conn_info(int rssi, uint8_t reason);

/**
 * @brief IP事件处理函数
 * @param arg 参数
 * @param event_base 事件基础
 * @param event_id 事件ID
 * @param event_data 事件数据
 */
void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief WiFi事件处理函数
 * @param arg 参数
 * @param event_base 事件基础
 * @param event_id 事件ID
 * @param event_data 事件数据
 */
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

/**
 * @brief BluFi事件回调函数
 * @param event BluFi事件类型
 * @param param BluFi事件参数
 */
void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

void blufi_dh_negotiate_data_handler(uint8_t *data, int len, uint8_t **output_data, int *output_len, bool *need_free);
int blufi_aes_encrypt(uint8_t iv8, uint8_t *crypt_data, int crypt_len);
int blufi_aes_decrypt(uint8_t iv8, uint8_t *crypt_data, int crypt_len);
uint16_t blufi_crc_checksum(uint8_t iv8, uint8_t *data, int len);

int blufi_security_init(void);
void blufi_security_deinit(void);
int esp_blufi_gap_register_callback(void);
esp_err_t esp_blufi_host_init(void);
esp_err_t esp_blufi_host_and_cb_init(esp_blufi_callbacks_t *callbacks);
esp_err_t esp_blufi_host_deinit(void);
esp_err_t esp_blufi_controller_init(void);
esp_err_t esp_blufi_controller_deinit(void);

/* BluFi回调函数结构体 */
extern esp_blufi_callbacks_t example_callbacks;
extern uint64_t _chipmacid;
extern char chipid_str[5];
#ifdef __cplusplus
}
#endif

#endif /* BLUFI_H */