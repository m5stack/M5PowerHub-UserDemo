/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <mooncake.h>
#include <mooncake_log.h>
#include <Arduino.h>
#include "hal/hal_esp32.h"

#include <FS.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "../utils/system/api_system.h"
#include "../utils/poster/api_poster.h"
#include "../utils/blufi/blufi.h"

static const std::string _tag  = "poster";
static AsyncWebServer* _server = nullptr;
void HalEsp32::start_poster_server(bool internal_call)
{
    mclog::tagInfo(_tag, "start poster server");
    auto config = GetHAL()->getConfig();

    // Try connect wifi
    if (!internal_call) {
        WiFi.mode(WIFI_STA);
        mclog::tagInfo(_tag, "set wifi mode to WIFI_STA");
    }

    mclog::tagInfo(_tag, "try connect wifi: \nssid:{}\npass:{}", config.wifi_ssid.c_str(),
                   config.wifi_password.c_str());

    // 创建单独的任务处理WiFi连接和WebSocket初始化
    xTaskCreate(
        [](void* param) {
            auto* hal   = (HalEsp32*)param;
            auto config = hal->getConfig();

            // WiFi连接
            WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());

            bool is_connected      = false;
            uint8_t log_count      = 0;
            uint32_t last_try_time = hal->millis();
            while (1) {
                for (int i = 255; i > 0; i--) {
                    // Connected
                    if (WiFi.status() == WL_CONNECTED) {
                        mclog::tagInfo(_tag, "wifi connected");
                        is_connected = true;
                        break;
                    }

                    // Time out
                    if (hal->millis() - last_try_time > 5000UL) {
                        log_count++;
                        hal->delay(5000);
                        if (log_count > 5) {
                            // mclog::tagWarn(_tag, "[wifi] still trying to connect wifi..");
                            // mclog::tagInfo(_tag, "[wifi] ssid: {}", hal->getConfig().wifi_ssid.c_str());
                            // mclog::tagInfo(_tag, "[wifi] pass: {}", hal->getConfig().wifi_password.c_str());
                            // // mclog:error("connect failed, return back to ap mode");
                            // config.start_poster = "no";
                            // hal->setConfig(config);

                            mclog::error("wifi connect failed!");
                            hal->_connect_timeout = true;
                            // hal->delay(300);
                            // esp_restart();
                        } else {
                            mclog::error("wifi connect failed, retry {} times", log_count);
                        }
                    }
                }

                if (is_connected) break;
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
            mclog::tagInfo(_tag, "wifi connect ok, ip: {}", WiFi.localIP().toString().c_str());
            // hal->setLedColor(hal::HalBase::LC_BAT_CHARGE, 0x0000FF);

            // // WebSocket初始化
            // hal->ws_init();

            vTaskDelete(NULL);
        },
        "poster_task", 8192, this, 5, NULL);
}

void HalEsp32::start_ap_server()
{
    delay(200);
    mclog::tagInfo(_tag, "start ap server");

    // Create web server
    _server = new AsyncWebServer(80);

    // Start open ap
    uint64_t chipid = ESP.getEfuseMac();
    char chipid_str[5];
    snprintf(chipid_str, sizeof(chipid_str), "%04X", (uint16_t)((chipid >> 32) & 0xFFFF));
    String ap_ssid = "PowerHub-" + String(chipid_str);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, emptyString, 1, 0, 1, false);
    IPAddress localIP = WiFi.softAPIP();
    mclog::tagInfo(_tag, "AP name: {}, AP IP address: {}", ap_ssid.c_str(), localIP.toString().c_str());

    vTaskDelay(100 / portTICK_PERIOD_MS);  // Add a small delay

    // Load apis
    load_system_apis(*_server);
    load_poster_apis(*_server);

    // Set file system support
    _server->serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    _server->onNotFound([](AsyncWebServerRequest* request) { request->send(LittleFS, "/404.html"); });

    // Start server
    _server->begin();
}

void HalEsp32::stop_ap_server()
{
    mclog::info("stop ap server");

    delete _server;
    delay(200);

    WiFi.softAPdisconnect();
    WiFi.mode(WIFI_STA);  // Switch to STA mode
    // WiFi.disconnect();
}

void HalEsp32::start_blufi_server(void)
{
    esp_err_t ret;

    // // Initialize NVS
    // ret = nvs_flash_init();
    // if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    //     ESP_ERROR_CHECK(nvs_flash_erase());
    //     ret = nvs_flash_init();
    // }
    // ESP_ERROR_CHECK(ret);

    // initialise_wifi();

#if CONFIG_BT_CONTROLLER_ENABLED || !CONFIG_BT_NIMBLE_ENABLED
    ret = esp_blufi_controller_init();
    if (ret) {
        BLUFI_ERROR("%s BLUFI controller init failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
#endif

    ret = esp_blufi_host_and_cb_init(&example_callbacks);
    if (ret) {
        BLUFI_ERROR("%s initialise failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    BLUFI_INFO("BLUFI VERSION %04x", esp_blufi_get_version());
}

void HalEsp32::stop_blufi_server(void)
{
    esp_err_t ret = ESP_OK;
    ret           = esp_bt_controller_disable();
    if (ret) {
        BLUFI_ERROR("%s disable bt controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_deinit();
    if (ret) {
        BLUFI_ERROR("%s deinit bt controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }
}