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
}

namespace Tele {

static UART_HandleTypeDef& huart_gsm = huart2;
static UART_HandleTypeDef& huart_ftdi = huart3;

static GPIO_TypeDef* k_led_port = GPIOD;
static constexpr uint16_t k_led_pin_orange = LD3_Pin;
static constexpr uint16_t k_led_pin_green = LD4_Pin;
static constexpr uint16_t k_led_pin_red = LD5_Pin;
static constexpr uint16_t k_led_pin_blue = LD6_Pin;

extern P256::PrivateKey g_privkey;

void init_globals();

void run_benchmarks();
void run_tests();

void init_tasks();

void isr_terminal_chars(std::string_view str);
void isr_gyro_interrupt();

void gyro_callback(Stf::Vector<float, 3> vec);
void watchdog_callback(TaskStatus_t& task);
void terminal_line_callback(std::string_view line);

}

namespace Log {

enum class Severity : int {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
};

void raw(Severity severity, std::string_view tag, std::string&& message);

template<typename... Ts>
void log(Severity severity, std::string_view tag, fmt::format_string<Ts...> fmt_str, Ts&&... args) {
    return raw(severity, tag, fmt::format(fmt_str, std::forward<Ts>(args)...));
}

#define LOG_FACTORY(_name, _severity)                                                                          \
    template<typename... Ts> void _name(std::string_view tag, fmt::format_string<Ts...> fmt_str, Ts&&... args) { \
        return raw(_severity, tag, fmt::format(fmt_str, std::forward<Ts>(args)...));                             \
    }

LOG_FACTORY(trace, Severity::Trace);
LOG_FACTORY(debug, Severity::Debug);
LOG_FACTORY(info, Severity::Info);
LOG_FACTORY(warn, Severity::Warning);
LOG_FACTORY(error, Severity::Error);

}
