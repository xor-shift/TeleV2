#include <GSM.hpp>

#include <stdcompat.hpp>

#include <atomic>
#include <charconv>
#include <chrono>

#include <date/date.h>

#include <CircularBuffer.hpp>
#include <Globals.hpp>
#include <UARTTasks.hpp>
#include <benchmarks.hpp>
#include <cmsis_os.h>
#include <main.h>
#include <queue.h>
#include <util.hpp>

/*static ReceiveTask s_gsm_rx_task {
    huart2,
    [](std::string_view line) {
        Log::info("GSM", "Received: {}", Tele::EscapedString { line });
        Log::trace("GSM", "Test: {}", GSM::Command::CFUN { GSM::Command::CFUNType::DisableTxRxCircuits, true });
        Log::trace("GSM", "Test: {}", GSM::Command::CFUN {});
    },
};*/

static TransmitTask s_gsm_tx_task { huart2 };

static GSM::TimerModule s_gsm_module_timer {};
static GSM::LoggerModule s_gsm_module_logger {};
static GSM::MainModule s_gsm_module_main {};
static GSM::Coordinator s_gsm_coordinator { huart2, s_gsm_tx_task };

extern "C" void libtele_trace_task_switched_in() { std::ignore = 0; }

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) { }

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t offset) {
    static uint16_t last_offset = 0;

    if (last_offset == offset)
        return;

    if (last_offset > offset) {
        last_offset = 0;
    }

    if (huart == &huart2) {
        s_gsm_coordinator.isr_rx_event(last_offset, offset);
    } else if (huart == &huart3) {
        // s_gsm_rx_task.isr_rx_event(last_offset, offset);
    }

    last_offset = offset;
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart) {
    if (huart == &huart2) {
        HAL_UART_DMAStop(huart);
        s_gsm_coordinator.begin_rx();
    }
}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t pin) {
    if (pin == 1) {
        Tele::isr_gyro_interrupt();
        // s_gyro_task.notify_isr();
    }

    std::ignore = pin;
}

extern "C" void cpp_assert_failed(const char* file, uint32_t line) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
        return;

    Log::error("Global", "assertion failed at {}:{}", file, line);
}

extern "C" void cpp_isr_cdc_receive(uint8_t* buf, uint32_t len) {
    char* ptr = reinterpret_cast<char*>(buf);
    Tele::isr_terminal_chars({ ptr, ptr + len });
}

static int stack_abuser(int i) { // NOLINT(misc-no-recursion)
    if (i <= 1)
        return 1;

    return 1 + stack_abuser(i - 1) * 2;
}

namespace Tele {

void terminal_line_callback(std::string_view line) {
    static std::array<TaskStatus_t, 24> s_task_status_buffer;

    if (line == "")
        return;

    if (line == "restart") {
        NVIC_SystemReset();
    } else if (line == "heap") {
        HeapStats_t heap_stats;
        vPortGetHeapStats(&heap_stats);

        Log::info("Terminal", "{} bytes free", heap_stats.xAvailableHeapSpaceInBytes);
        Log::info("Terminal", "{} malloc calls", heap_stats.xNumberOfSuccessfulAllocations);
        Log::info("Terminal", "{} free calls", heap_stats.xNumberOfSuccessfulFrees);
    } else if (line == "getticklf") {
        Log::info("Terminal", "HAL_GetTick(): {}", HAL_GetTick());
    } else if (line == "gettickhf") {
        Log::info("Terminal", "g_high_frequency_ticks: {}", g_high_frequency_ticks);
    } else if (line == "gettimeofday") {
        auto tp = std::chrono::system_clock::now();
        date::sys_seconds sys_secs { std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()) };

        std::string str { 128, ' ' };
        std::stringstream ss { str };
        date::to_stream(ss, "%Y/%m/%d %H:%M:%S", sys_secs);

        Log::info("Terminal", "Time: {}", ss.str());
    } else if (line == "tasks") {
        unsigned long rt;
        UBaseType_t num_tasks = uxTaskGetSystemState(data(s_task_status_buffer), uxTaskGetNumberOfTasks(), &rt);

        std::span<TaskStatus_t> tasks { data(s_task_status_buffer), num_tasks };
        sort(begin(tasks), end(tasks), [](auto& lhs, auto& rhs) { return lhs.xTaskNumber < rhs.xTaskNumber; });

        for (TaskStatus_t& task : tasks) {
            float norm_rt = task.ulRunTimeCounter / static_cast<float>(rt);
            Log::info(
              "Terminal", "Task#{} (\"{}\") spent {:.3f}% of the time. Watermark: {}", //
              task.xTaskNumber,                                                        //
              task.pcTaskName,                                                         //
              norm_rt * 100,                                                           //
              task.usStackHighWaterMark
            );
        }
    } else if (line.starts_with("abuse_stack")) {
        int i;
        std::string_view args = line.substr(line.find(' ') + 1);
        auto res = std::from_chars(begin(args), end(args), i);
        if (res.ec != std::errc()) {
            Log::warn("Terminal", "bad argument");
            return;
        }

        Log::warn("Terminal", "result: {}", stack_abuser(i));
    } else if (line == "clear_warning") {
        HAL_GPIO_WritePin(k_led_port, k_led_pin_orange, GPIO_PIN_RESET);
    } else if (line.starts_with("gsm_tx")) {
        std::string_view args = line.substr(line.find(' ') + 1);

        Tele::in_chunks<const char>(args, 8, [](std::span<const char> chunk) {
            size_t res = xStreamBufferSend(s_gsm_tx_task.stream(), chunk.data(), chunk.size(), portMAX_DELAY);
            taskYIELD();
            return res;
        });

        Tele::in_chunks<const char>(std::string_view("\n"), 8, [](std::span<const char> chunk) {
            size_t res = xStreamBufferSend(s_gsm_tx_task.stream(), chunk.data(), chunk.size(), portMAX_DELAY);
            taskYIELD();
            return res;
        });

    } else {
        Log::warn("Terminal", "unknown command");
    }
}

void gyro_callback(Stf::Vector<float, 3> vec) {
    vec = Stf::normalized(vec);

    // Log::trace("Gyro", "Gyro raw: {}, {}, {}", vec[0], vec[1], vec[2]);
}

}

/*
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
*/

extern "C" void cpp_init() {
    Tele::test_parse_ip();

    Tele::init_globals();
    Tele::init_tasks();

    s_gsm_tx_task.create("gsm tx");

    s_gsm_coordinator.register_module(&s_gsm_module_timer);
    s_gsm_coordinator.register_module(&s_gsm_module_logger);
    s_gsm_coordinator.register_module(&s_gsm_module_main);

    s_gsm_coordinator.create("gsm coordinator");
    s_gsm_coordinator.begin_rx();

    s_gsm_module_timer.create("gsm timer");

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
}

extern "C" void cpp_os_exit() {
    HAL_CRC_DeInit(&hcrc);
    HAL_RNG_DeInit(&hrng);
}
