#include "stdcompat.hpp"

#include "cmsis_os.h"
#include "main.h"
#include "stream_buffer.h"

#include <cstring>
#include <random>

#include <CircularBuffer.hpp>
#include <Fixed.hpp>
#include <GyroTask.hpp>
#include <LIS3DSH.hpp>
#include <Packets.hpp>
#include <StaticTask.hpp>
#include <UARTTasks.hpp>

#include <Stuff/Maths/Fmt.hpp>

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

static UART_HandleTypeDef& huart_gsm = huart2;
static UART_HandleTypeDef& huart_ftdi = huart3;
static bool init_finished = false;

extern P256::PrivateKey g_privkey;
P256::PrivateKey g_privkey;

static TransmitTask s_gsm_tx_task { huart_gsm };
static TransmitTask s_ftdi_tx_task { huart_ftdi };

static ReceiveTask s_gsm_rx_task {
    huart2,
    [](uint8_t v) { //
        xStreamBufferSend(s_ftdi_tx_task.stream(), &v, 1, portMAX_DELAY);
    },
};

static ReceiveTask s_ftdi_rx_task {
    huart3,
    [](uint8_t v) { //
        xStreamBufferSend(s_ftdi_tx_task.stream(), &v, 1, portMAX_DELAY);
        // xStreamBufferSend(s_gsm_tx_task.stream(), &v, 1, portMAX_DELAY);
    },
};

static Tele::GyroTask s_gyro_task { [](Stf::Vector<uint16_t, 3> raw_reading) {
    auto str = fmt::format("{}\r\n", raw_reading);
    xStreamBufferSend(s_ftdi_tx_task.stream(), str.c_str(), str.size(), portMAX_DELAY);
} };

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) { }

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t offset) {
    static uint16_t last_offset = 0;

    if (last_offset == offset)
        return;

    if (last_offset > offset) {
        last_offset = 0;
    }

    if (huart == &huart2) {
        s_gsm_rx_task.isr_rx_event(last_offset, offset);
    } else if (huart == &huart3) {
        s_ftdi_rx_task.isr_rx_event(last_offset, offset);
    }

    last_offset = offset;
}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t pin) {
    if (pin == 1) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(s_gyro_task.handle(), &xHigherPriorityTaskWoken);

        if (xHigherPriorityTaskWoken == pdTRUE)
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

    std::ignore = pin;
}

extern "C" void cpp_assert_failed(const char* file, uint32_t line) {
    std::ignore = file;
}

static void float_thing() {
    Tele::Fixed<uint16_t, -14> fixed_point;
    fixed_point = 3.1415926f;
    float v0 = fixed_point;
    fixed_point = 3;
    float v1 = fixed_point;

    Tele::RangeFloat<uint16_t, 0.f, 15.f> range_float;
    range_float = 3.1415926f;
    float v2 = range_float;

    std::ignore = 0;
}

extern "C" void cpp_init() {
    g_privkey = Tele::get_sk_from_config();

    HAL_RNG_Init(&hrng);
    HAL_CRC_Init(&hcrc);

    // Tele::p256_test(s_privkey);
    // Tele::signature_benchmark(s_privkey);

    Tele::start_led_tasks();

    /*struct xHeapStats heap_stats;
    vPortGetHeapStats(&heap_stats);

    Tele::PacketSequencer sequencer;

    std::string buf;
    Tele::PushBackStream stream { buf };
    Stf::Serde::JSON::Serializer<Tele::PushBackStream<std::string>> serializer { stream };

    Tele::EssentialsPacket inner_packet {
        .speed = 3.1415926,
        .bat_temp_readings { 2, 3, 4, 5, 6 },
        .voltage = 2.718,
        .remaining_wh = 1.618,
    };

    auto packet = sequencer.transmit(inner_packet);

    Stf::serialize(serializer, packet);*/

    s_gsm_tx_task.create();
    s_ftdi_tx_task.create();

    s_gsm_rx_task.create_buffer();
    s_ftdi_rx_task.create_buffer();

    s_gsm_rx_task.create_task();
    s_ftdi_rx_task.create_task();

    s_gsm_rx_task.begin_rx();
    s_ftdi_rx_task.begin_rx();

    s_gyro_task.create("gyro task");

    xStreamBufferSend(s_ftdi_tx_task.stream(), "asdasd\r\n", 8, 1000);

    init_finished = true;
}

extern "C" void cpp_os_exit() {
    HAL_CRC_DeInit(&hcrc);
    HAL_RNG_DeInit(&hrng);
}