#include "util.hpp"

#include <random>
#include <utility>

#include "main.h"
#include "cmsis_os.h"

#include "secrets.hpp"
#include "util.hpp"

namespace Tele {

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

template<size_t N> [[noreturn]] void led_task(void*) {
    static const std::array<std::pair<GPIO_TypeDef*, uint16_t>, 4> leds { {
      { LD3_GPIO_Port, LD3_Pin },
      { LD4_GPIO_Port, LD4_Pin },
      { LD5_GPIO_Port, LD5_Pin },
      { LD6_GPIO_Port, LD6_Pin },
    } };

    while (osDelay(1000), true)
        HAL_GPIO_TogglePin(leds[N].first, leds[N].second);

    std::unreachable();
}

static osThreadId_t s_led_task_handles[4];

void start_led_tasks() {
    static const osThreadAttr_t led_task_handle_attributes = {
        .name = "defaultTask",
        .stack_size = 32,
        .priority = (osPriority_t)osPriorityNormal,
    };

    s_led_task_handles[0] = osThreadNew(led_task<0>, NULL, &led_task_handle_attributes);
    s_led_task_handles[1] = osThreadNew(led_task<1>, NULL, &led_task_handle_attributes);
    s_led_task_handles[2] = osThreadNew(led_task<2>, NULL, &led_task_handle_attributes);
    s_led_task_handles[3] = osThreadNew(led_task<3>, NULL, &led_task_handle_attributes);
}

}
