/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_view.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <algorithm>
#include <string>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static uint16_t battery_voltage = 0;
static uint32_t color           = 0;
static int max_interval         = 5;
static int min_interval         = 1;
static int current_list[6]      = {};
static bool power_state_list[7] = {};

float map(float x, float in_min, float in_max, float out_min, float out_max)
{
    if (x < in_min) x = in_min;
    if (x > in_max) x = in_max;
    float result = (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
    // printf("result:%f, x:%f, in_min:%f, in_max:%f, out_min:%f, out_max:%f\n", result, x, in_min, in_max, out_min,
    //        out_max);
    return result;
}

void updateBatteryState(void* pvParameter)
{
    static bool ledState = false, charge_status = false, isPowerSupply = false;
    static unsigned long lastToggle    = 0;
    const unsigned long BLINK_INTERVAL = 100;
    const uint16_t LOW_VOLTAGE         = 6000;
    const uint16_t MID_VOLTAGE         = 7000;
    static uint32_t color = 0, update_counter = 0;

    while (true) {
        if (update_counter % 5 == 0) {
            isPowerSupply = GetHAL()->getPowerSupplyStatus();
            charge_status = GetHAL()->getPowerState(hal::HalBase::PC_Charge);
        }

        // 未接入电池状态
        if (battery_voltage < LOW_VOLTAGE) {
            GetHAL()->setBatteryLedColor(0xFFFFFF);
            vTaskDelay(250);
        } else {
            // 中电压闪烁
            if (LOW_VOLTAGE < battery_voltage && battery_voltage < MID_VOLTAGE) {
                unsigned long now = GetHAL()->millis();
                if (now - lastToggle >= BLINK_INTERVAL) {
                    ledState   = !ledState;
                    lastToggle = now;
                    color      = (isPowerSupply && charge_status) ? 0x00FF00 : 0xFF0000;  // 充电绿色, 放电红色
                    GetHAL()->setBatteryLedColor(ledState ? color : 0x000000);
                    // mclog::tagDebug("Debug", "[AppView] Middle");
                }
                vTaskDelay(250);
            }
            // 高电压常亮
            else {
                if (!charge_status && isPowerSupply) {  // 接入外部电源且未开启充电时绿色呼吸灯
                    color = 0x00FF00;
                    GetHAL()->setBatteryLedBreath(1, 15);
                    vTaskDelay(10);
                } else {
                    color = (isPowerSupply && charge_status) ? 0x00FF00 : 0xFF0000;  // 充电绿色, 放电红色
                    vTaskDelay(250);
                }
                GetHAL()->setBatteryLedColor(color);
            }
        }
        update_counter++;
    }

    vTaskDelete(NULL);
}

void updateLoadLedColor()
{
    uint32_t led_colors[5] = {0};
    uint32_t usb_a_color   = 0;

    led_colors[hal::HalBase::LC_USB_C] = power_state_list[hal::HalBase::PC_USB] ? 0xFF0000 : 0x00FF00;
    led_colors[hal::HalBase::LC_USB_A] = power_state_list[hal::HalBase::PC_USB] ? 0xFF0000 : 0x00FF00;
    led_colors[hal::HalBase::LC_UART]  = power_state_list[hal::HalBase::PC_UART] ? 0xFF0000 : 0x00FF00;
    led_colors[hal::HalBase::LC_BUS]   = power_state_list[hal::HalBase::PC_BUS] ? 0xFF0000 : 0x00FF00;
    led_colors[hal::HalBase::LC_I2C]   = power_state_list[hal::HalBase::PC_I2C] ? 0xFF0000 : 0x00FF00;

    GetHAL()->setLedColor(led_colors, 5);
}

void updateLoadState()
{
    for (int i = 0; i <= 6; i++) {
        if (i <= 5) {
            current_list[i] = GetHAL()->getDeviceCurrent((hal::HalBase::VAMonitor)i);
        }
        power_state_list[i] = GetHAL()->getPowerState(static_cast<hal::HalBase::PowerControl>(i));
    }
}

void updateLoadLedBrightness()
{
    // 灯光亮度与负载状态成正比, 将负载状态映射至灯光亮度
    for (int i = 0; i <= 5; i++) {
        // mclog::info("current_list[{}]: {}", i, current_list[i]);
        float brightness = map(current_list[i], 0, 1000, 10, 100);

        if (i == hal::HalBase::VAMonitor::VM_USB) {
            GetHAL()->setLedBrightness(hal::HalBase::LC_USB_A, brightness);
            GetHAL()->setLedBrightness(hal::HalBase::LC_USB_C, brightness);
        } else if (i == hal::HalBase::VAMonitor::VM_CAN_BUS) {
            int max_current      = std::max(current_list[i], current_list[hal::HalBase::VAMonitor::VM_RS485_BUS]);
            float max_brightness = map(max_current, 0, 1000, 10, 100);
            GetHAL()->setLedBrightness(hal::HalBase::loadLedMapping[i], max_brightness);
        } else if (i != hal::HalBase::VAMonitor::VM_RS485_BUS) {  // 跳过RS485，因为已经在CAN总线时处理过了
            GetHAL()->setLedBrightness(hal::HalBase::loadLedMapping[i], brightness);
        }
    }
}

void ledSelectShowTask(void* arg)
{
    mclog::info("[AppSetting] led select show task");
    while (true) {
        updateLoadState();
        updateLoadLedColor();
        GetHAL()->updateLedColors();
        GetHAL()->delay(80);

        if (GetHAL()->current_location == 1) {
            GetHAL()->setUSBLedColor(0x000000);
        } else if (GetHAL()->current_location <= 3) {
            GetHAL()->setLedColor((hal::HalBase::LedControl)GetHAL()->current_location, 0x000000);
        } else if (GetHAL()->current_location == 4) {
            GetHAL()->current_location = 5;
        } else if (GetHAL()->current_location == 5) {
            GetHAL()->setLedColor(hal::HalBase::LC_I2C, 0x000000);
        } else if (GetHAL()->current_location == 6) {
            for (int i = 0; i <= 5; i++) {  // 跳过PWOER_L,R
                if (i == 5) continue;
                // GetHAL()->setLedColor((hal::HalBase::LedControl)i, 0x000000);
                GetHAL()->setLedColor(hal::HalBase::LC_USB_C, hal::HalBase::LC_I2C, 0x000000);
            }
        } else if (GetHAL()->current_location > 6) {
            GetHAL()->current_location = 1;
        }
        GetHAL()->updateLedColors();
        GetHAL()->delay(80);
    }
    vTaskDelete(NULL);
}

using namespace mooncake;

TaskHandle_t _battery_state_task_handle = NULL;

AppView::AppView()
{
    // 配置 App 信息
    setAppInfo().name = "AppView";
}

void AppView::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    // 打开自己
    // open();
}

void AppView::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    GetHAL()->setBatteryLedBrightness(100);
    if (_battery_state_task_handle == NULL) {
        xTaskCreate(updateBatteryState, "updateBatteryState", 4096, NULL, 5, &_battery_state_task_handle);
    } else {
        mclog::warn("ledSelectShowTask already running!");
    }
    if (_battery_state_task_handle == NULL) {
        mclog::tagError(getAppInfo().name, "Failed to create battery state task");
    }
}

void AppView::onRunning()
{
    battery_voltage = GetHAL()->getDeviceVoltage(hal::HalBase::VAMonitor::VM_BAT);
    updateLoadState();
    updateLoadLedColor();
    updateLoadLedBrightness();
    GetHAL()->updateLedColors();
}

void AppView::onClose()
{
    mclog::tagInfo(getAppInfo().name, "stop battery state task");
    if (_battery_state_task_handle != NULL) {
        vTaskDelete(_battery_state_task_handle);
        _battery_state_task_handle = NULL;
    } else {
        mclog::tagError(getAppInfo().name, "battery state task not found");
    }
}
