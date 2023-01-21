#pragma once

#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include <cmsis_os.h>

namespace Tele {

template<typename T> [[gnu::always_inline]] inline void do_not_optimize(T&& value) {
#if defined(__clang__)
    asm volatile("" : "+r,m"(value) : : "memory");
#else
    asm volatile("" : "+m,r"(value) : : "memory");
#endif
}

enum class ResetCause {
    Unknown,
    LowPower,
    WindowWatchdog,
    IndependentWatchdog,
    // NVIC_SystemReset();
    Software,
    PowerOnPowerDown,
    ExternalResetPin,
    Brownout,
};

constexpr std::string_view enum_name(ResetCause cause) {
    switch (cause) {
    case ResetCause::LowPower: return "low power";
    case ResetCause::WindowWatchdog: return "window watchdog";
    case ResetCause::IndependentWatchdog: return "independent watchdog";
    case ResetCause::Software: return "software";
    case ResetCause::PowerOnPowerDown: return "poweron/poweroff";
    case ResetCause::ExternalResetPin: return "external reset pin";
    case ResetCause::Brownout: return "brownout";
    default: return "unknown";
    }
}

template<typename T, typename Fn> constexpr size_t in_chunks(std::span<T> span, size_t chunk_sz, Fn&& fn) {
    if (chunk_sz == 0)
        return 0;

    size_t executions = 0;

    while (++executions, !span.empty()) {
        size_t sz = std::min(chunk_sz, span.size());
        size_t real_sz = std::invoke(fn, std::span<T>(span.subspan(0, sz)));
        span = span.subspan(real_sz);
    }

    return executions;
}

ResetCause get_reset_cause();

// call this overload in critical sections such as the watchdog instead of the std::vector overload
std::span<TaskStatus_t> get_tasks(std::span<TaskStatus_t> storage);

std::vector<TaskStatus_t> get_tasks();

}
