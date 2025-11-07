/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_launcher.h"
#include "apps/app_view/app_view.h"
#include "../app_setting/app_setting.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <sys/types.h>

using namespace mooncake;

AppLauncher::AppLauncher()
{
    setAppInfo().name = "AppLauncher";
}

void AppLauncher::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    open();
}

void AppLauncher::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    GetMooncake().openApp(GetHAL()->app_view_id);
    GetMooncake().openApp(GetHAL()->app_ezdata_id);
}

void AppLauncher::onRunning()
{
    // mclog::tagInfo(getAppInfo().name, "on running");

    GetHAL()->updateButtons();

    if (GetHAL()->btnOK.wasSingleClicked()) {
        mclog::info("btnOK single clicked");
        GetHAL()->_btn_is_pressed = true;
    }

    if (GetHAL()->btnSelect.wasClicked()) {
        GetHAL()->_btn_is_pressed = true;
    }

    if (GetHAL()->stop_setting_flag) {
        mclog::info("stop_setting_flag is true, open app_view");
        GetHAL()->stop_setting_flag = false;
        GetMooncake().openApp(GetHAL()->app_view_id);
    }
    if (!GetHAL()->setting_running) {
        if (GetHAL()->btnSelect.wasClicked() || GetHAL()->btnOK.wasClicked()) {
            mclog::info("btnSelect wasClicked");
            GetMooncake().closeApp(GetHAL()->app_view_id);
            GetMooncake().openApp(GetHAL()->app_setting_id);
        }
    }

    if (GetHAL()->btnSelect.pressedFor(4000)) {
        mclog::info("btnOK hold 4000ms");
        // Reset
        auto config = GetHAL()->getDefaultConfig();
        GetHAL()->setConfig(config);
        GetHAL()->delay(300);
        GetHAL()->restart();
        GetHAL()->delay(10000);
    }
}

void AppLauncher::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}
