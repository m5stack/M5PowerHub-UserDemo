/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "utils/Button_Class.hpp"
#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <mutex>
#include <vector>

/**
 * @brief Hardware abstraction layer
 *
 */
namespace hal {

/**
 * @brief
 *
 */
class HalBase {
public:
    virtual ~HalBase() = default;

    /**
     * @brief
     *
     * @return std::string
     */
    virtual std::string type()
    {
        return "Base";
    }

public:
    struct Config_t {
        std::string wifi_ssid;
        std::string wifi_password;
        std::string start_poster = "no";
        std::string nickname     = "PowerHub";
        std::string time_zone    = "GMT+0";
        int post_interval        = 5;
        bool usb                 = 0;
        bool uart                = 0;
        bool rs485can            = 0;
        bool i2c                 = 0;
        int timer_mode           = 1;
        int time_of_on           = 0;
        int time_of_off          = 0;
        bool timer_is_done       = 1;
        int N                    = 0;
    };

protected:
    struct Data_t {
        Config_t config;
        bool is_wifi_config_vaild = false;
        bool is_sd_card_valid     = false;
    };
    Data_t _data;

public:
    /**
     * @brief
     *
     */
    virtual void init()
    {
    }

    int app_view_id;
    int app_setting_id;
    int app_ezdata_id;
    // 当前选中的LED位置
    int current_location              = 1;
    bool stop_setting_flag            = false;
    bool setting_running              = false;
    bool ezdata_running               = false;
    std::string _device_token         = "";
    uint32_t _last_token_request_time = 0;

    /* --------------------------------- System --------------------------------- */
    virtual void delay(uint32_t ms)
    {
    }
    virtual uint32_t millis()
    {
        return 0;
    }
    virtual void restart()
    {
    }
    virtual void powerOff()
    {
    }

    bool _btn_is_pressed = false;

    /* --------------------------------- Button --------------------------------- */
    m5::Button_Class btnOK;
    m5::Button_Class btnSelect;
    m5::Button_Class btnC;
    virtual void updateButtons()
    {
    }

    /* ---------------------------------- LED ---------------------------------- */
    bool _color_led_state = false;

    enum LedControl {
        LC_NONE = -1,
        LC_USB_C,
        LC_USB_A,
        LC_UART,
        LC_BUS,
        LC_I2C,
        LC_BAT_CHARGE,
        LC_POWER_L,
        LC_POWER_R
    };

    virtual void setLedState(bool state)
    {
    }
    virtual bool getLedState()
    {
        return false;
    }
    virtual void setBatteryLedColor(uint32_t color)
    {
    }
    virtual uint32_t getLedColor(LedControl led)
    {
        return 0;
    }
    virtual void setLedColor(LedControl led, uint32_t color)
    {
    }
    virtual void setLedColor(uint8_t start_pos, uint8_t end_pos, uint32_t color)
    {
    }
    virtual void setLedColor(const uint32_t* colors, size_t count)
    {
    }
    virtual void setUSBLedColor(uint32_t color)
    {
    }
    virtual void setAllLedColor(uint32_t color)
    {
    }
    virtual void updateLedColors()
    {
    }
    virtual void setBatteryLedBrightness(uint8_t brightness)
    {
    }
    virtual uint8_t getLedBrightness(LedControl led)
    {
        return 0;
    }
    virtual void setLedBrightness(LedControl led, uint8_t brightness)
    {
    }
    virtual void setBatteryLedBreath(uint8_t interval, uint8_t interval_ms)
    {
    }

    /* ---------------------------------- Power --------------------------------- */
    struct PMData_t {
        float busVoltage   = 0.0f;
        float busPower     = 0.0f;
        float shuntVoltage = 0.0f;
        float shuntCurrent = 0.0f;
    };
    PMData_t powerMonitorData;

    enum VAMonitor { VM_BAT, VM_CAN_BUS, VM_RS485_BUS, VM_USB, VM_I2C, VM_UART };

    static constexpr LedControl loadLedMapping[6] = {
        hal::HalBase::LC_NONE,   // VM_BAT
        hal::HalBase::LC_BUS,    // VM_CAN_BUS
        hal::HalBase::LC_BUS,    // VM_RS485_BUS
        hal::HalBase::LC_USB_A,  // VM_USB
        hal::HalBase::LC_I2C,    // VM_I2C
        hal::HalBase::LC_UART,   // VM_UART
    };

    enum PowerControl { PC_LED, PC_USB, PC_I2C, PC_UART, PC_BUS, PC_VAMeter, PC_Charge };

    static constexpr LedControl powerLedMapping[7] = {
        hal::HalBase::LC_NONE,   // PC_LED
        hal::HalBase::LC_USB_A,  // PC_USB
        hal::HalBase::LC_I2C,    // PC_I2C
        hal::HalBase::LC_UART,   // PC_UART
        hal::HalBase::LC_BUS,    // PC_BUS
        hal::HalBase::LC_NONE,   // PC_VAMeter
        hal::HalBase::LC_NONE,   // PC_Charge
    };

    static constexpr PowerControl ledPowerMapping[6] = {
        hal::HalBase::PC_USB,   // LC_USB_C
        hal::HalBase::PC_USB,   // LC_USB_A
        hal::HalBase::PC_UART,  // LC_UART
        hal::HalBase::PC_BUS,   // LC_BUS
        hal::HalBase::PC_Charge,
        hal::HalBase::PC_I2C,  // LC_I2C
    };

    static constexpr PowerControl VAMonitorToPowerControl[6] = {
        hal::HalBase::PC_Charge,  // VM_BAT
        hal::HalBase::PC_BUS,     // VM_CAN_BUS
        hal::HalBase::PC_BUS,     // VM_RS485_BUS
        hal::HalBase::PC_USB,     // VM_USB
        hal::HalBase::PC_I2C,     // VM_I2C
        hal::HalBase::PC_UART     // VM_UART
    };

    virtual void setPowerState(PowerControl device, uint16_t state)
    {
    }
    virtual bool getPowerState(PowerControl device)
    {
        return 0;
    }
    virtual uint16_t getDeviceVoltage(VAMonitor device)
    {
        return 0;
    }
    virtual int16_t getDeviceCurrent(VAMonitor device)
    {
        return 0;
    }
    virtual uint8_t getPowerSupplyStatus()
    {
        return 0;
    }
    virtual void setBusConfig(uint16_t voltage, uint8_t currentLimit, uint8_t enable, uint8_t direction)
    {
    }

    /* ----------------------------------- RTC ---------------------------------- */
    struct alarm_time {
        uint8_t alarm_min;
        uint8_t alarm_hour;
        uint8_t alarm_day;
    };

    virtual void getRtcTime(tm* time)
    {
    }
    virtual void setRtcTime(tm time)
    {
    }
    virtual void setAlarmTime(struct alarm_time time)
    {
    }

    /* --------------------------------- Network -------------------------------- */
    bool _ezdata_connected = false;
    bool _connect_timeout  = false;

    virtual void ws_init()
    {
    }
    virtual void setConfig(Config_t& cfg)
    {
    }
    virtual Config_t getConfig()
    {
        return Config_t();
    }
    virtual Config_t getDefaultConfig()
    {
        return Config_t();
    }
    virtual void printConfig()
    {
    }
    virtual void config_init()
    {
    }
    virtual void start_ap_server()
    {
    }
    virtual void stop_ap_server()
    {
    }
    virtual void start_poster_server(bool internal_call = false)
    {
    }
    virtual bool getWifiStatus()
    {
        return false;
    }
    virtual uint8_t getStationNum()
    {
        return 0;
    }
    virtual bool isConfigWifi()
    {
        return false;
    }
    virtual void start_blufi_server()
    {
    }
    virtual void stop_blufi_server(void)
    {
    }
};

/**
 * @brief Get the HAL instance
 *
 * @return HalBase&
 */
HalBase* Get();

/**
 * @brief Inject the HAL, which will call init() to initialize the HAL
 *
 * @param hal
 */
void Inject(std::unique_ptr<HalBase> hal);

/**
 * @brief Destroy the HAL instance
 *
 */
void Destroy();

/**
 * @brief Check if the HAL instance exists
 *
 * @return true
 * @return false
 */
bool Check();

}  // namespace hal

/**
 * @brief Get the HAL instance
 *
 * @return hal::HalBase&
 */
inline hal::HalBase* GetHAL()
{
    return hal::Get();
}