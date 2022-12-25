#include "stdcompat.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <random>
#include <span>
#include <tuple>
#include <utility>

#include <Stuff/Maths/Hash/Sha2.hpp>

#include "cmsis_os.h"
#include "main.h"

#include "benchmarks.hpp"
#include "secrets.hpp"
#include "util.hpp"

extern "C" {
extern CRC_HandleTypeDef hcrc;
extern I2C_HandleTypeDef hi2c1;
extern I2S_HandleTypeDef hi2s3;
extern RNG_HandleTypeDef hrng;
extern SPI_HandleTypeDef hspi1;
}

static P256::PrivateKey s_privkey;

extern "C" void cpp_init() {
    HAL_RNG_Init(&hrng);
    HAL_CRC_Init(&hcrc);

    Tele::start_led_tasks();
    s_privkey = Tele::get_sk_from_config();

    //Tele::p256_test(s_privkey);
    //Tele::signature_benchmark(s_privkey);
}

extern "C" void cpp_os_exit() {
    HAL_CRC_DeInit(&hcrc);
    HAL_RNG_DeInit(&hrng);
}