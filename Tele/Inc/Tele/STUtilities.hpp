#pragma once

#include <atomic>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

#include <cmsis_os.h>
#include <main.h>

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

    return executions - 1;
}

ResetCause get_reset_cause();

// call this overload in critical sections such as the watchdog instead of the std::vector overload
std::span<TaskStatus_t> get_tasks(std::span<TaskStatus_t> storage);

std::vector<TaskStatus_t> get_tasks();

struct Spinlock {
    /// @remarks
    /// This function is interrupt safe
    /// This function is thread safe
    bool try_lock() {
        bool expected = false;
        return m_lock.compare_exchange_weak(expected, true, std::memory_order_acquire);
    }

    /// @remarks
    /// This function must be called from a FreeRTOS thread.\n
    /// This function is not interrupt safe
    /// This function is thread safe
    void lock() {
        while (!try_lock()) {
            taskYIELD();
        }
    }

    void release() { m_lock.store(false, std::memory_order_release); }

private:
    std::atomic_bool m_lock { false };
};

inline bool debugger_attached() {
    return (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0;
}

[[gnu::always_inline]] inline void breakpoint() {
    if (debugger_attached())
        asm volatile("bkpt");
}

}
