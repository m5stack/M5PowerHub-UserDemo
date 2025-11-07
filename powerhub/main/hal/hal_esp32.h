/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
#include <cstdint>
#include "powerhub.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class HalEsp32 : public hal::HalBase {
public:
    std::string type() override
    {
        return "PowerHub";
    }

    void init() override;

    PowerHub powerhub;

    void delay(uint32_t ms) override;
    uint32_t millis() override;
    void restart() override;
    void powerOff() override;

    void updateButtons() override;

    void setAlarmTime(struct alarm_time time) override;
    void setRtcTime(tm time) override;
    void getRtcTime(tm* time) override;

    void setPowerState(PowerControl device, uint16_t state) override;
    bool getPowerState(PowerControl device) override;
    uint16_t getDeviceVoltage(VAMonitor device) override;
    int16_t getDeviceCurrent(VAMonitor device) override;
    uint8_t getPowerSupplyStatus() override;
    void setBusConfig(uint16_t voltage, uint8_t currentLimit, uint8_t enable, uint8_t direction);

    void setLedState(bool state) override;
    bool getLedState() override;
    void setBatteryLedColor(uint32_t color) override;
    void setBatteryLedBrightness(uint8_t brightness) override;
    uint32_t getLedColor(LedControl led) override;
    void setLedColor(LedControl led, uint32_t color) override;
    void setLedColor(uint8_t start_pos, uint8_t end_pos, uint32_t color) override;
    void setLedColor(const uint32_t* colors, size_t count) override;
    void setAllLedColor(uint32_t color) override;
    void setUSBLedColor(uint32_t color) override;
    void updateLedColors() override;
    uint8_t getLedBrightness(LedControl led) override;
    void setLedBrightness(LedControl led, uint8_t brightness) override;
    void setBatteryLedBreath(uint8_t interval = 1, uint8_t interval_ms = 10) override;

    void setConfig(Config_t& cfg);
    Config_t getConfig();
    Config_t getDefaultConfig();
    void config_init();
    void printConfig();

    void start_ap_server();
    void stop_ap_server();
    void start_poster_server(bool internal_call = false);
    void start_blufi_server();
    void stop_blufi_server(void);

    bool getWifiStatus() override;
    uint8_t getStationNum() override;
    bool isConfigWifi() override;
    void ws_init();

private:
    void update_system_time();

    uint32_t _last_token_request_time = 0;

    std::string _device_token = "";

    uint32_t colors_buffer[8] = {0};
    bool _color_led_state     = false;

    // 呼吸灯状态
    uint8_t _breath_brightness = 0;
    int8_t _breath_direction   = 1;
};
