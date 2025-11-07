/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <mooncake.h>
#include <mooncake_log.h>
#include "Arduino.h"
#include "api_system.h"
#include "hal/hal.h"
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <DNSServer.h>

DNSServer dnsServer;
const String localIPURL = "http://192.168.4.1";  // a string version of the local IP with http, used for redirecting
                                                 // clients to your webpage

void getToken(AsyncWebServerRequest *request)
{
    std::string &device_token = hal::Get()->_device_token;
    uint64_t chipid           = ESP.getEfuseMac();
    uint32_t getTokenTime     = millis();
    char chipid_str[5];
    snprintf(chipid_str, sizeof(chipid_str), "%04X", (uint16_t)((ESP.getEfuseMac() >> 32) & 0xFFFF));

    mclog::info("get token touched");

    if (device_token.empty()) {
        request->send(200, "application/json", "{\"msg\":\"pending\",\"token\":\"\"}");
        return;
    }

    String result = "{\"msg\":\"ok\",\"mac\":\"";
    result += chipid_str;
    result += "\",\"token\":\"";
    result += device_token.c_str();
    result += "\"}";
    mclog::info("result: {}", result.c_str());
    request->send(200, "application/json", result);

    hal::Get()->_last_token_request_time = millis();  // 记录请求时间
}

#include <FS.h>
#include <LittleFS.h>

void getConfig(AsyncWebServerRequest *request)
{
    request->send(LittleFS, "/config.json");
}

void resetConfig(AsyncWebServerRequest *request)
{
    auto cfg = hal::Get()->getDefaultConfig();
    hal::Get()->setConfig(cfg);
    request->send(200, "application/json", "{\"msg\":\"ok\"}");
}

#include <ArduinoJson.h>
#include <AsyncJson.h>

void setConfig(AsyncWebServerRequest *request, JsonVariant &json)
{
    hal::HalBase::Config_t config;

    // Copy configs
    config.wifi_ssid     = json["wifiSsid"].as<std::string>();
    config.wifi_password = json["wifiPass"].as<std::string>();
    config.start_poster  = json["startPoster"].as<std::string>();
    config.post_interval = json["postInterval"];
    config.nickname      = json["nickname"].as<std::string>();
    config.time_zone     = json["timeZone"].as<std::string>();

    // Check nickname
    if (config.nickname == "" || config.nickname == "null") config.nickname = "PowerHub";

    // Check bad configs
    if (config.start_poster == "" || config.post_interval == 0 || config.time_zone == "") {
        request->send(500, "application/json", "{\"msg\":\"bad config\"}");
        return;
    }
    if (config.wifi_ssid == "null" || config.wifi_password == "null" || config.start_poster == "null" ||
        config.time_zone == "null") {
        request->send(500, "application/json", "{\"msg\":\"bad config\"}");
        return;
    }

    // Store
    hal::Get()->setConfig(config);
    request->send(200, "application/json", "{\"msg\":\"ok\"}");
}

bool first_touch_scan = true;
static std::array<std::string, 20> _wifi_list_buffer;

void scanWifiListOnce()
{
    mclog::info("start scan wifi list (once after AP up)");

    xTaskCreate(
        [](void *param) {
            while (1) {
                int n = WiFi.scanNetworks();
                mclog::info("wifi scan done, num: {}", n);

                for (int i = 0; i < n; i++) {
                    if (i >= _wifi_list_buffer.size()) break;
                    _wifi_list_buffer[i] = WiFi.SSID(i).c_str();
                }
                WiFi.scanDelete();
                break;
            }
            vTaskDelete(NULL);
        },
        "scan_wifi", 8192, NULL, 5, NULL);
}

void getWifiList(AsyncWebServerRequest *request)
{
    if (!first_touch_scan) {
        mclog::info("start async wifi scan");
        WiFi.scanNetworks(true, false, false, 120);  // 异步

        unsigned long start = millis();
        while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
            delay(10);
            if (millis() - start > 10000) {
                mclog::warn("wifi scan timeout");
                return;
            }
        }

        int n = WiFi.scanComplete();
        mclog::info("wifi scan done, num: {}", n);

        for (int i = 0; i < n; i++) {
            if (i >= _wifi_list_buffer.size()) break;
            _wifi_list_buffer[i] = WiFi.SSID(i).c_str();
        }
        WiFi.scanDelete();
    } else {
        mclog::info("first touch scan wifi list skipped");
        first_touch_scan = false;
    }
    // Json
    DynamicJsonDocument doc(4096);
    for (int i = 0; i < _wifi_list_buffer.size(); i++) {
        doc["wifiList"][i] = _wifi_list_buffer[i];
    }

    String json_buffer;
    serializeJson(doc, json_buffer);
    mclog::info("result:\n {}", json_buffer.c_str());

    request->send(200, "application/json", json_buffer);
}

void setUpDNSServer(DNSServer &dnsServer, const IPAddress &localIP)
{
// Define the DNS interval in milliseconds between processing DNS requests
#define DNS_INTERVAL 30

    // Set the TTL for DNS response and start the DNS server
    dnsServer.setTTL(3600);
    dnsServer.start(53, "*", localIP);
}

void load_system_apis(AsyncWebServer &server)
{
    IPAddress localIP = WiFi.softAPIP();

    setUpDNSServer(dnsServer, localIP);

    server.on("/api/v1/get_token", HTTP_GET, getToken);
    server.on("/api/v1/get_config", HTTP_GET, getConfig);
    AsyncCallbackJsonWebHandler *getConfigHandler = new AsyncCallbackJsonWebHandler("/api/v1/set_config", setConfig);
    server.addHandler(getConfigHandler);
    server.on("/api/v1/reset_config", HTTP_GET, resetConfig);
    server.on("/api/v1/get_wifi_list", HTTP_GET, getWifiList);

    scanWifiListOnce();
    // Captiave Portal
    //  Required
    server.on("/connecttest.txt", [](AsyncWebServerRequest *request) {
        request->redirect("http://logout.net");
    });  // windows 11 captive portal workaround
    server.on("/wpad.dat", [](AsyncWebServerRequest *request) {
        request->send(404);
    });  // Honestly don't understand what this is but a 404 stops win 10 keep calling this repeatedly and panicking the
         // esp32 :)

    // Background responses: Probably not all are Required, but some are. Others might speed things up?
    // A Tier (commonly used by modern systems)
    server.on("/generate_204", [](AsyncWebServerRequest *request) {
        request->redirect(localIPURL);
    });  // android captive portal redirect
    server.on("/redirect",
              [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });  // microsoft redirect
    server.on("/hotspot-detect.html",
              [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });  // apple call home
    server.on("/canonical.html", [](AsyncWebServerRequest *request) {
        request->redirect(localIPURL);
    });  // firefox captive portal call home
    server.on("/success.txt",
              [](AsyncWebServerRequest *request) { request->send(200); });  // firefox captive portal call home
    server.on("/ncsi.txt", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });  // windows call home

    // B Tier (uncommon)
    //  server.on("/chrome-variations/seed",[](AsyncWebServerRequest *request){request->send(200);}); //chrome captive
    //  portal call home server.on("/service/update2/json",[](AsyncWebServerRequest *request){request->send(200);});
    //  //firefox? server.on("/chat",[](AsyncWebServerRequest *request){request->send(404);}); //No stop asking
    //  Whatsapp, there is no internet connection server.on("/startpage",[](AsyncWebServerRequest
    //  *request){request->redirect(localIPURL);});

    // the catch all
    server.onNotFound([](AsyncWebServerRequest *request) {
        request->redirect(localIPURL);
        Serial.print("onnotfound ");
        Serial.print(
            request->host());  // This gives some insight into whatever was being requested on the serial monitor
        Serial.print(" ");
        Serial.print(request->url());
        Serial.print(" sent redirect to " + localIPURL + "\n");
    });
}
