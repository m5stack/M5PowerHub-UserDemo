/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_template.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>

using namespace mooncake;

AppTemplate::AppTemplate()
{
    // 配置 App 信息
    setAppInfo().name = "AppTemplate";
}

void AppTemplate::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    // 打开自己
    open();
}

void AppTemplate::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
}

void AppTemplate::onRunning()
{
    // mclog::tagInfo(getAppInfo().name, "on running");
}

void AppTemplate::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}
