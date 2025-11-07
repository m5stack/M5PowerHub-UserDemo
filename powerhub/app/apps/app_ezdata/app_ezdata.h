/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <cstdint>
#include <mooncake.h>
#include <mooncake_log.h>

/**
 * @brief AppEzdata
 *
 */
class AppEzdata : public mooncake::AppAbility {
public:
    AppEzdata();

    // 重写生命周期回调
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    struct Data_t {
        std::uint32_t time_count = 0;
    };
    Data_t _data;
};