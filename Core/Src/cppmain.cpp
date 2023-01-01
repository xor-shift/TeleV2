#include "stdcompat.hpp"

#include "cmsis_os.h"
#include "main.h"
#include "stream_buffer.h"

#include <CircularBuffer.hpp>
#include <Stuff/Maths/Hash/Sha2.hpp>
#include <UARTTasks.hpp>

#include "benchmarks.hpp"
#include "secrets.hpp"
#include "util.hpp"

extern "C" {
extern CRC_HandleTypeDef hcrc;
extern I2C_HandleTypeDef hi2c1;
extern I2S_HandleTypeDef hi2s3;
extern RNG_HandleTypeDef hrng;
extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
}

static P256::PrivateKey s_privkey;

static TransmitTask s_gsm_tx_task { huart2 };
static TransmitTask s_ftdi_tx_task { huart3 };

static ReceiveTask s_gsm_rx_task {
    huart2,
    [](uint8_t v) { //
        xStreamBufferSend(s_ftdi_tx_task.stream(), &v, 1, portMAX_DELAY);
    },
};

static ReceiveTask s_ftdi_rx_task {
    huart3,
    [](uint8_t v) { //
        xStreamBufferSend(s_gsm_tx_task.stream(), &v, 1, portMAX_DELAY);
    },
};

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) { }

extern "C" void HAL_UART_RxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart == &huart2) {
        s_gsm_rx_task.isr_rx_cplt();
        s_gsm_rx_task.begin_rx();
    } else if (huart == &huart3) {
        s_ftdi_rx_task.isr_rx_cplt();
        s_ftdi_rx_task.begin_rx();
    }
}

extern "C" void cpp_init() {
    auto& gsm_uart = huart2;
    auto& ftdi_uart = huart3;

    s_gsm_tx_task.create();
    s_ftdi_tx_task.create();

    s_gsm_rx_task.create_buffer();
    s_ftdi_rx_task.create_buffer();

    s_gsm_rx_task.create_task();
    s_ftdi_rx_task.create_task();

    s_gsm_rx_task.begin_rx();
    s_ftdi_rx_task.begin_rx();

    // std::array<uint8_t, 1024> rx_buf { 0 };

    HAL_UART_Transmit(&huart2, (uint8_t*)"AT+CFUN=1,1\r\n", 13, HAL_MAX_DELAY);
    // HAL_UART_Receive(&gsm_uart, data(rx_buf), 6, 2500);
    //__asm__ volatile("BKPT 0");
    // std::fill(begin(rx_buf), end(rx_buf), 0);

    /*HAL_UART_Transmit(&huart2, (uint8_t*)"ATE0\r\n", 6, HAL_MAX_DELAY);
    HAL_UART_Receive(&gsm_uart, data(rx_buf), rx_buf.size(), 1000);
    __asm__ volatile("BKPT 0");
    std::fill(begin(rx_buf), end(rx_buf), 0);

    HAL_UART_Transmit(&huart2, (uint8_t*)"AT\n", 3, HAL_MAX_DELAY);
    HAL_UART_Receive(&gsm_uart, data(rx_buf), rx_buf.size(), 1000);
    __asm__ volatile("BKPT 0");
    std::fill(begin(rx_buf), end(rx_buf), 0);*/

    HAL_RNG_Init(&hrng);
    HAL_CRC_Init(&hcrc);

    Tele::start_led_tasks();
    s_privkey = Tele::get_sk_from_config();

    // Tele::p256_test(s_privkey);
    // Tele::signature_benchmark(s_privkey);
}

extern "C" void cpp_os_exit() {
    HAL_CRC_DeInit(&hcrc);
    HAL_RNG_DeInit(&hrng);
}