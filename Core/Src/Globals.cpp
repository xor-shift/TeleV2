#include <Globals.hpp>

#include <atomic>

#include <Stuff/Maths/Check/CRC.hpp>

#include <queue.h>
#include <stream_buffer.h>
#include <usbd_cdc_if.h>

#include <GyroTask.hpp>
#include <benchmarks.hpp>
#include <util.hpp>

namespace Tele {

[[gnu::section(".ccmram")]] static volatile struct InterRebootData {
    uint32_t hcf_status;
    char hcf_task_name[configMAX_TASK_NAME_LEN];
    size_t hcf_task_name_sz;

    uint32_t crc_sum;

    uint32_t self_check() volatile {
        uint32_t old_sum = crc_sum;
        crc_sum = 0;

        using desc_type = Stf::CRCDescriptions::CRC32ISOHDLC;
        Stf::CRCState<desc_type, false> crc_state {};
        for (size_t i = 0; i < sizeof(InterRebootData); i++) {
            crc_state.update(reinterpret_cast<volatile uint8_t*>(this)[i]);
        }

        crc_sum = old_sum;
        return crc_state.finished_value();
    }

    /// @return
    /// true if there was an initialization.
    bool initialize_if_needed() volatile {
        if (self_check() == crc_sum)
            return false;

        hcf_status = HCF_DIDNT_CATCH_FIRE;
        crc_sum = self_check();

        return true;
    }

    void catched_fire(uint32_t status, const char* who) volatile {
        hcf_status = status;

        // we don't trust that is non-null
        who = who == nullptr ? "" : who;

        // we don't trust that `who` is null terminated either
        hcf_task_name_sz = 0;
        for (const char* p = who; *p && hcf_task_name_sz <= configMAX_TASK_NAME_LEN; p++, hcf_task_name_sz++) { }

        // actually it might be unwise to dirty the stack with a lot of calls
        /*std::fill(hcf_task_name, hcf_task_name + configMAX_TASK_NAME_LEN, '\0');
        std::copy_n(who, hcf_task_name_sz, hcf_task_name);*/

        for (size_t i = 0; i < hcf_task_name_sz; i++) {
            hcf_task_name[i] = who[i];
        }

        for (size_t i = hcf_task_name_sz; i < configMAX_TASK_NAME_LEN; i++) {
            hcf_task_name[i] = 0;
        }

        crc_sum = self_check();
    }
} s_inter_reboot_data;

P256::PrivateKey g_privkey;

template<typename Callback> struct LineBuffer {
    constexpr LineBuffer(Callback&& callback)
        : m_callback(std::move(callback)) { }

    constexpr void new_char(char c) {
        if (c >= ' ' && c <= '~') {
            if (m_buffer_utilization == size(m_buffer))
                return;

            m_buffer[m_buffer_utilization++] = c;
            return;
        }

        if (c == '\r' || c == '\n') {
            std::invoke(m_callback, cur_line());
            m_buffer_utilization = 0;
            return;
        }

        if (c == '\x7F') {
            if (m_buffer_utilization == 0)
                return;

            --m_buffer_utilization;
            return;
        }
    }

    constexpr std::string_view cur_line() { return { data(m_buffer), m_buffer_utilization }; }

private:
    Callback m_callback;

    std::array<char, 80> m_buffer;
    uint32_t m_buffer_utilization = 0;
};

struct TerminalTask : StaticTask<2048> {
    struct QueueElement {
        static constexpr size_t max_sz = 32;

        std::array<char, max_sz> data;
        size_t sz;
        bool incoming;

        constexpr std::string_view str() const { return { data.data(), sz }; }
    };

    static constexpr size_t k_queue_size = 32;

    virtual ~TerminalTask() override = default;

    void create(const char* name) override {
        m_queue_handle = xQueueCreateStatic(k_queue_size, sizeof(QueueElement), data(m_queue_storage), &m_static_queue);

        if (m_queue_handle == nullptr)
            Error_Handler();

        m_stream_buffer_handle = xStreamBufferCreateStatic(
          size(m_stream_buffer_storage), 1, data(m_stream_buffer_storage), &m_static_stream_buffer
        );

        if (m_stream_buffer_handle == nullptr)
            Error_Handler();

        StaticTask::create(name);
    }

    void isr_new_chars(std::string_view str) {
        while (!str.empty()) {
            QueueElement elem {
                .sz = std::min(str.size(), QueueElement::max_sz),
                .incoming = true,
            };

            std::copy_n(begin(str), elem.sz, elem.data.data());
            str = str.substr(elem.sz);

            BaseType_t higher_prio_task_awoken = pdFALSE;
            xQueueSendFromISR(m_queue_handle, &elem, &higher_prio_task_awoken);
            portYIELD_FROM_ISR(higher_prio_task_awoken);
        }
    }

    void send_str(std::string_view str) {
        std::string_view sv = str;
        while (!sv.empty()) {
            QueueElement elem {
                .sz = std::min(QueueElement::max_sz, sv.size()),
                .incoming = false,
            };
            std::copy_n(begin(sv), elem.sz, elem.data.data());
            sv = sv.substr(elem.sz);

            if (xQueueSend(m_queue_handle, &elem, /*portMAX_DELAY*/ 0) != pdTRUE) {
                break;
            }
        }
    }

protected:
    [[noreturn]] void operator()() override {
        LineBuffer line_buffer { [this](std::string_view line) { input_line_callback(line); } };

        auto push_num = [](auto& stream, size_t n) {
            if (n > 9999)
                n = 9999;

            std::array<char, 4> buf;
            auto res = std::to_chars(begin(buf), end(buf), n);
            if (res.ec != std::errc())
                return false;

            for (auto it = begin(buf); it != res.ptr; it++)
                stream << *it;

            return true;
        };

        auto set_cursor = [this, &push_num](size_t i, size_t j) {
            std::array<char, 4 + 2 * 4> buffer;
            auto res = fmt::format_to_n(data(buffer), buffer.size(), "\033[{};{}f", i, j);
            raw_send({ data(buffer), res.out });
        };

        auto save_cursor = [this] {
            // raw_send("\0337");
            raw_send("\033[s");
        };

        auto restore_cursor = [this] {
            // raw_send("\0338");
            raw_send("\033[u");
        };

        enum class EraseLineMode {
            CursorToEnd = 0,
            CursorToBeg = 1,
            WholeLine = 2,
        };

        auto erase_line = [this](EraseLineMode mode) {
            char buf[] = "\033[ K";
            buf[2] = static_cast<char>(mode) + '0';
            raw_send(buf);
        };

        // positive amount will scroll up as if \n was sent a few times
        auto scroll = [this, &push_num](int v) {
            bool up = v > 0;
            v = v < 0 ? -v : v;

            std::array<char, 3 + 4> buffer;
            BufCharStream stream { buffer };
            stream << "\033[";
            if (!push_num(stream, v))
                return;
            stream << (up ? 'S' : 'T');

            raw_send(stream);
        };

        auto display_prompt_top = [&] {
            save_cursor();
            set_cursor(0, 0);
            erase_line(EraseLineMode::WholeLine);
            raw_send(m_prompt);
            raw_send(line_buffer.cur_line());
        };

        auto clear_prompt_top = [&] {
            // no-op, since the prompt is at the top, the buffer can
            // just scroll over it.
            restore_cursor();
        };

        auto display_prompt_bottom = [&] {
            save_cursor();
            // scroll(1);
            // erase_line(EraseLineMode::WholeLine);
            raw_send("\r\n");
            raw_send(m_prompt);
            raw_send(line_buffer.cur_line());
        };

        auto clear_prompt_bottom = [&] {
            // scroll(-1);
            erase_line(EraseLineMode::WholeLine);
            restore_cursor();
        };

        auto& clear_prompt = clear_prompt_top;
        auto& display_prompt = display_prompt_top;

        for (;;) {
            QueueElement elem;
            if (xQueueReceive(m_queue_handle, &elem, portMAX_DELAY) != pdTRUE)
                Error_Handler();

            if (elem.incoming) {
                clear_prompt();
                for (char c : elem.str()) {
                    line_buffer.new_char(c);
                }
                display_prompt();
            } else {
                clear_prompt();
                raw_send(elem.str());
                display_prompt();
            }
        }
    }

private:
    std::array<uint8_t, k_queue_size * sizeof(QueueElement)> m_queue_storage;
    StaticQueue_t m_static_queue;
    QueueHandle_t m_queue_handle = nullptr;

    std::array<uint8_t, 32> m_stream_buffer_storage;
    StaticStreamBuffer_t m_static_stream_buffer;
    StreamBufferHandle_t m_stream_buffer_handle = nullptr;

    std::string_view m_prompt = "> ";

    void raw_send(std::string_view buffer) {
        /*for (char c : buffer) {
            CDC_Transmit_FS((uint8_t*)&c, 1);
            vTaskDelay(10);
        }*/

        while (CDC_Transmit_FS((uint8_t*)buffer.data(), buffer.size()) != USBD_OK)
            vTaskDelay(1);
    }

    // when this is called, the prompt line is empty
    void input_line_callback(std::string_view line) {
        send_str(m_prompt);
        send_str(line);
        send_str("\r\n");
        terminal_line_callback(line);
    }
};

struct DiagnosticWatchdogTask final : Tele::StaticTask<2048> {
    ~DiagnosticWatchdogTask() noexcept override = default;

    void create(const char* name) final override { StaticTask::create(name); }

protected:
    enum class WarningStatus : int {
        Clear = 0,
        Warned = 1,
        Errored = 2,
    };

    [[noreturn]] void operator()() override {
        ResetCause last_reset_cause = get_reset_cause();
        Log::Severity severity = Log::Severity::Info;

        Log::info("Watchdog", "this is a hack... the top-mode prompt destroys the first log entry");
        Log::info("Watchdog", "last reset cause was: {}", enum_name(last_reset_cause));

        if (!s_inter_reboot_data.initialize_if_needed()) {
            if (s_inter_reboot_data.hcf_status != HCF_DIDNT_CATCH_FIRE) {
                Log::warn("Watchdog", "the previous reset was a HCF ({})", s_inter_reboot_data.hcf_status);

                char name_buf[configMAX_TASK_NAME_LEN];
                std::copy_n(s_inter_reboot_data.hcf_task_name, configMAX_TASK_NAME_LEN, name_buf);

                Log::warn(
                  "Watchdog", "task name that threw the HCF or additional info, if any: {}",
                  std::string_view(name_buf, s_inter_reboot_data.hcf_task_name_sz)
                );
            }

            s_inter_reboot_data.catched_fire(HCF_DIDNT_CATCH_FIRE, "no fire");
        }

        for (;;) {
            vTaskDelay(333);

            if (HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_SET) {
                HAL_GPIO_WritePin(k_led_port, k_led_pin_orange, GPIO_PIN_RESET);
                HAL_GPIO_WritePin(k_led_port, k_led_pin_red, GPIO_PIN_RESET);
            }

            // signal that the watchdog is alive and well
            HAL_GPIO_TogglePin(k_led_port, k_led_pin_blue);

            HeapStats_t heap_stats;
            vPortGetHeapStats(&heap_stats);

            unsigned long rt;
            UBaseType_t num_tasks = uxTaskGetSystemState(data(m_task_status_buffer), uxTaskGetNumberOfTasks(), &rt);

            for (UBaseType_t i = 0; i < num_tasks; i++) {
                TaskStatus_t& task = m_task_status_buffer[i];
                watchdog_callback(task);
            }
        }
    }

private:
    std::array<TaskStatus_t, 24> m_task_status_buffer;
};

static GyroTask s_gyro_task { [](auto vec) { gyro_callback(vec); } };
static TerminalTask s_terminal_task {};
static DiagnosticWatchdogTask s_watchdog_task {};

void init_globals() {
    g_privkey = Tele::get_sk_from_config();

    HAL_RNG_Init(&hrng);
    HAL_CRC_Init(&hcrc);
}

void run_benchmarks() { Tele::p256_test(g_privkey); }

void run_tests() { Tele::signature_benchmark(g_privkey); }

void init_tasks() {
    s_watchdog_task.create("watchdog");
    s_terminal_task.create("terminal");
    s_gyro_task.create("gyro");
}

void isr_terminal_chars(std::string_view str) { s_terminal_task.isr_new_chars(str); }

void isr_gyro_interrupt() { s_gyro_task.notify_isr(); }

[[gnu::weak]] void gyro_callback(Stf::Vector<float, 3> vec) { }

[[gnu::weak]] void watchdog_callback(TaskStatus_t& task) {
    static std::array<uint16_t, 32> s_task_watermarks;
    static bool s_initialised = false;

    if (!s_initialised) {
        s_initialised = true;
        std::fill(begin(s_task_watermarks), end(s_task_watermarks), 0xFFFF);
    }

    if (task.xTaskNumber >= s_task_watermarks.size())
        return;

    uint16_t& old_watermark = s_task_watermarks[task.xTaskNumber];
    uint16_t watermark = task.usStackHighWaterMark;

    if (old_watermark <= watermark)
        return;

    old_watermark = watermark;

    if (watermark > 48)
        return;

    if (watermark >= 24) {
        Log::warn(
          "Watchdog", "Task#{} (\"{}\") is sussy! (watermark: {})", //
          task.xTaskNumber, task.pcTaskName, task.usStackHighWaterMark
        );
        return;
    }

    halt_and_catch_fire(HCF_WDT_STACK_OVERFLOW, task.pcTaskName);
}

[[gnu::weak]] void terminal_line_callback(std::string_view line) {
    Log::warn("Stub", "terminal_line_callback is stubbed");
}

}

namespace Log {

constexpr std::string_view short_severity_name(Severity severity) {
    switch (severity) {
    case Severity::Trace: return "TT";
    case Severity::Debug: return "DD";
    case Severity::Info: return "II";
    case Severity::Warning: return "WW";
    case Severity::Error: return "EE";
    }

    return "??";
}

constexpr std::string_view long_severity_name(Severity severity) {
    switch (severity) {
    case Severity::Trace: return "Trace";
    case Severity::Debug: return "Debug";
    case Severity::Info: return "Info";
    case Severity::Warning: return "Warning";
    case Severity::Error: return "Error";
    }

    return "Invalid";
}

static std::atomic<Severity> min_severity = Severity::Trace;

void set_min_severity(Severity severity) {
    min_severity = severity;
}

void raw(Severity severity, std::string_view tag, std::string&& message) {
    if (severity == Severity::Warning) {
        HAL_GPIO_WritePin(Tele::k_led_port, Tele::k_led_pin_orange, GPIO_PIN_SET);
    } else if (severity == Severity::Error) {
        HAL_GPIO_WritePin(Tele::k_led_port, Tele::k_led_pin_red, GPIO_PIN_SET);
    }

    if (severity < min_severity.load()) {
        return;
    }

    std::string_view severity_str = short_severity_name(severity);
    std::string tag_string = tag.empty() ? "" : fmt::format(" [{}]", tag);

    std::string line
      = fmt::format("[{:08X}] [{}]{}: {}\r\n", g_high_frequency_ticks, severity_str, tag_string, message);
    Tele::s_terminal_task.send_str(std::move(line));
}

}

extern "C" __attribute((noreturn)) void halt_and_catch_fire(uint32_t code, const char* who) {
#ifndef NDEBUG
    asm volatile("bkpt");
#endif

    __disable_irq();

    Tele::s_inter_reboot_data.catched_fire(code, who);

    /*if (from_task) {
        if (from_isr)
            taskENTER_CRITICAL_FROM_ISR();
        else
            taskENTER_CRITICAL();
    }*/

    NVIC_SystemReset();
}

extern "C" void vApplicationMallocFailedHook() { //
    halt_and_catch_fire(HCF_RTOS_MALLOC, "malloc failed");
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t task, signed char* task_name) {
    halt_and_catch_fire(HCF_RTOS_STACK_OVERFLOW, reinterpret_cast<const char*>(task_name));
}