#include "util.hpp"

#include <random>
#include <utility>

#include "cmsis_os.h"
#include "main.h"

#include <StaticTask.hpp>

#include "secrets.hpp"
#include "util.hpp"

namespace Tele {

ResetCause get_reset_cause() {
#define CAUSE_FACTORY(_flg, _res) else if (__HAL_RCC_GET_FLAG(_flg) != 0u) cause = _res
    ResetCause cause = ResetCause::Unknown;

    if (false) {} // NOLINT(readability-simplify-boolean-expr)
    CAUSE_FACTORY(RCC_FLAG_LPWRRST, ResetCause::LowPower);
    CAUSE_FACTORY(RCC_FLAG_WWDGRST, ResetCause::WindowWatchdog);
    CAUSE_FACTORY(RCC_FLAG_IWDGRST, ResetCause::IndependentWatchdog);
    CAUSE_FACTORY(RCC_FLAG_SFTRST, ResetCause::Software);
    CAUSE_FACTORY(RCC_FLAG_PORRST, ResetCause::PowerOnPowerDown);
    CAUSE_FACTORY(RCC_FLAG_PINRST, ResetCause::ExternalResetPin);
    CAUSE_FACTORY(RCC_FLAG_BORRST, ResetCause::Brownout);

    return cause;
}

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

struct BlinkTask : StaticTask<configMINIMAL_STACK_SIZE> {
    constexpr BlinkTask(GPIO_TypeDef* port, uint16_t pin) noexcept
        : m_port(port)
        , m_pin(pin) { }

protected:
    [[noreturn]] void operator()() override {
        while (osDelay(1000), true)
            HAL_GPIO_TogglePin(m_port, m_pin);
    }

private:
    GPIO_TypeDef* m_port;
    uint16_t m_pin;
};

[[gnu::section(".ccmram")]] static BlinkTask blink_tasks[4] {
    { LD3_GPIO_Port, LD3_Pin },
    { LD4_GPIO_Port, LD4_Pin },
    { LD5_GPIO_Port, LD5_Pin },
    { LD6_GPIO_Port, LD6_Pin },
};

void start_led_tasks() {
    for (auto& task : blink_tasks) {
        task.create("blink task");
    }
}

}
