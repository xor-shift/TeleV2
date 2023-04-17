#include <Globals.hpp>

#include <atomic>

#include <queue.h>
#include <stream_buffer.h>
#include <usbd_cdc_if.h>

#include <Stuff/Maths/Check/CRC.hpp>

#include <Tele/CharConv.hpp>
#include <Tele/Log.hpp>
#include <Tele/Stream.hpp>
#include <Tele/STUtilities.hpp>

#include <PlainSink.hpp>
#include <benchmarks.hpp>
#include <secrets.hpp>

namespace Tele {

P256::PrivateKey g_privkey;

P256::PrivateKey get_sk_from_config() {
    P256::PrivateKey sk;
    Tele::from_chars(std::span(sk.d), Tele::Config::sk_text, std::endian::little);

    if (!sk.compute_pk())
        Error_Handler();

    if (Tele::Config::pkx_text.empty() || Tele::Config::pky_text.empty())
        return sk;

    std::array<uint32_t, 8> expected_pk_x;
    Tele::from_chars(std::span(expected_pk_x), Tele::Config::pkx_text, std::endian::little);
    std::array<uint32_t, 8> expected_pk_y;
    Tele::from_chars(std::span(expected_pk_y), Tele::Config::pky_text, std::endian::little);

    if (sk.pk.x != expected_pk_x)
        Error_Handler();

    if (sk.pk.y != expected_pk_y)
        Error_Handler();

    return sk;
}

void init_globals() {
    g_privkey = Tele::get_sk_from_config();

    HAL_RNG_Init(&hrng);
    HAL_CRC_Init(&hcrc);

    CAN_FilterTypeDef filter = {
        .FilterIdHigh = 0,
        .FilterIdLow = 0,
        .FilterMaskIdHigh = 0,
        .FilterMaskIdLow = 0,
        .FilterFIFOAssignment = CAN_RX_FIFO0,
        .FilterBank = 0,
        .FilterMode = CAN_FILTERMODE_IDMASK,
        .FilterScale = CAN_FILTERSCALE_32BIT,
        .FilterActivation = ENABLE,
        .SlaveStartFilterBank = 14,
    };

    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK) {
        Error_Handler();
    }

    /*if (fifo) {
        filter.FilterFIFOAssignment = *fifo;
        if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK)
            Error_Handler();
    }*/

    if (HAL_CAN_Start(&hcan1) != HAL_OK) {
        Error_Handler();
    }

    /*if (fifo && HAL_CAN_ActivateNotification(&hcan1, *fifo) != HAL_OK) {
        Error_Handler();
    }*/
}

void run_benchmarks() { Tele::p256_test(g_privkey); }

void run_tests() { Tele::signature_benchmark(g_privkey); }

}