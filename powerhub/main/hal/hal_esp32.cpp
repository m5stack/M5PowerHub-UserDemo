/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal/hal_esp32.h"
#include <driver/i2c.h>
#include <i2c_bus.h>
#include <mooncake_log.h>
#include <esp_timer.h>
#include <cstdint>
#include "apps/utils/math/math.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <WiFi.h>

#define SYS_I2C_SCL (gpio_num_t)48
#define SYS_I2C_SDA (gpio_num_t)45

static const std::string _tag   = "hal";
static i2c_bus_handle_t i2c_bus = NULL;
static esp_err_t i2c_scan(i2c_port_t i2c_port);
void HalEsp32::init()
{
    vTaskDelay(500 / portTICK_PERIOD_MS);  // Wait for system to stabilize
    mclog::tagInfo(_tag, "esp32 init");

    mclog::tagInfo(_tag, "i2c init");
    i2c_config_t conf;
    conf.mode             = I2C_MODE_MASTER;
    conf.sda_io_num       = SYS_I2C_SDA;
    conf.scl_io_num       = SYS_I2C_SCL;
    conf.sda_pullup_en    = GPIO_PULLUP_DISABLE;
    conf.scl_pullup_en    = GPIO_PULLUP_DISABLE;
    conf.master.clk_speed = 400000;
    conf.clk_flags        = 0;
    i2c_bus               = i2c_bus_create(I2C_NUM_0, &conf);
    mclog::tagInfo(_tag, "i2c scan");
    i2c_scan(I2C_NUM_0);

    powerhub.begin(i2c_bus, 0x50, SYS_I2C_SDA, SYS_I2C_SCL, 400000);
    mclog::tagInfo(_tag, "Fireware Version: 0x{:02X}", powerhub.getFirmwareVersion());
    update_system_time();
    // Config
    GetHAL()->config_init();
    auto cfg = GetHAL()->getConfig();
    GetHAL()->setPowerState(PC_LED, true);
    GetHAL()->setPowerState(PC_USB, cfg.usb);
    GetHAL()->setPowerState(PC_I2C, cfg.i2c);
    GetHAL()->setPowerState(PC_UART, cfg.uart);
    GetHAL()->setPowerState(PC_BUS, cfg.rs485can);
    GetHAL()->setPowerState(PC_VAMeter, true);
    GetHAL()->setPowerState(PC_Charge, true);
    if (cfg.rs485can) {
        BusConfig config = powerhub.getBusConfig();
        mclog::tagInfo(_tag, "BusConfig: {}, {}, {}, {}", config.voltage, config.currentLimit, config.enable,
                       config.direction);
        config.voltage      = 12000;
        config.currentLimit = 1000;
        config.enable       = 1;
        config.direction    = 1;
        powerhub.setBusConfig(config);
        config = powerhub.getBusConfig();
        mclog::tagInfo(_tag, "BusConfig: {}, {}, {}, {}", config.voltage, config.currentLimit, config.enable,
                       config.direction);

        SC8721Config sc_config = powerhub.getSC8721Config();
        mclog::tagInfo(_tag, "SC8721Config: {}, 0x{:02X}, 0x{:02X}, 0x{:02X}, 0x{:02X}, 0x{:02X}, 0x{:02X}",
                       sc_config.csoValue, sc_config.slopeCompensation, sc_config.outputVoltageSet, sc_config.control,
                       sc_config.systemSetting, sc_config.frequency, sc_config.statusFlags);
    }

    for (int i = 0; i < 7; i++) {
        mclog::tagInfo(_tag, "PowerControl[{}]: {}", i, GetHAL()->getPowerState(PowerControl(i)));
    }
    powerhub.setWakeUpSource(WS_RTC_ALARM, true);
    powerhub.setWakeUpSource(WS_VIN, true);
    powerhub.setWakeUpSource(WS_BUTTON, true);
}

/* -------------------------------------------------------------------------- */
/*                                   System                                   */
/* -------------------------------------------------------------------------- */
#include <driver/temperature_sensor.h>
static temperature_sensor_handle_t _temp_sensor = nullptr;

void HalEsp32::delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t HalEsp32::millis()
{
    return esp_timer_get_time() / 1000;
}

void HalEsp32::restart()
{
    mclog::tagInfo(_tag, "system restart");
    esp_restart();
}

void HalEsp32::powerOff()
{
    mclog::tagInfo(_tag, "system power off");
    powerhub.powerOff();
}

/* -------------------------------------------------------------------------- */
/*                                   Button                                   */
/* -------------------------------------------------------------------------- */
void HalEsp32::updateButtons()
{
    btnOK.setRawState(millis(), !powerhub.getButtonState(BTN_OK));
    btnSelect.setRawState(millis(), !powerhub.getButtonState(BTN_SELECT));
    btnC.setRawState(millis(), !powerhub.getButtonState(BTN_KEY2));
}

/* -------------------------------------------------------------------------- */
/*                                    RTC                                     */
/* -------------------------------------------------------------------------- */
void HalEsp32::getRtcTime(tm* time)
{
    rtc_time r_time = powerhub.getRTCTime();

    time->tm_sec  = r_time.rtc_sec;
    time->tm_min  = r_time.rtc_min;
    time->tm_hour = r_time.rtc_hour;
    time->tm_mday = r_time.rtc_day;
    time->tm_mon  = r_time.rtc_mon - 1;
    time->tm_year = r_time.rtc_year;  // tm_year is years since 1900
    time->tm_wday = r_time.rtc_wday;
}

void HalEsp32::setRtcTime(tm time)
{
    rtc_time r_time = {.rtc_sec  = time.tm_sec,
                       .rtc_min  = time.tm_min,
                       .rtc_hour = time.tm_hour,
                       .rtc_day  = time.tm_mday,
                       .rtc_mon  = time.tm_mon + 1,
                       .rtc_year = time.tm_year - 100,  // tm_year is years since 1900, rtc_year range is 0-99
                       .rtc_wday = (time.tm_wday == 0) ? 7 : time.tm_wday};  // 0=>7
    powerhub.setRTCTime(r_time);
    delay(50);
}

void HalEsp32::update_system_time()
{
    rtc_time time = powerhub.getRTCTime();

    tm timeinfo = {.tm_sec  = time.rtc_sec,
                   .tm_min  = time.rtc_min,
                   .tm_hour = time.rtc_hour,
                   .tm_mday = time.rtc_day,
                   .tm_mon  = time.rtc_mon - 1,     // tm_mon is 0-11
                   .tm_year = time.rtc_year + 100,  // tm_year is years since 1900, rtc_year range is 0-99
                   .tm_wday = time.rtc_wday};
    time_t t    = mktime(&timeinfo);

    struct timeval tv = {
        .tv_sec  = t,
        .tv_usec = 0,
    };
    settimeofday(&tv, nullptr);
}

void HalEsp32::setAlarmTime(struct alarm_time time)
{
    powerhub.setWakeUpSource(WS_RTC_ALARM, true);
    powerhub.setAlarmTime({.alarm_min = time.alarm_min, .alarm_hour = time.alarm_hour, .alarm_day = time.alarm_day});
    powerhub.setAlarmState(true);
    auto pb_alarm = powerhub.getAlarmTime();
    hal::HalBase::alarm_time a_time;
    a_time.alarm_day  = pb_alarm.alarm_day;
    a_time.alarm_hour = pb_alarm.alarm_hour;
    a_time.alarm_min  = pb_alarm.alarm_min;
}

/* -------------------------------------------------------------------------- */
/*                                    Power                                   */
/* -------------------------------------------------------------------------- */

void HalEsp32::setPowerState(PowerControl device, uint16_t state)
{
    powerhub.setPowerState(static_cast<::PowerControl>(device), state);
}

bool HalEsp32::getPowerState(PowerControl device)
{
    return powerhub.getPowerState(static_cast<::PowerControl>(device));
}

uint16_t HalEsp32::getDeviceVoltage(VAMonitor device)
{
    return powerhub.getDeviceVoltage(static_cast<::VAMonitor>(device));
}
int16_t HalEsp32::getDeviceCurrent(VAMonitor device)
{
    return powerhub.getDeviceCurrent(static_cast<::VAMonitor>(device));
}
uint8_t HalEsp32::getPowerSupplyStatus()
{
    return powerhub.getPowerSupplyStatus();
}
void HalEsp32::setBusConfig(uint16_t voltage, uint8_t currentLimit, uint8_t enable, uint8_t direction)
{
    BusConfig cfg;
    cfg.voltage      = voltage;
    cfg.currentLimit = currentLimit;
    cfg.enable       = enable;
    cfg.direction    = direction;
    powerhub.setBusConfig(cfg);
}

/* -------------------------------------------------------------------------- */
/*                                    LED                                     */
/* -------------------------------------------------------------------------- */
void HalEsp32::setLedState(bool state)
{
    powerhub.setPowerState(static_cast<::PowerControl>(PC_LED), state);
}

bool HalEsp32::getLedState()
{
    return powerhub.getPowerState(static_cast<::PowerControl>(PC_LED));
}

uint32_t HalEsp32::getLedColor(LedControl led)
{
    return powerhub.getLEDColor(static_cast<::LedControl>(led));
}

void HalEsp32::setLedColor(LedControl led, uint32_t color)
{
    colors_buffer[led] = color;
}

void HalEsp32::setLedColor(uint8_t start_pos, uint8_t end_pos, uint32_t color)
{
    if (start_pos > end_pos) std::swap(start_pos, end_pos);
    for (uint8_t i = start_pos; i <= end_pos; i++) {
        colors_buffer[i] = color;
    }
}

void HalEsp32::setLedColor(const uint32_t* colors, size_t count)
{
    memcpy(colors_buffer, colors, sizeof(uint32_t) * count);
}

void HalEsp32::setBatteryLedColor(uint32_t color)
{
    colors_buffer[static_cast<::LedControl>(LC_POWER_L)] = color;
    colors_buffer[static_cast<::LedControl>(LC_POWER_R)] = color;
}

void HalEsp32::setBatteryLedBrightness(uint8_t brightness)
{
    setLedBrightness(LC_POWER_L, brightness);
    setLedBrightness(LC_POWER_R, brightness);
}

void HalEsp32::setUSBLedColor(uint32_t color)
{
    colors_buffer[static_cast<::LedControl>(LC_USB_C)] = color;
    colors_buffer[static_cast<::LedControl>(LC_USB_A)] = color;
}

void HalEsp32::setAllLedColor(uint32_t color)
{
    for (int i = 0; i < 8; i++) {
        colors_buffer[i] = color;
    }
}

void HalEsp32::updateLedColors()
{
    powerhub.updateLedColors(colors_buffer);
}

uint8_t HalEsp32::getLedBrightness(LedControl led)
{
    return powerhub.getLEDBrightness(static_cast<::LedControl>(led));
}

void HalEsp32::setLedBrightness(LedControl led, uint8_t brightness)
{
    if (led != LC_NONE) powerhub.setLEDBrightness(static_cast<::LedControl>(led), brightness);
}

void HalEsp32::setBatteryLedBreath(uint8_t interval, uint8_t interval_ms)
{
    _breath_brightness += _breath_direction * interval;
    if (_breath_brightness >= 80) {
        _breath_brightness = 80;
        _breath_direction  = -_breath_direction;
    } else if (_breath_brightness <= 5) {
        _breath_brightness = 5;
        _breath_direction  = -_breath_direction;
    }
    powerhub.setLEDBrightness(static_cast<::LedControl>(LC_POWER_L), _breath_brightness);
    powerhub.setLEDBrightness(static_cast<::LedControl>(LC_POWER_R), _breath_brightness);
    vTaskDelay(interval_ms / portTICK_PERIOD_MS);
    // mclog::tagInfo(_tag, "set led breath: {} ", interval);
}

/* -------------------------------------------------------------------------- */
/*                                  Interface                                 */
/* -------------------------------------------------------------------------- */

static esp_err_t i2c_scan(i2c_port_t i2c_port)
{
    esp_err_t ret;
    uint8_t address;

    printf("scan i2c device\n");
    printf("\n     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;

            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, 0x1);
            i2c_master_stop(cmd);
            ret = i2c_master_cmd_begin(i2c_port, cmd, 50);
            i2c_cmd_link_delete(cmd);

            if (ret == ESP_OK) {
                printf("%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("UU ");
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
    printf("\nscan i2c device finished\n");

    return ESP_OK;
}

static const char* _config_file_path = "/config.json";

void HalEsp32::setConfig(HalEsp32::Config_t& cfg)
{
    // Try open
    File file = LittleFS.open(_config_file_path, "w");
    if (!file) {
        mclog::error("open {} failed", _config_file_path);
        return;
    }

    // Parse json
    DynamicJsonDocument doc(1024);

    doc["wifiSsid"]     = cfg.wifi_ssid;
    doc["wifiPass"]     = cfg.wifi_password;
    doc["startPoster"]  = cfg.start_poster;
    doc["nickname"]     = cfg.nickname;
    doc["postInterval"] = cfg.post_interval;
    doc["timeZone"]     = cfg.time_zone;
    doc["i2c"]          = cfg.i2c;
    doc["usb"]          = cfg.usb;
    doc["rs485can"]     = cfg.rs485can;
    doc["uart"]         = cfg.uart;
    doc["timerMode"]    = cfg.timer_mode;
    doc["timeOfOn"]     = cfg.time_of_on;
    doc["timeOfOff"]    = cfg.time_of_off;
    doc["N"]            = cfg.N;
    doc["timerIsDone"]  = cfg.timer_is_done;

    serializeJson(doc, file);

    file.close();
}

HalEsp32::Config_t HalEsp32::getConfig()
{
    // Try open
    File file = LittleFS.open(_config_file_path, "r");
    if (!file) {
        mclog::error("open {} failed", _config_file_path);
        return _data.config;
    }

    // Parse json
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, file);

    // Copy into buffer
    _data.config.wifi_ssid     = doc["wifiSsid"].as<std::string>();
    _data.config.wifi_password = doc["wifiPass"].as<std::string>();
    _data.config.start_poster  = doc["startPoster"].as<std::string>();
    _data.config.nickname      = doc["nickname"].as<std::string>();
    _data.config.post_interval = doc["postInterval"];
    _data.config.time_zone     = doc["timeZone"].as<std::string>();
    _data.config.i2c           = doc["i2c"];
    _data.config.usb           = doc["usb"];
    _data.config.rs485can      = doc["rs485can"];
    _data.config.uart          = doc["uart"];
    _data.config.timer_mode    = doc["timerMode"];
    _data.config.time_of_on    = doc["timeOfOn"];
    _data.config.time_of_off   = doc["timeOfOff"];
    _data.config.N             = doc["N"];
    _data.config.timer_is_done = doc["timerIsDone"].as<bool>();

    file.close();

    return _data.config;
}

HalEsp32::Config_t HalEsp32::getDefaultConfig()
{
    Config_t ret;
    return ret;
}

void HalEsp32::config_init()
{
    // File system
    if (!LittleFS.begin(true)) {
        while (1) {
            mclog::info("file system init failed");
            delay(1000);
        }
    }

    mclog::info("config init");

    // Check exist
    if (!LittleFS.exists(_config_file_path)) {
        // Default config
        setConfig(_data.config);
    } else {
        mclog::info("{} already exsit", _config_file_path);
    }

    // Sync config
    getConfig();

    printConfig();
}

void HalEsp32::printConfig()
{
    mclog::info("\nconfig:\n----------------------");
    printf("wifi ssid: %s\n", _data.config.wifi_ssid.c_str());
    printf("wifi pass: %s\n", _data.config.wifi_password.c_str());
    printf("start poster: %s\n", _data.config.start_poster.c_str());
    printf("nickname: %s\n", _data.config.nickname.c_str());
    printf("post interval: %d\n", _data.config.post_interval);
    printf("time zone: %s\n", _data.config.time_zone.c_str());
    printf("usb: %d\n", _data.config.usb);
    printf("uart: %d\n", _data.config.uart);
    printf("rs485can: %d\n", _data.config.rs485can);
    printf("i2c: %d\n", _data.config.i2c);
    printf("timer mode: %d\n", _data.config.timer_mode);
    printf("time of on: %d\n", _data.config.time_of_on);
    printf("time of off: %d\n", _data.config.time_of_off);
    printf("N: %d\n", _data.config.N);
    printf("timer is done: %d\n", _data.config.timer_is_done);
    printf("----------------------\n");
}

bool HalEsp32::getWifiStatus()
{
    return WiFi.status() == WL_CONNECTED;
}

uint8_t HalEsp32::getStationNum()
{
    return WiFi.softAPgetStationNum();
}

bool HalEsp32::isConfigWifi()
{
    return _data.config.wifi_ssid.length() > 0 && _data.config.wifi_password.length() > 0;
}