/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>

void ledSelectShowTask(void* arg);

/**
 * @brief 派生 App
 *
 */
class AppView : public mooncake::AppAbility {
public:
    AppView();

    // 重写生命周期回调
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
};
