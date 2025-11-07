/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "powerhub.h"

std::unordered_map<LedControl, LedRegisterInfo> ledRegisterMap = {
    {LC_USB_C, {0x60, 0x80}}, {LC_USB_A, {0x64, 0x81}},      {LC_UART, {0x68, 0x82}},    {LC_BUS, {0x6C, 0x83}},
    {LC_I2C, {0x70, 0x84}},   {LC_BAT_CHARGE, {0x74, 0x85}}, {LC_POWER_L, {0x78, 0x86}}, {LC_POWER_R, {0x7C, 0x87}}};

std::unordered_map<VAMonitor, VARegisterInfo> VARegisterMap = {{VM_BAT, {0x30, 0x32}},       {VM_CAN_BUS, {0x34, 0x36}},
                                                               {VM_RS485_BUS, {0x38, 0x3A}}, {VM_USB, {0x3C, 0x3E}},
                                                               {VM_I2C, {0x40, 0x42}},       {VM_UART, {0x44, 0x46}}};

unsigned long IRAM_ATTR millis()
{
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

static i2c_bus_device_handle_t powerhub_dev = NULL;

bool PowerHub::begin(i2c_bus_handle_t i2c_bus, uint8_t addr, uint8_t sda, uint8_t scl, uint32_t speed)
{
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "i2c_bus is NULL");
        return false;
    }
    _i2c_bus      = i2c_bus;
    _addr         = addr;
    _sda          = sda;
    _scl          = scl;
    powerhub_dev = i2c_bus_device_create(i2c_bus, addr, speed);

    if (powerhub_dev == NULL) {
        ESP_LOGE(TAG, "Failed to create i2c device");
        return false;
    }

    gpio_config_t io_conf;
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BTN_SELECT);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    return true;
}

void PowerHub::writeRegister(uint8_t reg, uint8_t *buffer, uint8_t length)
{
    i2c_bus_write_bytes(powerhub_dev, reg, length, buffer);
}

void PowerHub::readRegister(uint8_t reg, uint8_t *buffer, uint8_t length)
{
    i2c_bus_read_bytes(powerhub_dev, reg, length, buffer);
}

uint8_t PowerHub::readRegister8(uint8_t reg)
{
    uint8_t value;
    readRegister(reg, &value, 1);
    return value;
}

void PowerHub::writeRegister8(uint8_t reg, uint8_t value)
{
    uint8_t buf[1] = {value};
    writeRegister(reg, buf, 1);
}

void PowerHub::setPowerState(PowerControl device, bool state)
{
    uint8_t data[1];
    data[0] = state;
    writeRegister(POWERHUB_POWER_CTR_REG + device, data, 1);
}

bool PowerHub::getPowerState(PowerControl device)
{
    uint8_t data[1];
    readRegister(POWERHUB_POWER_CTR_REG + device, data, 1);
    return data[0];
}

void PowerHub::setUSBMode(USBMode mode)
{
    uint8_t data[1];
    data[0] = mode;
    writeRegister(POWERHUB_USB_MODE_REG, data, 1);
}

uint8_t PowerHub::getUSBMode()
{
    uint8_t data[1];
    readRegister(POWERHUB_USB_MODE_REG, data, 1);
    return data[0];
}

void PowerHub::setBusConfig(BusConfig config)
{
    uint8_t data[5];
    data[0] = config.voltage & 0xFF;
    data[1] = config.voltage >> 8;
    data[2] = config.currentLimit & 0xFF;
    data[3] = config.enable;
    data[4] = config.direction;
    writeRegister(POWERHUB_BUS_CFG_REG, data, 5);
}

BusConfig PowerHub::getBusConfig()
{
    uint8_t data[5];
    readRegister(POWERHUB_BUS_CFG_REG, data, 5);
    BusConfig config;
    config.voltage      = (data[1] << 8) | data[0];
    config.currentLimit = data[2];
    config.enable       = data[3];
    config.direction    = data[4];
    return config;
}

uint16_t PowerHub::getDeviceVoltage(VAMonitor device)
{
    uint8_t data[2];
    readRegister(VARegisterMap[device].voltageReg, data, 2);
    return (data[1] << 8) | data[0];
}
int16_t PowerHub::getDeviceCurrent(VAMonitor device)
{
    uint8_t data[2];
    readRegister(VARegisterMap[device].currentReg, data, 2);
    return (data[1] << 8) | data[0];
}

uint8_t PowerHub::getChargeStatus()
{
    uint8_t data[1];
    readRegister(POWERHUB_CHG_STA_REG, data, 1);
    return data[0];
}

uint8_t PowerHub::getPowerSupplyStatus()
{
    uint8_t data[1];
    readRegister(POWERHUB_PWR_SUP_REG, data, 1);
    return data[0];
}

void PowerHub::setLEDColor(LedControl device, uint32_t color)
{
    uint8_t data[3];
    data[0] = color & 0xFF;
    data[1] = (color >> 8) & 0xFF;
    data[2] = (color >> 16) & 0xFF;
    writeRegister(ledRegisterMap[device].colorStartReg, data, 3);
}

void fillColorArray(uint8_t *data, uint32_t color, int groups, bool appendZero)
{
    for (int i = 0; i < groups; i++) {
        data[i * (appendZero ? 4 : 3)]     = color & 0xFF;
        data[i * (appendZero ? 4 : 3) + 1] = (color >> 8) & 0xFF;
        data[i * (appendZero ? 4 : 3) + 2] = (color >> 16) & 0xFF;
        if (appendZero) {
            data[i * 4 + 3] = 0x00;
        }
    }
}

void PowerHub::updateLedColors(const uint32_t *colors)
{
    uint8_t data[32] = {0};
    for (size_t i = 0; i < 8; ++i) {
        data[i * 4 + 0] = colors[i] & 0xFF;
        data[i * 4 + 1] = (colors[i] >> 8) & 0xFF;
        data[i * 4 + 2] = (colors[i] >> 16) & 0xFF;
        data[i * 4 + 3] = 0x00;
        // printf("i: %zu, i*4+1: %d\n", i, i * 4 + 1);
    }
    writeRegister(ledRegisterMap[LC_USB_C].colorStartReg, data, 32);
}

uint32_t PowerHub::getLEDColor(LedControl device)
{
    uint8_t data[3];
    readRegister(ledRegisterMap[device].colorStartReg, data, 3);
    return (data[2] << 16) | (data[1] << 8) | data[0];
}

void PowerHub::setLEDBrightness(LedControl device, uint8_t brightness)
{
    uint8_t data[1];
    data[0] = brightness;
    writeRegister(ledRegisterMap[device].brightnessReg, data, 1);
}
uint8_t PowerHub::getLEDBrightness(LedControl device)
{
    uint8_t data[1];
    readRegister(ledRegisterMap[device].brightnessReg, data, 1);
    return data[0];
}

SC8721Config PowerHub::getSC8721Config()
{
    uint8_t data[10], writeData[1] = {1};
    const unsigned long timeout = 1000;
    unsigned long startTime     = millis();

    writeRegister(POWERHUB_SC8721_CFG_REG, writeData, 1);
    while (millis() - startTime <= timeout) {
        readRegister(POWERHUB_SC8721_CFG_REG, writeData, 1);

        if (writeData[0] == 0) {
            readRegister(POWERHUB_SC8721_CFG_REG + 1, data, 10);
            break;
        }
    }

    SC8721Config config = {};
    if (writeData[0] == 0) {
        config.csoValue          = data[0];
        config.slopeCompensation = data[1];
        config.outputVoltageSet  = (data[2] << 8) | (data[3]);
        config.control           = data[4];
        config.systemSetting     = data[5];
        config.frequency         = data[6];
        config.statusFlags       = (data[8] << 8) | data[7];
    } else {
        ESP_LOGE(TAG, "Timeout while waiting for SC8721 configuration.");
    }
    return config;
}

bool PowerHub::getButtonState(ButtonKey key)
{
    if (key == BTN_SELECT) {
        return gpio_get_level((gpio_num_t)key);
    } else {
        uint8_t data[1];
        readRegister(POWERHUB_BUTTON_REG, data, 1);
        // Serial.printf("Button State: 0b");
        // for (int i = 7; i >= 0; i--) {
        //     Serial.print((data[0] >> i) & 1);
        // }
        // Serial.println();
        return data[0] >> key & 0x01;
    }
}

void PowerHub::setWakeUpSource(WakeUpSource source, bool state)
{
    uint8_t data[1];
    data[0] = state;
    writeRegister(POWERHUB_WAKEUP_REG + source, data, 1);
}

bool PowerHub::checkWakeUpSource(WakeUpSource source)
{
    uint8_t data[1];
    readRegister((POWERHUB_WAKEUP_REG + source), data, 1);
    return data[0];
}

void PowerHub::setRTCTime(rtc_time time)
{
    uint8_t data[7];
    data[0]            = time.rtc_sec;
    data[1]            = time.rtc_min;
    data[2]            = time.rtc_hour;
    data[3]            = time.rtc_day;
    data[4]            = time.rtc_mon;
    data[5]            = time.rtc_year;
    uint8_t wday_map[] = {2, 4, 8, 10, 20, 40, 1};  // Monday -> Sunday
    data[6]            = (time.rtc_wday >= 1 && time.rtc_wday <= 7) ? wday_map[time.rtc_wday - 1] : 0;
    // printf("raw wday:%d\n", (time.rtc_wday >= 1 && time.rtc_wday <= 7) ? wday_map[time.rtc_wday - 1] : 0);
    writeRegister(POWERHUB_RTC_TIME_REG, data, 7);
}

struct rtc_time PowerHub::getRTCTime()
{
    uint8_t data[7];
    readRegister(POWERHUB_RTC_TIME_REG, data, 7);
    rtc_time time;
    time.rtc_sec  = data[0];
    time.rtc_min  = data[1];
    time.rtc_hour = data[2];
    time.rtc_day  = data[3];
    time.rtc_mon  = data[4];
    time.rtc_year = data[5];

    uint8_t wday_map[] = {1, 2, 4, 8, 10, 20, 40};
    time.rtc_wday      = -1;
    for (int i = 0; i < 7; i++) {
        if (data[6] == wday_map[i]) {
            time.rtc_wday = i + 1;
            break;
        }
    }
    return time;
}

void PowerHub::setAlarmTime(struct alarm_time time)
{
    uint8_t data[3];
    data[0] = time.alarm_min;
    data[1] = time.alarm_hour;
    data[2] = time.alarm_day;
    writeRegister(POWERHUB_RTC_ALARM_REG, data, 3);
}

struct alarm_time PowerHub::getAlarmTime()
{
    uint8_t data[3];
    readRegister(POWERHUB_RTC_ALARM_REG, data, 3);
    alarm_time time;
    time.alarm_min  = data[0];
    time.alarm_hour = data[1];
    time.alarm_day  = data[2];
    return time;
}

void PowerHub::setAlarmState(bool state)
{
    uint8_t data[1];
    data[0] = state;
    writeRegister(POWERHUB_RTC_ALARM_CTR_REG, data, 1);
}

bool PowerHub::getAlarmState()
{
    uint8_t data[1];
    readRegister(POWERHUB_RTC_ALARM_CTR_REG, data, 1);
    return data[0];
}

void PowerHub::powerOff()
{
    uint8_t data[1];
    data[0] = 1;
    writeRegister(POWERHUB_POWER_OFF, data, 1);
}

uint8_t PowerHub::getBootloaderVersion()
{
    uint8_t data[1];
    readRegister(POWERHUB_BL_VERSION_REG, data, 1);
    return data[0];
}

uint8_t PowerHub::getFirmwareVersion()
{
    uint8_t data[1];
    readRegister(POWERHUB_FW_VERSION_REG, data, 1);
    return data[0];
}

void PowerHub::setI2CAddress(uint8_t newAddr)
{
    uint8_t data[1];
    data[0] = newAddr;
    writeRegister(POWERHUB_I2C_ADDR_CFG_REG, data, 1);
}

uint8_t PowerHub::getI2CAddress()
{
    uint8_t data[1];
    readRegister(POWERHUB_I2C_ADDR_CFG_REG, data, 1);
    return data[0];
}