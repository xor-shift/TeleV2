#pragma once

#include <atomic>
#include <chrono>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <cmsis_os.h>

namespace Log {

enum class Severity : int {
    Off = -1,
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
};

struct LogMessage {
    Severity severity = Severity::Off;
    std::chrono::system_clock::time_point time = std::chrono::system_clock::from_time_t(0);
    size_t thread_id = 0;

    std::source_location source_location;
    std::string_view message;
};

struct LogSink {
    virtual ~LogSink() = default;

    virtual void set_severity(Severity severity) { m_severity = severity; }

    virtual bool should_log(LogMessage const& message) const { return m_severity.load() <= message.severity; }

    virtual void log(LogMessage const& message) = 0;

private:
    std::atomic<Severity> m_severity { Severity::Trace };
};

struct Logger {
    void add_sink(std::unique_ptr<LogSink> ptr) { m_sinks.emplace_back(std::move(ptr)); }

    template<typename... Ts>
    void log(std::source_location location, Severity severity, fmt::format_string<Ts...> fmt, Ts&&... args) {
        return log_impl(location, severity, fmt, std::forward<Ts>(args)...);
    }

    template<typename... Ts> void log(Severity severity, fmt::format_string<Ts...> fmt, Ts&&... args) {
        return log_impl({}, severity, fmt, std::forward<Ts>(args)...);
    }

    template<typename T>
        requires(!requires() { typename std::enable_if_t<std::convertible_to<T, fmt::format_string<>>, void>; })
    void log(std::source_location location, Severity severity, T const& v) {
        return log_impl(location, severity, "{}", v);
    }

    template<typename T>
        requires(!requires() { typename std::enable_if_t<std::convertible_to<T, fmt::format_string<>>, void>; })
    void log(Severity severity, T const& v) {
        return log_impl({}, severity, "{}", v);
    }

#pragma push_macro("FACTORY")
#define FACTORY(_name, _level)                                                         \
    template<typename... Ts> void _name(fmt::format_string<Ts...> fmt, Ts&&... args) { \
        return log(_level, fmt, std::forward<Ts>(args)...);                            \
    }

    FACTORY(trace, Severity::Trace);
    FACTORY(debug, Severity::Debug);
    FACTORY(info, Severity::Info);
    FACTORY(warn, Severity::Warning);
    FACTORY(error, Severity::Error);

#undef FACTORY
#pragma pop_macro("FACTORY")

    void set_severity(Severity severity) { m_severity = severity; }

    bool should_log(LogMessage const& message) const { return m_severity.load() <= message.severity; }

private:
    std::vector<std::unique_ptr<LogSink>> m_sinks;
    std::atomic<Severity> m_severity { Severity::Trace };

    template<typename... Ts>
    void log_impl(std::source_location location, Severity severity, fmt::format_string<Ts...> fmt, Ts&&... args) {
        if (m_severity.load() > severity) {
            return;
        }

        std::string formatted = fmt::format(fmt, std::forward<Ts>(args)...);

        /*size_t thread_id = ({
            TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
            current_task == nullptr ? 0 : uxTaskGetTaskNumber(current_task);
        });*/
        size_t thread_id = reinterpret_cast<std::uintptr_t>(xTaskGetCurrentTaskHandle());

        LogMessage message {
            .severity = severity,
            .time = std::chrono::system_clock::now(),
            .thread_id = thread_id,
            .source_location = location,
            .message = formatted,
        };

        for (auto& sink : m_sinks) {
            if (sink->should_log(message))
                sink->log(message);
        }
    }
};

extern Logger g_logger;

#pragma push_macro("FACTORY")
#define FACTORY(_name)                                                                 \
    template<typename... Ts> void _name(fmt::format_string<Ts...> fmt, Ts&&... args) { \
        return g_logger._name(fmt, std::forward<Ts>(args)...);                         \
    }

FACTORY(trace);
FACTORY(debug);
FACTORY(info);
FACTORY(warn);
FACTORY(error);

#undef FACTORY
#pragma pop_macro("FACTORY")

}
