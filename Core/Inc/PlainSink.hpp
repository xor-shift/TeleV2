#pragma once

#include <Tele/Log.hpp>
#include <Tele/STUtilities.hpp>

#include <stream_buffer.h>

namespace Log {

constexpr std::string_view short_severity_name(Severity severity) {
    switch (severity) {
    case Severity::Off: return "XX";
    case Severity::Trace: return "TT";
    case Severity::Debug: return "DD";
    case Severity::Info: return "II";
    case Severity::Warning: return "WW";
    case Severity::Error: return "EE";
    default: return "??";
    }
}

constexpr std::string_view long_severity_name(Severity severity) {
    switch (severity) {
    case Severity::Off: return "Off";
    case Severity::Trace: return "Trace";
    case Severity::Debug: return "Debug";
    case Severity::Info: return "Info";
    case Severity::Warning: return "Warning";
    case Severity::Error: return "Error";
    default: return "Invalid";
    }
}

struct PlainSink : LogSink {
    virtual ~PlainSink() = default;

    void log(LogMessage const& message) override {
        using namespace std::chrono;
        system_clock::time_point tp = message.time;
        auto dp = floor<days>(tp);
        year_month_day ymd { dp };
        hh_mm_ss time { floor<milliseconds>(tp - dp) };

        std::string str = fmt::format(
          "[{:08X}] [{:02}{:02}{:02}] [{}]: {}\r\n", //
          message.thread_id,                            //
          time.hours().count(),                         //
          time.minutes().count(),                       //
          time.seconds().count(),                       //
          short_severity_name(message.severity),        //
          message.message
        );

        sink(str);
    }

protected:
    virtual void sink(std::string_view raw) = 0;
};

}
