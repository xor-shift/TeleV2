#include "stdcompat.hpp"

#include <Globals.hpp>

#include "cmsis_os.h"
#include "main.h"

#include <CircularBuffer.hpp>

/*static TransmitTask s_gsm_tx_task { huart_gsm };

static ReceiveTask s_gsm_rx_task {
    huart2,
    [](uint8_t v) { //
        //xStreamBufferSend(s_ftdi_tx_task.stream(), &v, 1, portMAX_DELAY);
    },
};

struct TerminalTask : Tele::TerminalBaseTask {
    ~TerminalTask() noexcept override = default;

protected:
    void line_received(std::string_view line) override {
        if (line == "restart") {
            NVIC_SystemReset();
        } else if (line == "heap") {
            HeapStats_t heap_stats;
            vPortGetHeapStats(&heap_stats);

            log_fmt(Tele::Severity::Info, "Terminal", "{} bytes free", heap_stats.xAvailableHeapSpaceInBytes);
            log_fmt(Tele::Severity::Info, "Terminal", "{} malloc calls", heap_stats.xNumberOfSuccessfulAllocations);
            log_fmt(Tele::Severity::Info, "Terminal", "{} free calls", heap_stats.xNumberOfSuccessfulFrees);
        } else if (line == "getticklf") {
            log_fmt(Tele::Severity::Info, "Terminal", "HAL_GetTick(): {}", HAL_GetTick());
        } else if (line == "gettickhf") {
            log_fmt(Tele::Severity::Info, "Terminal", "g_high_frequency_ticks: {}", g_high_frequency_ticks);
        } else if (line == "tasks") {
            unsigned long rt;
            UBaseType_t num_tasks = uxTaskGetSystemState(data(m_task_status_buffer), uxTaskGetNumberOfTasks(), &rt);

            std::span<TaskStatus_t> tasks { data(m_task_status_buffer), num_tasks };
            sort(begin(tasks), end(tasks), [](auto& lhs, auto& rhs) { return lhs.xTaskNumber < rhs.xTaskNumber; });

            for (TaskStatus_t& task : tasks) {
                float norm_rt = task.ulRunTimeCounter / static_cast<float>(rt);
                log_fmt(
                  Tele::Severity::Info, "Terminal", "Task#{} (\"{}\") spent {:.3f}% of the time. Watermark: {}", //
                  task.xTaskNumber,                                                                              //
                  task.pcTaskName,                                                                               //
                  norm_rt * 100,                                                                                 //
                  task.usStackHighWaterMark
                );
            }
        } else if (line.starts_with("abuse_stack")) {
            int i;
            std::string_view args = line.substr(line.find(' ') + 1);
            auto res = std::from_chars(begin(args), end(args), i);
            if (res.ec != std::errc()) {
                log_str(Tele::Severity::Warning, "Terminal", "bad argument");
                return;
            }

            log_fmt(Tele::Severity::Warning, "Terminal", "result: {}", stack_abuser(i));
        } else {
            log_str(Tele::Severity::Warning, "Terminal", "Unknown command");
        }
    }

private:
    std::array<TaskStatus_t, 24> m_task_status_buffer;

    static int stack_abuser(int i) {
        if (i <= 1)
            return 1;

        return 1 + stack_abuser(i - 1) * 2;
    }
};

static TerminalTask s_logger_task {};

static struct DiagnosticWatchdogTask : Tele::StaticTask<2048> {
    constexpr DiagnosticWatchdogTask() {
        std::fill(begin(m_task_watermarks), end(m_task_watermarks), 0xFFFF);
    }

    ~DiagnosticWatchdogTask() noexcept override = default;

protected:
    enum class WarningStatus : int {
        Clear = 0,
        Warned = 1,
        Errored = 2,
    };

    [[noreturn]] void operator()() override {
        for (;;) {
            vTaskDelay(500);

            HeapStats_t heap_stats;
            vPortGetHeapStats(&heap_stats);

            unsigned long rt;
            UBaseType_t num_tasks = uxTaskGetSystemState(data(m_task_status_buffer), uxTaskGetNumberOfTasks(), &rt);

            for (UBaseType_t i = 0; i < num_tasks; i++) {
                TaskStatus_t& task = m_task_status_buffer[i];
                update_task_susness(task);
            }
        }
    }

private:
    std::array<TaskStatus_t, 24> m_task_status_buffer;
    std::array<uint16_t, 24> m_task_watermarks;

    void update_task_susness(TaskStatus_t& task) {
        if (task.xTaskNumber >= m_task_watermarks.size())
            return;

        uint16_t& old_watermark = m_task_watermarks[task.xTaskNumber];
        uint16_t watermark = task.usStackHighWaterMark;

        if (old_watermark <= watermark)
            return;

        old_watermark = watermark;

        if (watermark > 64)
            return;

        if (watermark >= 32) {
            s_logger_task.log_fmt(
              Tele::Severity::Warning, "Watchdog", "Task#{} (\"{}\") is sussy! (watermark: {})", //
              task.xTaskNumber, task.pcTaskName, task.usStackHighWaterMark
            );
            return;
        }

        taskENTER_CRITICAL(); // don't context switch

#ifndef NDEBUG
        asm volatile ("bkpt");
#endif

        NVIC_SystemReset();
    }
} s_diagnostic_task {};*/

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) { }

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t offset) {
    static uint16_t last_offset = 0;

    if (last_offset == offset)
        return;

    if (last_offset > offset) {
        last_offset = 0;
    }

    if (huart == &huart2) {
        // s_gsm_rx_task.isr_rx_event(last_offset, offset);
    } else if (huart == &huart3) {
        // s_ftdi_rx_task.isr_rx_event(last_offset, offset);
    }

    last_offset = offset;
}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t pin) {
    if (pin == 1) {
        Tele::isr_gyro_interrupt();
        //s_gyro_task.notify_isr();
    }

    std::ignore = pin;
}

extern "C" void cpp_assert_failed(const char* file, uint32_t line) { std::ignore = file; }

extern "C" void cpp_isr_cdc_receive(uint8_t* buf, uint32_t len) {
    char* ptr = reinterpret_cast<char*>(buf);
    Tele::isr_terminal_chars({ ptr, ptr + len });
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
    Tele::init_globals();
    Tele::init_tasks();
    //
    //

    //s_logger_task.create("logger task");
    //s_diagnostic_task.create("diagnostic task");
    //s_gyro_task.create("gyro task");

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