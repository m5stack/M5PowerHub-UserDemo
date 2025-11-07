/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <set>
#include <WiFi.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include "hal/hal_esp32.h"
#include "../utils/daemon_control/daemon_control.h"

// #define USE_WEBSOCKET
#define USE_MQTT

#if defined(USE_WEBSOCKET)
#include <WebSocketsClient.h>
#elif defined(USE_MQTT)
#include "../utils/arduino-mqtt/src/MQTTClient.h"
#endif

#define DEVICE_ADD_DATA    100
#define DEVICE_MODIFY_DATA 101
#define DEVICE_QUERY_DATA  103
#define DEVICE_INFO_DATA   104
#define USER_MODIFY_DATA   107

class EzdataDaemonControl_t : public DaemonControl_t {
public:
    bool is_register_ok             = false;
    bool is_wifi_connected          = false;
    bool is_ezdata_connected        = false;
    std::string current_wifi_status = "not config";
};

typedef struct {
    uint8_t switch_state = 0;
    uint16_t voltage     = 0;
    int16_t current      = 0;
    uint8_t timerMode    = 1;
    uint8_t timeOfOn     = 0;
    uint8_t timeOfOff    = 0;
    int16_t N            = 0;
    uint32_t _timer      = 0;
    bool _loop           = false;
    bool _mode_active    = false;
} ChannelConfig_t;

static EzdataDaemonControl_t* _daemon_control = nullptr;
#if defined(USE_WEBSOCKET)
static WebSocketsClient* _webSocket       = nullptr;
static const char* _ezdata_websocket_host = "ezdata2.m5stack.com";
static const std::string _ez_tag          = "ezdata_ws";
#elif defined(USE_MQTT)
static WiFiClient* _wifi_client = nullptr;
static MQTTClient* _mqtt_client = nullptr;
static std::string _ezdata_mqtt_topic_up;
static std::string _ezdata_mqtt_topic_down;
static const char* _ezdata_mqtt_host = "ezdata2.m5stack.com";
static const std::string _ez_tag     = "ezdata_mqtt";
#endif
static std::string _device_token;
static int8_t _ntp_time_zone = 0;
static bool _query_data      = false;
static int _post_interval    = 5;
static ChannelConfig_t channel_cfg[7];  // 6 channels + system
uint32_t heart_beat_time_count = millis();
uint32_t time_count            = 0;
static uint8_t power_direction = 0;
static uint8_t current_limit   = 100;
static uint16_t output_voltage = 3000;

void updatePortModes()
{
    uint32_t now = millis();
    for (int i = 0; i < 6; ++i) {
        auto& device = channel_cfg[i];
        switch (device.timerMode) {
            mclog::tagInfo(_ez_tag, "channel {} enter timerMode {}", i, device.timerMode);
            case 0:
            case 1:
                device.timeOfOn  = 0;
                device.timeOfOff = 0;
                device.N         = 0;
                break;
            case 2:
            case 3: {
                bool first_level_high = ((device.timerMode & 1) == 0);
                if (!device._mode_active) {
                    device._mode_active = true;
                    device._timer       = now;
                    mclog::tagInfo(_ez_tag, "channel {} timerMode {}: auto turn {} after {} seconds", i,
                                   device.timerMode, first_level_high ? "on" : "off", device.timeOfOn);
                    GetHAL()->setPowerState(hal::HalBase::VAMonitorToPowerControl[i], first_level_high);
                }
                if (now - device._timer >= (first_level_high ? device.timeOfOn : device.timeOfOff) * 1000) {
                    GetHAL()->setPowerState(hal::HalBase::VAMonitorToPowerControl[i], !first_level_high);
                    device.timerMode = 1;
                }
                break;
            }
            case 4:
            case 5:
            case 6:
            case 7: {
                bool first_level_high = ((device.timerMode & 1) == 0);
                if (!device._mode_active) {
                    device._mode_active = true;
                    device._timer       = now;
                    device.N = (device.timerMode == 4 || device.timerMode == 5) ? 1 : device.N;  // 4,5模式N=1, 6,7:N=N
                    device._loop =
                        ((device.timerMode == 6 || device.timerMode == 7) && (device.N == 1));  // 6,7模式N=1时循环
                    mclog::tagInfo(_ez_tag, "channel {} timerMode {}: open {}s then turn off {}s, N={}, loop {}", i,
                                   device.timerMode, device.timeOfOn, device.timeOfOff, device.N, device._loop);
                }
                if (device.N > 0) {
                    if (now - device._timer < (first_level_high ? device.timeOfOn : device.timeOfOff) * 1000) {
                        GetHAL()->setPowerState(hal::HalBase::VAMonitorToPowerControl[i], first_level_high);
                    } else {
                        GetHAL()->setPowerState(hal::HalBase::VAMonitorToPowerControl[i], !first_level_high);
                        if (now - device._timer >= (device.timeOfOn + device.timeOfOff) * 1000) {
                            if (!device._loop) {
                                device.N--;
                                if (device.N == 0) {
                                    GetHAL()->setPowerState(hal::HalBase::VAMonitorToPowerControl[i], first_level_high);
                                    device.timerMode = 1;
                                }
                            }
                            device._timer = now;
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

void updateSystemModes()
{
    uint32_t now               = millis();
    auto& device               = channel_cfg[6];
    auto cfg                   = GetHAL()->getConfig();
    static bool _recovery_mode = false;
    if (4 <= cfg.timer_mode && cfg.timer_mode <= 7 && !_recovery_mode) {
        device.timerMode = cfg.timer_mode;
        device.timeOfOn  = cfg.time_of_on;
        device.timeOfOff = cfg.time_of_off;
        device.N         = cfg.N;
        mclog::tagDebug(_ez_tag, "This is recovery mode, timerMode: {}, timeOfOn: {}, timeOfOff: {}, N: {}",
                        device.timerMode, device.timeOfOn, device.timeOfOff, device.N);
        _recovery_mode = true;
    }

    switch (device.timerMode) {
        mclog::tagInfo(_ez_tag, "System enter timerMode {}", device.timerMode);
        case 0:
            GetHAL()->powerOff();
            break;
        case 1:
            device.timeOfOn  = 0;
            device.timeOfOff = 0;
            device.N         = 0;
            break;
        case 2:  // 开启一段时间后关闭
            if (!device._mode_active) {
                device._mode_active = true;
                device._timer       = now;
                mclog::tagInfo(_ez_tag, "System timerMode {}: auto turn off after {} seconds", device.timerMode,
                               device.timeOfOn);
            }
            if (now - device._timer >= device.timeOfOn * 1000) {
                GetHAL()->powerOff();
            }
            break;
        case 3: {  // 马上关闭, 一段时间后开启
            mclog::tagInfo(_ez_tag, "System timerMode {}: auto turn on after {} seconds", device.timerMode,
                           device.timeOfOn);
            struct tm tm;
            GetHAL()->getRtcTime(&tm);
            // device.timeOfOff 为x秒，转换为分钟和小时
            int new_min  = tm.tm_min + std::max(1, device.timeOfOff / 60);
            int new_hour = tm.tm_hour + (new_min / 60);
            GetHAL()->setAlarmTime({.alarm_min  = static_cast<unsigned char>(new_min % 60),
                                    .alarm_hour = static_cast<unsigned char>(new_hour % 24),
                                    .alarm_day  = static_cast<unsigned char>(tm.tm_mday + (new_hour / 24))});
            GetHAL()->powerOff();
            break;
        }
        case 4:
            device.N = 1;
        case 6: {                     // 开启一段时间 (A) 后关闭一段时间 (B) , 最终保持开启
            if (cfg.timer_is_done) {  // 关闭前保存模式相关配置
                cfg.timer_is_done = false;
            }

            if (!device._mode_active) {
                device._mode_active = true;
                device._timer       = now;
                device._loop =
                    ((device.timerMode == 6 || device.timerMode == 7) && (device.N == 1));  // 6,7模式N=1时循环
                mclog::tagInfo(_ez_tag, "System  timerMode {}: auto turn off after {} seconds", device.timerMode,
                               device.timeOfOn);
            }

            if (now - device._timer >= device.timeOfOn * 1000) {
                if (!device._loop) device.N--;
                if (device.N > 0) {
                    cfg.N           = device.N;
                    cfg.timer_mode  = device.timerMode;
                    cfg.time_of_on  = device.timeOfOn;
                    cfg.time_of_off = device.timeOfOff;
                } else {
                    cfg.timer_is_done = true;
                    cfg.timer_mode    = 1;
                    cfg.time_of_on    = 0;
                    cfg.time_of_off   = 0;
                }
                GetHAL()->setConfig(cfg);

                struct tm tm;
                GetHAL()->getRtcTime(&tm);
                // device.timeOfOff 为x秒，转换为分钟和小时
                int new_min  = tm.tm_min + std::max(1, device.timeOfOff / 60);
                int new_hour = tm.tm_hour + (new_min / 60);
                GetHAL()->setAlarmTime({.alarm_min  = static_cast<unsigned char>(new_min % 60),
                                        .alarm_hour = static_cast<unsigned char>(new_hour % 24),
                                        .alarm_day  = static_cast<unsigned char>(tm.tm_mday + (new_hour / 24))});

                GetHAL()->powerOff();
            }
            break;
        }
        case 5:
            device.N = 1;
        case 7: {                     // 关闭一段时间 (B) 后开启一段时间 (A) , 最终保持关闭
            if (cfg.timer_is_done) {  // 关闭前保存模式相关配置
                cfg.timer_is_done = false;
                cfg.timer_mode    = device.timerMode;
                cfg.time_of_on    = device.timeOfOn;
                cfg.time_of_off   = device.timeOfOff;
                cfg.N             = device.N;
                GetHAL()->setConfig(cfg);

                struct tm tm;
                GetHAL()->getRtcTime(&tm);
                int new_min  = tm.tm_min + std::max(1, device.timeOfOff / 60);
                int new_hour = tm.tm_hour + (new_min / 60);
                GetHAL()->setAlarmTime({.alarm_min  = static_cast<unsigned char>(new_min % 60),
                                        .alarm_hour = static_cast<unsigned char>(new_hour % 24),
                                        .alarm_day  = static_cast<unsigned char>(tm.tm_mday + (new_hour / 24))});
                GetHAL()->powerOff();
            } else {  // 开机后继续执行
                if (!device._mode_active) {
                    device._mode_active = true;
                    device._timer       = now;
                    device._loop =
                        ((device.timerMode == 6 || device.timerMode == 7) && (device.N == 1));  // 6,7模式N=1时循环
                    mclog::tagInfo(_ez_tag, "System timerMode {}: auto turn off after {} seconds", device.timerMode,
                                   device.timeOfOn);
                }
                if (now - device._timer >= device.timeOfOn * 1000) {
                    if (!device._loop) device.N--;
                    if (device.N != 0) {
                        cfg.timer_mode  = device.timerMode;
                        cfg.time_of_on  = device.timeOfOn;
                        cfg.time_of_off = device.timeOfOff;
                        cfg.N           = device.N;

                        struct tm tm;
                        GetHAL()->getRtcTime(&tm);
                        // device.timeOfOff 为x秒，转换为分钟和小时
                        int new_min  = tm.tm_min + std::max(1, device.timeOfOff / 60);
                        int new_hour = tm.tm_hour + (new_min / 60);
                        GetHAL()->setAlarmTime({.alarm_min  = static_cast<unsigned char>(new_min % 60),
                                                .alarm_hour = static_cast<unsigned char>(new_hour % 24),
                                                .alarm_day = static_cast<unsigned char>(tm.tm_mday + (new_hour / 24))});
                    } else {
                        cfg.timer_is_done = true;
                        cfg.timer_mode    = 1;
                        cfg.time_of_on    = 0;
                        cfg.time_of_off   = 0;
                    }
                    GetHAL()->setConfig(cfg);
                    GetHAL()->powerOff();
                }
            }
            break;
        }
        default:
            break;
    }
}

static void get_dev_indices(const String& name, int8_t& dev_vm, int8_t& dev_switch)
{
    if (name == "USB") {
        dev_vm     = hal::HalBase::VM_USB;
        dev_switch = hal::HalBase::PC_USB;
    } else if (name == "I2C") {
        dev_vm     = hal::HalBase::VM_I2C;
        dev_switch = hal::HalBase::PC_I2C;
    } else if (name == "UART") {
        dev_vm     = hal::HalBase::VM_UART;
        dev_switch = hal::HalBase::PC_UART;
    } else if (name == "BAT-CHARGE") {
        dev_vm     = hal::HalBase::VM_BAT;
        dev_switch = hal::HalBase::PC_Charge;
    } else if (name == "CAN") {
        dev_vm     = hal::HalBase::VM_CAN_BUS;
        dev_switch = hal::HalBase::PC_BUS;
    } else if (name == "RS485") {
        dev_vm     = hal::HalBase::VM_RS485_BUS;
        dev_switch = hal::HalBase::PC_BUS;
    } else if (name == "485/CAN") {
        dev_vm     = hal::HalBase::VM_CAN_BUS;
        dev_switch = hal::HalBase::PC_BUS;
    } else if (name == "SYSTEM") {
        dev_vm     = 6;
        dev_switch = -1;
    } else {
        dev_vm     = -1;
        dev_switch = -1;
    }
}

static bool _connect_wifi()
{
    bool ret = true;
    _daemon_control->Borrow();
    _daemon_control->is_wifi_connected   = ret;
    _daemon_control->current_wifi_status = ret ? "connected" : "disconnected";
    _daemon_control->Return();
    return ret;
}

static void _start_ntp()
{
    const char* ntpServer        = "pool.ntp.org";
    const long gmtOffset_sec     = 3600 * _ntp_time_zone;
    const int daylightOffset_sec = 0;

    mclog::tagInfo(_ez_tag, "[ntp] try start ntp..");

    // 配置 NTP 服务器
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

static void _update_rtc_time()  // 存UTC到RTC
{
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        time_t local      = mktime(&timeinfo);
        struct tm* utc_tm = gmtime(&local);
        GetHAL()->setRtcTime(*utc_tm);

        struct tm current_time;
        GetHAL()->getRtcTime(&current_time);
        mclog::tagDebug(_ez_tag, "RTC time: {}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", current_time.tm_year,
                        current_time.tm_mon, current_time.tm_mday, current_time.tm_hour, current_time.tm_min,
                        current_time.tm_sec);
    } else {
        mclog::tagWarn(_ez_tag, "[ntp] get time failed");
    }
}

static bool _get_device_token()
{
    _device_token.clear();
    mclog::tagInfo(_ez_tag, "try get device token");

    const char* api_url = "https://ezdata2.m5stack.com/api/v2/device/registerMac";
    mclog::tagInfo(_ez_tag, "api url: {}", api_url);

    HTTPClient http;
    http.begin(api_url);
    http.addHeader("Content-Type", "application/json");

    String json_content;
    {
        uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
        WiFi.macAddress(mac);
        char mac_str[18] = {0};
        sprintf(mac_str, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        JsonDocument doc;
        doc["deviceType"] = "powerhub";
        doc["mac"]        = mac_str;
        serializeJson(doc, json_content);
    }

    mclog::tagInfo(_ez_tag, "post json:\n{}", json_content.c_str());
    int response_code = http.POST(json_content);
    mclog::tagInfo(_ez_tag, "get response code: {}", response_code);

    if (response_code <= 0) {
        return false;
    }

    String response = http.getString();
    mclog::tagInfo(_ez_tag, "get response:\n{}", response.c_str());

    if (response_code != HTTP_CODE_OK) {
        return false;
    }

    JsonDocument doc;
    deserializeJson(doc, response);
    if (doc["code"].as<int>() != 200) {
        mclog::tagError(_ez_tag, "invalid code: {}", doc["code"].as<int>());
        return false;
    }
    if (doc["data"].isNull()) {
        mclog::tagError(_ez_tag, "empty token");
        return false;
    }
    _device_token = doc["data"].as<std::string>();
    mclog::tagInfo(_ez_tag, "get token: {}", _device_token);
    GetHAL()->_device_token = _device_token;

    http.end();
    return true;
}

static JsonDocument send_doc;
static String send_json_content;

template <typename T>
void ws_send_json(T requestType, const String& name = "", int8_t dev_vm = -1, int8_t dev_switch = -1)
{
    send_doc.clear();
    send_json_content.clear();

    send_doc["deviceToken"] = GetHAL()->_device_token;
    if (!(String(requestType) == "ping")) {
        JsonObject body     = send_doc.createNestedObject("body");
        body["requestType"] = requestType;
        if (name != "") {
            body["name"] = name;
            get_dev_indices(name, dev_vm, dev_switch);
            JsonObject value = body.createNestedObject("value");
            if (name == "REBOOT SW-STATUS") {
                auto cfg         = GetHAL()->getConfig();
                value["USB"]     = int(cfg.usb);
                value["UART"]    = int(cfg.uart);
                value["485/CAN"] = int(cfg.rs485can);
                value["I2C"]     = int(cfg.i2c);
            } else if (name == "SYSTEM") {
                auto cfg           = GetHAL()->getConfig();
                value["timerMode"] = cfg.timer_mode;
                value["timeOfOn"]  = cfg.time_of_on;
                value["timeOfOff"] = cfg.time_of_off;
                value["N"]         = cfg.N;
            } else if (name == "485/CAN-CONFIG") {
                value["powerDirection"] = power_direction;
                value["outputVoltage"]  = output_voltage;
                value["currentLimit"]   = current_limit;
            } else {
                value["timerMode"] = channel_cfg[dev_vm].timerMode;
                value["timeOfOn"]  = channel_cfg[dev_vm].timeOfOn;
                value["timeOfOff"] = channel_cfg[dev_vm].timeOfOff;
                value["N"]         = channel_cfg[dev_vm].N;
                value["voltage"]   = String(channel_cfg[dev_vm].voltage / 1000.0, 3) + "V";
                if (name == "485/CAN") {
                    value["current485"] = String(channel_cfg[VM_RS485_BUS].current / 1000.0, 3) + "A";
                    value["currentCAN"] = String(channel_cfg[VM_CAN_BUS].current / 1000.0, 3) + "A";
                } else {
                    value["current"] = String(channel_cfg[dev_vm].current / 1000.0, 3) + "A";
                }
                value["switch"] = GetHAL()->getPowerState((hal::HalBase::PowerControl)dev_switch) ? 1 : 0;
            }
        }
    } else {
        send_doc["body"] = "ping";
    }
    serializeJson(send_doc, send_json_content);
    mclog::tagDebug(_ez_tag, "[Local] post {}:{}", requestType, send_json_content.c_str());
#if defined(USE_WEBSOCKET)
    _webSocket->sendTXT(send_json_content);
#elif defined(USE_MQTT)
    _mqtt_client->publish(_ezdata_mqtt_topic_up.c_str(), send_json_content.c_str());
#endif
}

static bool _was_controlled = false;

void onResponse(JsonDocument& responseDoc)
{
    if (!_query_data) {
        if (responseDoc["cmd"] == DEVICE_QUERY_DATA && responseDoc["code"] == 200 &&
            responseDoc["body"].is<JsonArray>()) {
            std::vector<String> requiredNames = {
                "USB", "UART", "I2C", "485/CAN", "485/CAN-CONFIG", "BAT-CHARGE", "SYSTEM", "REBOOT SW-STATUS"};
            std::set<String> existNames;
            for (JsonObject item : responseDoc["body"].as<JsonArray>()) {
                existNames.insert(item["name"].as<String>());
            }
            printf("existing names:\n");
            for (const auto& name : existNames) {
                printf(" - %s\n", name.c_str());
            }

            for (auto& name : requiredNames) {
                if (existNames.find(name) == existNames.end()) {
                    mclog::tagInfo(_ez_tag, "[Local] {} not found, adding default data", name.c_str());
                    ws_send_json(DEVICE_ADD_DATA, name);
                }
            }
            ws_send_json(DEVICE_MODIFY_DATA, "REBOOT SW-STATUS");
            ws_send_json(DEVICE_MODIFY_DATA, "SYSTEM");
        }
        _query_data = true;
    } else {
        if (responseDoc["cmd"] == USER_MODIFY_DATA && responseDoc["code"] == 200 &&
            responseDoc["body"].is<JsonObject>()) {
            // bool remoteControlDetected = false;
            JsonObject item  = responseDoc["body"].as<JsonObject>();
            String name      = item["name"].as<String>();
            JsonObject value = item["value"].as<JsonObject>();
            String valueStr;
            serializeJson(value, valueStr);
            mclog::tagInfo(_ez_tag, "[Remote]: {} data update to: = {}", name.c_str(), valueStr.c_str());

            bool remoteState = false;

            int8_t dev_vm     = -1;
            int8_t dev_switch = -1;
            get_dev_indices(name, dev_vm, dev_switch);

            if (dev_vm != -1) {
                auto& device     = channel_cfg[dev_vm];
                device.timerMode = value["timerMode"].as<int>();
                mclog::tagDebug(_ez_tag, "[Remote]: {} timerMode is {}", name.c_str(), device.timerMode);
                if (dev_switch != -1) {
                    if (device.timerMode == 1) {
                        bool localState = GetHAL()->getPowerState((hal::HalBase::PowerControl)dev_switch);
                        if (value["switch"].is<int>()) {
                            remoteState = (value["switch"].as<int>() == 1);
                        } else if (value["switch"].is<bool>()) {
                            remoteState = value["switch"].as<bool>();
                        }
                        if (remoteState != localState) {
                            _was_controlled = true;
                            mclog::tagInfo(_ez_tag, "[Remote]: control detected: {} -> {}", name.c_str(),
                                           remoteState ? 1 : 0);
                            if (dev_switch == hal::HalBase::PC_Charge) {
                                mclog::tagInfo(_ez_tag, "[Remote]: charging control detected, adding delay");
                                ws_send_json(DEVICE_MODIFY_DATA, "BAT-CHARGE");
                            }
                            GetHAL()->setPowerState((hal::HalBase::PowerControl)dev_switch, remoteState);
                        }
                    } else if (device.timerMode == 0) {
                        GetHAL()->setPowerState((hal::HalBase::PowerControl)dev_switch, 0);
                    }
                }
                device.timeOfOn     = value["timeOfOn"].as<int>();
                device.timeOfOff    = value["timeOfOff"].as<int>();
                device.N            = value["N"].as<int>();
                device._mode_active = false;
                // mclog::tagDebug(_ez_tag,
                //                 "[Remote]: {} config updated: timerMode: {}, timeOfOn: {}, timeOfOff: {}, N: {}",
                //                 name.c_str(), device.timerMode, device.timeOfOn, device.timeOfOff, device.N);
            } else if (name == "REBOOT SW-STATUS") {
                mclog::tagInfo(_ez_tag, "[Remote]: reboot switch set");
                auto cfg     = GetHAL()->getConfig();
                cfg.usb      = value["USB"].as<bool>();
                cfg.uart     = value["UART"].as<bool>();
                cfg.rs485can = value["485/CAN"].as<bool>();
                cfg.i2c      = value["I2C"].as<bool>();
                GetHAL()->setConfig(cfg);
                mclog::tagDebug(_ez_tag, "[Remote]: reboot switch config: USB: {}, UART: {}, RS485/CAN: {}, I2C: {}",
                                cfg.usb, cfg.uart, cfg.rs485can, cfg.i2c);
            } else if (name == "485/CAN-CONFIG") {
                power_direction = value["powerDirection"].as<int>();
                output_voltage  = value["outputVoltage"].as<int>();
                current_limit   = value["currentLimit"].as<int>();
                GetHAL()->setBusConfig(output_voltage, current_limit, true, power_direction);
            }
        }
    }
}

static JsonDocument ws_doc;
static String ws_json_content;

#if defined(USE_WEBSOCKET)
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length)
{
    switch (type) {
        case WStype_DISCONNECTED:
            mclog::tagWarn(_ez_tag, "[WSc] Disconnected");
            GetHAL()->_ezdata_connected = false;
            break;
        case WStype_CONNECTED:
            mclog::tagInfo(_ez_tag, "[WSc] Connected to url: {}", (const char*)payload);
            // ws login - 使用静态分配的JSON对象
            ws_doc.clear();
            ws_doc["deviceToken"] = GetHAL()->_device_token;
            ws_json_content.clear();
            serializeJson(ws_doc, ws_json_content);
            _webSocket->sendTXT(ws_json_content);
            mclog::tagInfo(_ez_tag, "post login json:\n{}", ws_json_content.c_str());
            break;
        case WStype_TEXT:
            mclog::tagDebug(_ez_tag, "[WSc] get text: {}", (const char*)payload);
            if (String((char*)payload) == "device Login successful") {
                mclog::tagInfo(_ez_tag, "login successful");
                GetHAL()->_ezdata_connected = true;
                ws_send_json(DEVICE_QUERY_DATA);
            } else {
                JsonDocument doc;
                if (deserializeJson(doc, payload) != DeserializationError::Ok) {
                    mclog::tagWarn(_ez_tag, "parse msg json failed");
                    return;
                }
                onResponse(doc);
                GetHAL()->_color_led_state = true;
            }
            break;
        case WStype_BIN:
        case WStype_ERROR:
            mclog::tagError(_ez_tag, "[WSc] Error or binary data received, type: {}", (int)type);
            break;
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
    }
}
#elif defined(USE_MQTT)
static void _on_mqtt_msg(String& topic, String& payload)
{
    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationError::Ok) {
        mclog::tagWarn(_ez_tag, "parse msg json failed");
        return;
    }
    GetHAL()->_color_led_state = true;
    mclog::tagDebug(_ez_tag, "get payload: {}", payload.c_str());
    onResponse(doc);
}
#endif

static bool _connect_ezdata()
{
    bool ret = false;
#if defined(USE_WEBSOCKET)
    mclog::tagInfo(_ez_tag, "try connect ezdata ws..");
#if !defined(USE_LOCAL_NETWORK)
    _webSocket->beginSslWithBundle(_ezdata_websocket_host, 443, "/ws", NULL);
#else
    _webSocket->begin(_ezdata_websocket_host, 8188, "/ws", NULL);
#endif
    _webSocket->onEvent(webSocketEvent);
    _webSocket->setReconnectInterval(5000);
    while (1) {
        _webSocket->loop();
        if (GetHAL()->_ezdata_connected) {
            mclog::tagInfo(_ez_tag, "ezdata websocket connected");
            ret = true;
            break;
        }
        mclog::tagWarn(_ez_tag, "ezdata websocket connecting...");
        delay(100);
    }
#elif defined(USE_MQTT)
    mclog::tagInfo(_ez_tag, "try connect ezdata mqtt..");

    // Get client id format: ez<mac_address>ez
    std::string client_id = "ez";
    {
        uint8_t mac[6]   = {0};
        char mac_str[18] = {0};
        WiFi.macAddress(mac);
        sprintf(mac_str, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        client_id += mac_str;
    }
    client_id += "ez";

    mclog::tagInfo(_ez_tag, "username: {}", _device_token);
    mclog::tagInfo(_ez_tag, "client_id: {}", client_id);

    // Try connect
    mclog::tagInfo(_ez_tag, "try connect mqtt..");
    _mqtt_client->begin(_ezdata_mqtt_host, *_wifi_client);
    while (1) {
        if (_mqtt_client->connect(client_id.c_str(), _device_token.c_str())) {
            mclog::tagInfo(_ez_tag, "mqtt connected");
            ret = true;
            break;
        }
        mclog::tagWarn(_ez_tag, "mqtt connect failed, retry in 1s");
        delay(1000);
    }
#endif
    _daemon_control->Borrow();
    _daemon_control->is_ezdata_connected = ret;
    _daemon_control->Return();

    return ret;
}
#if defined(USE_MQTT)
static bool _setup_ezdata_mqtt()
{
    bool ret = false;

    // Get topic names
    _ezdata_mqtt_topic_up   = "$ezdata/" + _device_token + "/up";
    _ezdata_mqtt_topic_down = "$ezdata/" + _device_token + "/down";
    mclog::tagInfo(_ez_tag, "get mqtt topic names:\n{}\n{}", _ezdata_mqtt_topic_up, _ezdata_mqtt_topic_down);

    // Subscribe down topic
    _mqtt_client->subscribe(_ezdata_mqtt_topic_down.c_str());
    _mqtt_client->onMessage(_on_mqtt_msg);

    return ret;
}
#endif

static void _upload_powerhub_status()
{
    for (int i = 0; i < 6; i++) {
        static uint16_t voltage = 0;
        static int16_t current  = 0;
        voltage                 = GetHAL()->getDeviceVoltage(static_cast<hal::HalBase::VAMonitor>(i));
        current                 = GetHAL()->getDeviceCurrent(static_cast<hal::HalBase::VAMonitor>(i));
        channel_cfg[i].voltage  = voltage;
        channel_cfg[i].current  = current;
        // mclog::tagDebug(_ez_tag, "device {} voltage: {}V, current: {}A", i, voltage, current);
    }
    time_count            = millis();
    heart_beat_time_count = millis();
    ws_send_json(DEVICE_MODIFY_DATA, "USB");
    ws_send_json(DEVICE_MODIFY_DATA, "UART");
    ws_send_json(DEVICE_MODIFY_DATA, "485/CAN");
    ws_send_json(DEVICE_MODIFY_DATA, "485/CAN-CONFIG");
    ws_send_json(DEVICE_MODIFY_DATA, "I2C");
    ws_send_json(DEVICE_MODIFY_DATA, "BAT-CHARGE");
    ws_send_json(DEVICE_MODIFY_DATA, "SYSTEM");
}

static bool _check_output_changed()
{
    bool is_changed = false;
    for (int i = 0; i < 7; i++) {
        if (i == hal::HalBase::PC_LED || i == hal::HalBase::PC_VAMeter || i == hal::HalBase::PC_Charge) {
            continue;
        }

        bool current_state = GetHAL()->getPowerState(static_cast<hal::HalBase::PowerControl>(i));
        if (current_state != channel_cfg[i].switch_state) {
            const char* power_name = nullptr;
            int8_t dev_vm          = -1;
            int8_t dev_switch      = -1;
            if (i == hal::HalBase::PC_USB) {
                power_name = "USB";
            } else if (i == hal::HalBase::PC_UART)
                power_name = "UART";
            else if (i == hal::HalBase::PC_I2C)
                power_name = "I2C";
            else if (i == hal::HalBase::PC_BUS)
                power_name = "485/CAN";
            else if (i == hal::HalBase::PC_Charge)
                power_name = "BAT-CHARGE";

            if (power_name != nullptr) {
                get_dev_indices(power_name, dev_vm, dev_switch);
                mclog::tagDebug(_ez_tag, "history state: {}, current state: {}", channel_cfg[i].switch_state,
                                int(current_state));
                if (channel_cfg[i].switch_state == 0 &&
                    channel_cfg[dev_vm].timerMode == 0) {  // 历史开关状态为0且历史模式为0, 并且开关状态变化
                    channel_cfg[dev_vm].timerMode = 1;
                }
                if (power_name == "485/CAN" && current_state) {
                    GetHAL()->setBusConfig(output_voltage, current_limit, true, power_direction);
                }
                ws_send_json(DEVICE_MODIFY_DATA, power_name);
            }
            is_changed = true;
        }
        channel_cfg[i].switch_state = current_state;
    }
    return is_changed;
}

static void _ezdata_daemon(void* param)
{
    if (uxTaskGetStackHighWaterMark(NULL) < 1024) {
        mclog::tagWarn(_ez_tag, "警告: daemon堆栈剩余不足: %d 字节", uxTaskGetStackHighWaterMark(NULL));
    }

    mclog::tagInfo(_ez_tag, "ezdata daemon start");

    size_t stack_left = uxTaskGetStackHighWaterMark(NULL);
    mclog::tagInfo(_ez_tag, "ezdata daemon task stack left: {} bytes", stack_left);

    while (!hal::Check()) {
        mclog::tagInfo(_ez_tag, "wait hal valid..");
        delay(1000);
    }

    auto cfg           = GetHAL()->getConfig();
    std::string tz_str = cfg.time_zone;
    mclog::tagInfo(_ez_tag, "time zone: {}", tz_str.c_str());
    int8_t tz_val = 0;
    if (tz_str.find("GMT") == 0) {
        size_t plus_pos  = tz_str.find('+');
        size_t minus_pos = tz_str.find('-');
        if (plus_pos != std::string::npos) {
            tz_val = std::stoi(tz_str.substr(plus_pos + 1));
        } else if (minus_pos != std::string::npos) {
            tz_val = -std::stoi(tz_str.substr(minus_pos + 1));
        }
    }
    _ntp_time_zone  = tz_val;
    _daemon_control = new EzdataDaemonControl_t;

    if (cfg.wifi_ssid.length() == 0) {
        mclog::tagWarn(_ez_tag, "empty wifi config, return");
        return;
    }
    _post_interval = cfg.post_interval;

    mclog::tagInfo(_ez_tag, "post interval: {} seconds", _post_interval);
    // Try connect wifi
    if (!_connect_wifi()) {
        mclog::tagWarn(_ez_tag, "try connect wifi failed");
        _daemon_control->DaemonGone();
        vTaskDelete(NULL);
    }
    mclog::tagInfo(_ez_tag, "connect ok, ip: {}", WiFi.localIP().toString().c_str());

    // NTP setup
    _start_ntp();

    // Update RTC time
    _update_rtc_time();

    // Check token
    while (!_get_device_token()) {
        mclog::tagError(_ez_tag, "ezdata token invalid, retry in 10s");
        delay(10000);
    }

#if defined(USE_WEBSOCKET)
    _webSocket = new WebSocketsClient;
#elif defined(USE_MQTT)
    _wifi_client = new WiFiClient;
    _mqtt_client = new MQTTClient(4096, 512);
#endif

    uint32_t output_check_time_count = millis();

    while (1) {
#if defined(USE_WEBSOCKET)
        _webSocket->loop();
        if (!_webSocket->isConnected()) {
            _connect_ezdata();
#elif defined(USE_MQTT)
        _mqtt_client->loop();
        if (!_mqtt_client->connected()) {
            _connect_ezdata();
            _setup_ezdata_mqtt();
            mclog::tagInfo(_ez_tag, "login successful");
            GetHAL()->_ezdata_connected = true;
            ws_send_json(DEVICE_QUERY_DATA);
#endif
        } else {
            if (GetHAL()->_btn_is_pressed) {
                channel_cfg[6].timerMode = 1;
                auto cfg                 = GetHAL()->getConfig();
                cfg.timer_is_done        = true;
                GetHAL()->setConfig(cfg);
                for (int i = 0; i < 6; i++) {
                    channel_cfg[i].timerMode = 1;
                }
                GetHAL()->_btn_is_pressed = false;
            }
            if (_query_data) {
                updatePortModes();
                updateSystemModes();

                // Upload if was controlled
                if (_was_controlled) {
                    _was_controlled = false;
                    mclog::tagInfo(_ez_tag, "[Remote] was controlled, upload status");
                    _upload_powerhub_status();
                    output_check_time_count = millis();
                }

                // Upload if output changed
                if (millis() - output_check_time_count > 1000) {
                    if (_check_output_changed()) {
                        mclog::tagInfo(_ez_tag, "output changed, upload status");
                        _upload_powerhub_status();
                    }
                    output_check_time_count = millis();
                }

                // Upload status every x seconds
                if (millis() - time_count > _post_interval * 1000) {
                    _upload_powerhub_status();
                }

                if (millis() - heart_beat_time_count > 45000) {
                    mclog::tagInfo(_ez_tag, "[Local] send ping to keep connection alive");
                    ws_send_json("ping");
                    heart_beat_time_count = millis();
                }
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void HalEsp32::ws_init()
{
    mclog::tagInfo(_ez_tag, "ezdata init");

    // 检查主任务栈剩余空间
    size_t main_stack_left = uxTaskGetStackHighWaterMark(NULL);
    mclog::tagInfo(_ez_tag, "main task stack left: {} bytes", main_stack_left);

    xTaskCreate(_ezdata_daemon, "ezdata", 8192, NULL, 15, NULL);
}
