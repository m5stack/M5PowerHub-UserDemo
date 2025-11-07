/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef __POWERHUB_H__
#define __POWERHUB_H__

#include "i2c_bus.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <unordered_map>
#include <cstdint>
#include <string.h>
#define POWERHUB_ADDRESS 0x50

#define POWERHUB_POWER_CTR_REG     0x00
#define POWERHUB_USB_MODE_REG      0x10
#define POWERHUB_BUS_CFG_REG       0x20
#define POWERHUB_CHG_STA_REG       0x50
#define POWERHUB_PWR_SUP_REG       0x51
#define POWERHUB_SC8721_CFG_REG    0x90
#define POWERHUB_BUTTON_REG        0xA0
#define POWERHUB_WAKEUP_REG        0xB0
#define POWERHUB_RTC_TIME_REG      0xC0
#define POWERHUB_RTC_ALARM_REG     0xD0
#define POWERHUB_RTC_ALARM_CTR_REG 0xD3
#define POWERHUB_POWER_OFF         0xE0

#define POWERHUB_BL_VERSION_REG   0xFC
#define POWERHUB_FW_VERSION_REG   0xFE
#define POWERHUB_I2C_ADDR_CFG_REG 0xFF

enum PowerControl { PC_LED, PC_USB, PC_I2C, PC_UART, PC_BUS, PC_VAMeter, PC_Charge };

enum LedControl { LC_NONE = -1, LC_USB_C, LC_USB_A, LC_UART, LC_BUS, LC_I2C, LC_BAT_CHARGE, LC_POWER_L, LC_POWER_R };
enum VAMonitor { VM_BAT, VM_CAN_BUS, VM_RS485_BUS, VM_USB, VM_I2C, VM_UART };

enum ButtonKey { BTN_OK, BTN_KEY2, BTN_SELECT = 11 };
enum WakeUpSource { WS_RTC_ALARM, WS_VIN, WS_BUTTON };
enum USBMode { SLAVE_MODE, HOST_MODE_TPYE_A, HOST_MODE_TPYE_C };

struct BusConfig {
    uint16_t voltage;
    uint8_t currentLimit;
    uint8_t enable;
    uint8_t direction;
};

struct LedRegisterInfo {
    uint8_t colorStartReg;
    uint8_t brightnessReg;
};

struct VARegisterInfo {
    uint8_t voltageReg;
    uint8_t currentReg;
};

struct SC8721Config {
    uint8_t csoValue;
    uint8_t slopeCompensation;
    uint16_t outputVoltageSet;
    uint8_t control;
    uint8_t systemSetting;
    uint8_t frequency;
    uint16_t statusFlags;
};

struct rtc_time {
    uint8_t rtc_sec;
    uint8_t rtc_min;
    uint8_t rtc_hour;
    uint8_t rtc_day;
    uint8_t rtc_mon;
    uint8_t rtc_year;
    uint8_t rtc_wday;
};

struct alarm_time {
    uint8_t alarm_min;
    uint8_t alarm_hour;
    uint8_t alarm_day;
};

extern std::unordered_map<LedControl, LedRegisterInfo> ledRegisterMap;
extern std::unordered_map<VAMonitor, VARegisterInfo> VARegisterMap;

#define TAG "PowerHub"

class PowerHub {
public:
    bool begin(i2c_bus_handle_t busHandle, uint8_t addr = POWERHUB_ADDRESS, uint8_t sda = 45, uint8_t scl = 48,
               uint32_t speed = 100000L);
    // void setCANPower(uint8_t status);

    void setPowerState(PowerControl device, bool state);
    bool getPowerState(PowerControl device);

    void setUSBMode(USBMode mode);
    uint8_t getUSBMode();

    void setBusConfig(BusConfig config);
    BusConfig getBusConfig();

    uint16_t getDeviceVoltage(VAMonitor device);
    int16_t getDeviceCurrent(VAMonitor device);
    uint8_t getChargeStatus();
    uint8_t getPowerSupplyStatus();
    void setLEDColor(LedControl device, uint32_t color);
    void updateLedColors(const uint32_t* colors);
    uint32_t getLEDColor(LedControl device);
    void setLEDBrightness(LedControl device, uint8_t brightness);
    uint8_t getLEDBrightness(LedControl device);

    SC8721Config getSC8721Config();

    bool getButtonState(ButtonKey key);

    void setWakeUpSource(WakeUpSource source, bool state);
    bool checkWakeUpSource(WakeUpSource source);

    void setRTCTime(struct rtc_time time);
    struct rtc_time getRTCTime();

    void setAlarmTime(struct alarm_time time);
    struct alarm_time getAlarmTime();

    void setAlarmState(bool state);
    bool getAlarmState();

    void powerOff();

    uint8_t getBootloaderVersion();
    uint8_t getFirmwareVersion();
    void setI2CAddress(uint8_t newAddr);
    uint8_t getI2CAddress();

private:
    i2c_bus_handle_t _i2c_bus;
    uint8_t _addr;
    uint8_t _scl;
    uint8_t _sda;

protected:
    uint8_t readRegister8(uint8_t reg);
    void writeRegister8(uint8_t reg, uint8_t value);
    void readRegister(uint8_t reg, uint8_t* buffer, uint8_t length);
    void writeRegister(uint8_t reg, uint8_t* buffer, uint8_t length);
};

#endif
