#include <stdcompat.hpp>

#include <atomic>
#include <charconv>
#include <chrono>

#include <fmt/chrono.h>

#include <cmsis_os.h>
#include <main.h>
#include <queue.h>

#include <Globals.hpp>
#include <MainGSMModule.hpp>
#include <PlainSink.hpp>
#include <Shell.hpp>
#include <Watchdog.hpp>

#include <Tele/GSMModules/Logger.hpp>
#include <Tele/GSMModules/Timer.hpp>
#include <Tele/GyroTask.hpp>
#include <Tele/Log.hpp>
#include <Tele/STUtilities.hpp>
#include <Tele/UARTTasks.hpp>

static void terminal_line_callback(std::string_view line);

struct ShellTask : Tele::TerminalTask {
    virtual ~ShellTask() = default;

protected:
    void new_line(std::string_view line) final override { terminal_line_callback(line); }
};

struct ShellSink : Log::PlainSink {
    ShellSink(ShellTask& shell)
        : m_shell(shell) { }

protected:
    void sink(std::string_view raw) override {
        m_shell.send_str(raw);

        /*Tele::in_chunks<const char>({ raw }, ShellTask::QueueElement::max_sz, [this](std::span<const char> chunk) {

            return chunk.size();
        });*/
    }

private:
    ShellTask& m_shell;
};

static Tele::DiagnosticWatchdogTask s_watchdog_task {};
static ShellTask s_shell_task {};
static Tele::GyroTask s_gyro_task { hspi1, CS_I2C_SPI_GPIO_Port, CS_I2C_SPI_Pin };

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
        s_gyro_task.isr_notify();
    }

    std::ignore = pin;
}

extern "C" void cpp_isr_cdc_receive(uint8_t* buf, uint32_t len) {
    char* ptr = reinterpret_cast<char*>(buf);
    s_shell_task.isr_new_chars({ ptr, ptr + len });
}

static int stack_abuser(int i) { // NOLINT(misc-no-recursion)
    if (i <= 1)
        return 1;

    return 1 + stack_abuser(i - 1) * 2;
}

static void terminal_line_callback(std::string_view line) {
    static std::array<TaskStatus_t, 24> s_task_status_buffer;

    if (line == "")
        return;

    if (line == "restart") {
        NVIC_SystemReset();
    } else if (line == "heap") {
        HeapStats_t heap_stats;
        vPortGetHeapStats(&heap_stats);

        Log::info("{} bytes free", heap_stats.xAvailableHeapSpaceInBytes);
        Log::info("{} malloc calls", heap_stats.xNumberOfSuccessfulAllocations);
        Log::info("{} free calls", heap_stats.xNumberOfSuccessfulFrees);
    } else if (line == "getticklf") {
        Log::info("HAL_GetTick(): {}", HAL_GetTick());
    } else if (line == "gettickhf") {
        Log::info("g_high_frequency_ticks: {}", g_high_frequency_ticks);
    } else if (line == "gettimeofday") {
        auto tp = std::chrono::system_clock::now();
        Log::info("Time: {}", tp);
    } else if (line == "tasks") {
        unsigned long rt;
        UBaseType_t num_tasks = uxTaskGetSystemState(data(s_task_status_buffer), uxTaskGetNumberOfTasks(), &rt);

        std::span<TaskStatus_t> tasks { data(s_task_status_buffer), num_tasks };
        sort(begin(tasks), end(tasks), [](auto& lhs, auto& rhs) { return lhs.xTaskNumber < rhs.xTaskNumber; });

        for (TaskStatus_t& task : tasks) {
            float norm_rt = task.ulRunTimeCounter / static_cast<float>(rt);
            Log::info(
              "Task#{} (\"{}\") spent {:.3f}% of the time. Watermark: {}", //
              task.xTaskNumber,                                            //
              task.pcTaskName,                                             //
              norm_rt * 100,                                               //
              task.usStackHighWaterMark
            );
        }
    } else if (line.starts_with("abuse_stack")) {
        int i;
        std::string_view args = line.substr(line.find(' ') + 1);
        auto res = std::from_chars(begin(args), end(args), i);
        if (res.ec != std::errc()) {
            Log::warn("bad argument");
            return;
        }

        Log::warn("result: {}", stack_abuser(i));
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
        Log::warn("unknown command");
    }
}

extern "C" void cpp_init() {
    Tele::init_globals();

    Log::g_logger.add_sink(std::make_unique<ShellSink>(std::ref(s_shell_task)));

    s_watchdog_task.create("watchdog");
    s_shell_task.create("shell");
    s_gsm_tx_task.create("gsm tx");
    s_gyro_task.create("gryo");

    s_gsm_coordinator.register_module(&s_gsm_module_timer);
    s_gsm_coordinator.register_module(&s_gsm_module_logger);
    s_gsm_coordinator.register_module(&s_gsm_module_main);

    s_gsm_coordinator.create("gsm coordinator");
    s_gsm_coordinator.begin_rx();

    s_gsm_module_timer.create("gsm timer");
    s_gsm_module_main.create("gsm main");

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
