/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <mooncake.h>
#include <mooncake_log.h>
#include "api_poster.h"
#include "hal/hal.h"
#include <LittleFS.h>

static void _task_start_poster(void* param)
{
    delay(500);
    mclog::info("start poster");
    auto config         = GetHAL()->getConfig();
    config.start_poster = "yes";
    GetHAL()->setConfig(config);
    GetHAL()->start_poster_server(true);

    vTaskDelete(NULL);
}

void startPoster(AsyncWebServerRequest* request)
{
    xTaskCreate(_task_start_poster, "reboot", 4000, NULL, 15, NULL);
    request->send(200, "application/json", "{\"msg\":\"ok\"}");
}

void load_poster_apis(AsyncWebServer& server)
{
    server.on("/api/v1/start_poster", HTTP_GET, startPoster);
}