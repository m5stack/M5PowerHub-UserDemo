/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <memory>
#include <hal/hal.h>
#include "app_launcher/app_launcher.h"
#include "app_view/app_view.h"
#include "app_setting/app_setting.h"
#include "app_ezdata/app_ezdata.h"
/* Header files locator (Don't remove) */

/**
 * @brief App 安装回调
 *
 * @param mooncake
 */
inline void on_install_apps()
{
    // 安装 App
    // mooncake::GetMooncake().installApp(std::make_unique<AppTemplate>());
    mooncake::GetMooncake().installApp(std::make_unique<AppLauncher>());
    GetHAL()->app_view_id    = mooncake::GetMooncake().installApp(std::make_unique<AppView>());
    GetHAL()->app_setting_id = mooncake::GetMooncake().installApp(std::make_unique<AppSetting>());
    GetHAL()->app_ezdata_id  = mooncake::GetMooncake().installApp(std::make_unique<AppEzdata>());
    /* Install app locator (Don't remove) */
}
