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
#include <secrets.hpp>
#include <Shell.hpp>
#include <Watchdog.hpp>

#include <Tele/CANTask.hpp>
#include <Tele/DataCollector.hpp>
#include <Tele/GPSTask.hpp>
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

struct NextionTask : Tele::Task<1024, true> {
    NextionTask(UART_HandleTypeDef& huart, Tele::DataCollectorTask& data_collector)
        : m_uart_task(huart)
        , m_data_collector(data_collector) { }

    void create(const char* name) override {
        m_uart_task.create("nextion uart");
        return Task::create(name);
    }

protected:
    [[noreturn]] void operator()() override {
        std::array<char, 96> fmt_buffer {};

        for (int i = 0;; i++) {
            vTaskDelay(200);

            std::array<float, 5> battery_temperatures;

#if RACE_MODE == RACE_MODE_ELECTRO
            static constexpr size_t cell_count = 27;
#elif RACE_MODE == RACE_MODE_HYDRO
            static constexpr size_t cell_count = 20;
#endif

            std::array<float, cell_count> battery_voltages;

            m_data_collector.get_array<float>("can_battery_temp", { battery_temperatures });
            m_data_collector.get_array<float>("can_battery_voltage", { battery_voltages });

            float spent_mah = m_data_collector.get<float>("can_spent_mah", 5);
            float spent_mwh = m_data_collector.get<float>("can_spent_mwh", 6);

            float current = m_data_collector.get<float>("can_current", 7);
            // float current = m_data_collector.get<float>("engine_speed", 7);

            float soc_percent = m_data_collector.get<float>("can_soc_percent", 8);
            // float soc_percent = m_data_collector.get<float>("engine_rpm", 8);

            const auto voltage_sum = std::reduce(battery_voltages.cbegin(), battery_voltages.cend());
            const auto [voltage_min, voltage_max]
              = std::minmax_element(battery_voltages.cbegin(), battery_voltages.cend());
            const auto voltage_avg = voltage_sum / battery_voltages.size();

            const auto temperature_sum = std::reduce(battery_temperatures.cbegin(), battery_temperatures.cend());
            const auto [temperature_min, temperature_max]
              = std::minmax_element(battery_temperatures.cbegin(), battery_temperatures.cend());
            const auto temperature_avg = voltage_sum / battery_temperatures.size();

            m_uart_task.transmit(produce_assignment_expression({ fmt_buffer }, "cell_init", battery_voltages[0], 2));
            for (int i = 0; i < 26; i++) {
                char str[] = "cell_ ";
                str[5] = 'A' + i;
                m_uart_task.transmit(produce_assignment_expression({ fmt_buffer }, str, battery_voltages[i + 1], 2));
            }

            for (int i = 0; i < 5; i++) {
                char str[] = "temp_batt_ ";
                str[10] = '0' + i;
                m_uart_task.transmit(produce_assignment_expression({ fmt_buffer }, str, battery_temperatures[i], 1));
            }

            // clang-format off

            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "speed_kmh", m_data_collector.get<float>("engine_speed", i), 0));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "rpm_engine", m_data_collector.get<float>("engine_rpm"), 0));

            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "volt_smps", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "curr_smps", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "volt_engine", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "curr_engine", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "volt_engine_driver", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "curr_engine_driver", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "volt_bms", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "curr_bms", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "volt_telemetry", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "curr_telemetry", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "temp_engine_drv", 0.f, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "temp_smps", 0.f, 1));

            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "hydro_ppm", m_data_collector.get<float>("can_hydro_ppm"), 2));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "hydro_temp", m_data_collector.get<float>("can_hydro_temp"), 2));

            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "cell_min", *voltage_min, 2));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "cell_max", *voltage_max, 2));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "cell_avg", voltage_avg, 2));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "cell_sum", voltage_sum, 2));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "temp_batt_max", *temperature_max, 1));

            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "current", current, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "spent_wh", spent_mwh / 1000.f, 1));
            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "spent_ah", spent_mah / 1000.f, 1));
            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "spent_mwh", spent_mwh, 1));
            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "spent_mah", spent_mah, 1));
            m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "soc_percent", soc_percent, 1));
            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "soc_normal", soc_percent / 100.f, 1));

            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "loop_count", 100.f, 1));
            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "lap_count", 100.f, 1));
            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "lap_avg_time_left", "00:00:00"));
            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "eta_charge", "00:00:00"));

            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "performance_1s", 1));
            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "performance_5s", 5));
            // m_uart_task.transmit(produce_assignment_expression({fmt_buffer}, "performance_15s", 15));

            // clang-format on
        }
    }

    template<typename T, size_t Extent = std::dynamic_extent>
    static std::string_view
    produce_assignment_expression(std::span<char, Extent> buffer, std::string_view name, T const& v) {
        std::string_view field;

        if constexpr (std::is_same_v<T, char*> || std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
            field = "txt";
        } else {
            field = "val";
        }

        auto it = fmt::format_to_n(buffer.begin(), buffer.size(), "{}.{}={}\xFF\xFF\xFF", name, field, v).out;

        return { buffer.begin(), it };
    }

    template<std::floating_point T, size_t Extent = std::dynamic_extent>
    static std::string_view
    produce_assignment_expression(std::span<char, Extent> buffer, std::string_view name, T const& v, int digits) {
        long scaled = std::lround(v * std::pow(10, digits));
        return produce_assignment_expression<long, Extent>(buffer, name, scaled);
    }

private:
    Tele::TransmitTask m_uart_task;
    Tele::DataCollectorTask& m_data_collector;
};

static Tele::DataCollectorTask s_data_collector {};

static Tele::DiagnosticWatchdogTask s_watchdog_task {};
static ShellTask s_shell_task {};
// static Tele::GyroTask s_gyro_task { hspi1, CS_I2C_SPI_GPIO_Port, CS_I2C_SPI_Pin };

static Tele::GPSTask s_gps_task { s_data_collector, Tele::s_gps_uart };
static Tele::CANTask s_can_task { s_data_collector, hcan1 };
static Tele::PacketForgerTask s_packet_forger_task { s_data_collector };

static Tele::TransmitTask s_gsm_transmit_task { Tele::s_gsm_uart };
static Tele::GSM::TimerModule s_gsm_module_timer {};
static Tele::GSM::LoggerModule s_gsm_module_logger {};
static Tele::GSM::MainModule s_gsm_module_main { s_packet_forger_task };
static Tele::GSM::Coordinator s_gsm_coordinator { Tele::s_gsm_uart, s_gsm_transmit_task };

static NextionTask s_nextion_task { Tele::s_nextion_uart, s_data_collector };

extern "C" void libtele_trace_task_switched_in() { std::ignore = 0; }

extern "C" void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart) { }

extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t offset) {
    s_gsm_coordinator.isr_rx_event(huart, offset);
    s_gps_task.isr_rx_event(huart, offset);
}

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart) {
    HAL_UART_DMAStop(huart);

    if (huart == &Tele::s_gsm_uart) {
        s_gsm_coordinator.begin_rx();
    } else if (huart == &Tele::s_gps_uart) {
        s_gps_task.begin_rx();
    }
}

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t pin) {
    if (pin == 1) {
        s_gsm_module_main.isr_gyro_notify();
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
    } else {
        Log::warn("unknown command");
    }
}

extern "C" void cpp_init() {
    Tele::init_globals();

    Log::g_logger.add_sink(std::make_unique<ShellSink>(std::ref(s_shell_task)));

    s_watchdog_task.create("watchdog");
    s_shell_task.create("shell");

    s_gps_task.create("gps");
    s_gps_task.begin_rx();
    s_can_task.create("can");
    s_packet_forger_task.create("packet forger");

    s_gsm_coordinator.register_module(&s_gsm_module_timer);
    s_gsm_coordinator.register_module(&s_gsm_module_logger);
    s_gsm_coordinator.register_module(&s_gsm_module_main);

    s_gsm_coordinator.create("gsm coordinator");
    s_gsm_coordinator.begin_rx();

    s_gsm_transmit_task.create("gsm tx");

    s_gsm_module_timer.create("gsm timer");
    s_gsm_module_main.create("gsm main");

    s_nextion_task.create("nextion task");
}

extern "C" void cpp_os_exit() {
    HAL_CRC_DeInit(&hcrc);
    HAL_RNG_DeInit(&hrng);
}
