/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_ezdata.h"
#include <cstdint>
#include <mutex>
#include "../../hal/hal.h"
#include "../../assets/assets.h"
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace mooncake;

static uint32_t _last_flash_time = 0;
static bool ap_closed            = true;

void flash_led_state(void* arg)
{
    auto hal          = GetHAL();
    size_t stack_left = uxTaskGetStackHighWaterMark(NULL);
    mclog::info("flash task stack left: {} bytes", stack_left);
    hal->setLedBrightness(hal::HalBase::LC_BAT_CHARGE, 70);
    while (1) {
        bool& color_state = hal->_color_led_state;
        uint32_t now      = hal->millis();
        if (!GetHAL()->getWifiStatus()) {
            // WiFi未连接，蓝灯闪烁, 未配网快闪, 配网慢闪
            uint16_t interval = 0;
            if (!hal->isConfigWifi())
                interval = 160;
            else
                interval = 320;
            if (now - _last_flash_time > interval) {
                // mclog::debug("color_state: {}", color_state);
                hal->setLedColor(hal::HalBase::LC_BAT_CHARGE, color_state
                                                                  ? (hal->_connect_timeout ? 0xFF0000 : 0x0000FF)
                                                                  : (hal->_connect_timeout ? 0x0000FF : 0x000000));
                color_state      = !color_state;
                _last_flash_time = now;
            }
        } else {
            // WiFi已连但EzData未连，常亮蓝灯
            if (!GetHAL()->_ezdata_connected) {
                if (now - _last_flash_time > 320) {
                    hal->setLedColor(hal::HalBase::LC_BAT_CHARGE, color_state ? 0xFF0000 : 0x8A2BE2);
                    color_state      = !color_state;
                    _last_flash_time = now;
                }
            } else {
                hal->setLedColor(hal::HalBase::LC_BAT_CHARGE, color_state ? 0x000000 : 0x8A2BE2);
                if (color_state) color_state = !color_state;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

AppEzdata::AppEzdata()
{
    // 配置 App 信息
    setAppInfo().name = "AppEzdata";
}

void AppEzdata::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // 打开自己
    open();
}

void AppEzdata::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // // Start server
    xTaskCreate(flash_led_state, "flash_led", 4096, NULL, 15, NULL);
    if (GetHAL()->getConfig().start_poster == "yes") {
        GetHAL()->start_poster_server();
    } else {
        GetHAL()->start_ap_server();
        GetHAL()->start_blufi_server();
        ap_closed = false;
    }
}

void AppEzdata::onRunning()
{
    if (!ap_closed) {
        uint32_t& last_token_request_time = GetHAL()->_last_token_request_time;
        if (GetHAL()->millis() - last_token_request_time > 10000 && GetHAL()->getWifiStatus()) {
            if (GetHAL()->getStationNum() > 0) {
                mclog::info("AP has connected stations, keep it open.");
                last_token_request_time = GetHAL()->millis();
                return;
            }
            mclog::info("AP closed due to inactivity.");
            ap_closed = true;
            GetHAL()->stop_ap_server();
            GetHAL()->stop_blufi_server();
        }
    }
    static bool ws_inited = false;
    if (GetHAL()->getWifiStatus() && !ws_inited) {
        GetHAL()->ws_init();
        ws_inited = true;
    }
}

void AppEzdata::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    // GetHAL()->ezdata_running = false;
}
