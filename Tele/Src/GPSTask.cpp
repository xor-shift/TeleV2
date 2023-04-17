#include <Tele/GPSTask.hpp>

#include <Tele/Log.hpp>
#include <Tele/NMEA.hpp>

#include <Stuff/Util/Scope.hpp>
#include <Stuff/Util/Visitor.hpp>

namespace Tele {

GPSTask::GPSTask(DataCollectorTask& data_collector, UART_HandleTypeDef& handle)
    : m_data_collector(data_collector)
    , m_handle(handle)
    , m_uart_task("GPS", handle, 256, "\r\n") { }

void GPSTask::operator()() {
    for (std::string* line_ptr = nullptr;;) {
        if (line_ptr = m_uart_task.receive_line(); line_ptr == nullptr) {
            std::unreachable();
        }

        Stf::ScopeExit guard { [&line_ptr] {
            delete line_ptr;
            line_ptr = nullptr;
        } };

        std::string& line = *line_ptr;

        auto parse_res = Tele::NMEA::parse_line(line);
        if (!parse_res) {
            auto& msg = parse_res.error();

            if (msg != "WIP")
                Log::warn("GPS parsing failed: {}", parse_res.error());
            continue;
        }

        NMEA::message_type message = *parse_res;

        Stf::MultiVisitor visitor {
            [this](NMEA::GGAMessage const& message) {
                m_data_collector.set<float>("gps_latitude", message.latitude);
                m_data_collector.set<float>("gps_longitude", message.longitude);
                Log::debug("GPS @ {}, {}", message.latitude, message.longitude);
            },
            [](auto const&) { Log::debug("received unprocessed GPS message"); },
        };

        std::visit(visitor, message);
    }
}

}
