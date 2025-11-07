/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_setting.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <sys/types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "../app_view/app_view.h"

using namespace mooncake;
uint32_t start_time;
TaskHandle_t _led_select_task_handle = NULL;

void startLedSelect()
{
    mclog::info("[AppSetting] start led select");
    if (_led_select_task_handle == NULL) {
        xTaskCreate(ledSelectShowTask, "ledSelectShowTask", 4096, NULL, 10, &_led_select_task_handle);
    } else {
        mclog::warn("[AppSetting] ledSelectShowTask already running!");
    }
    if (_led_select_task_handle == NULL) {
        mclog::error("[AppSetting] Failed to create led select task");
    }
}

void stopLedSelect()
{
    mclog::info("[AppSetting] stop led select");
    if (_led_select_task_handle != NULL) {
        vTaskDelete(_led_select_task_handle);
        _led_select_task_handle = NULL;
    } else {
        mclog::error("[AppSetting] led select task not found");
    }
}

AppSetting::AppSetting()
{
    setAppInfo().name = "AppSetting";
}

void AppSetting::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    // open();
}

void AppSetting::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    GetHAL()->stop_setting_flag = false;
    startLedSelect();
    start_time                = GetHAL()->millis();
    GetHAL()->setting_running = true;
}

void AppSetting::onRunning()
{
    if (GetHAL()->btnSelect.wasClicked()) {
        GetHAL()->current_location++;
        start_time = GetHAL()->millis();
    }
    if (GetHAL()->btnOK.wasClicked()) {
        start_time = GetHAL()->millis();
        if (GetHAL()->current_location != 6) {
            hal::HalBase::PowerControl led = hal::HalBase::ledPowerMapping[GetHAL()->current_location];
            bool state                     = GetHAL()->getPowerState(led);
            GetHAL()->setPowerState(led, !state);
        } else {
            bool anyOn = false;

            for (int i = 0; i < 7; i++) {
                auto device = static_cast<hal::HalBase::PowerControl>(i);
                if (device == hal::HalBase::PC_LED || device == hal::HalBase::PC_VAMeter ||
                    device == hal::HalBase::PC_Charge) {
                    continue;
                }
                if (GetHAL()->getPowerState(device)) {
                    anyOn = true;
                    break;
                }
            }

            bool newState = !anyOn;
            mclog::info("Setting all power devices to: {}", newState ? "ON" : "OFF");

            for (int i = 0; i < 7; i++) {
                auto device = static_cast<hal::HalBase::PowerControl>(i);
                if (device == hal::HalBase::PC_LED || device == hal::HalBase::PC_VAMeter ||
                    device == hal::HalBase::PC_Charge) {
                    continue;
                }
                GetHAL()->setPowerState(device, newState);
            }
            for (int i = 0; i < 7; i++) {
                auto device = static_cast<hal::HalBase::PowerControl>(i);
                if (device == hal::HalBase::PC_LED || device == hal::HalBase::PC_VAMeter) {
                    continue;
                }
            }
        }
    }
    if (GetHAL()->millis() - start_time > 5000) {
        GetHAL()->stop_setting_flag = true;
        stopLedSelect();
        close();
    }
}

void AppSetting::onClose()
{
    GetHAL()->setting_running = false;
    mclog::tagInfo(getAppInfo().name, "on close");
}
