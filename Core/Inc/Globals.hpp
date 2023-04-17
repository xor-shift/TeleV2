#pragma once

#include <string_view>

#include <fmt/core.h>

#include <Stuff/Maths/BLAS/Vector.hpp>

#include <cmsis_os.h>
#include <main.h>
#include <p256.hpp>

extern "C" {
extern CRC_HandleTypeDef hcrc;
extern I2C_HandleTypeDef hi2c1;
extern I2S_HandleTypeDef hi2s3;
extern RNG_HandleTypeDef hrng;
extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart6;
extern CAN_HandleTypeDef hcan1;
}

namespace Tele {

static UART_HandleTypeDef& s_gsm_uart = huart2;
static UART_HandleTypeDef& s_gps_uart = huart3;
static UART_HandleTypeDef& s_nextion_uart = huart6;

extern P256::PrivateKey g_privkey;

P256::PrivateKey get_sk_from_config();

void init_globals();

void run_benchmarks();
void run_tests();

}
