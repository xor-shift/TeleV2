#include <Tele/CANTask.hpp>

#include <cstring>

#include <Stuff/Maths/Bit.hpp>

#include <Tele/Log.hpp>
#include <secrets.hpp>

namespace Tele {

void CANTask::process_can_rx(uint16_t id, std::span<uint8_t> data) {
    /*std::string can_text = fmt::format("Received {} bytes from {:04X}: ", data.size(), id);
    for (uint8_t b : data) {
        fmt::format_to(std::back_inserter(can_text), "{:02X} ", b);
    }
    Log::debug("{}", can_text);*/

    // std::array<float, 8> temp;
    float temp_f;

    auto read_battery_voltages = [data](std::span<float> out) {
        for (size_t i = 0; float& f : out) {
            f = Stf::map<float>(data[i++], 0, 255, 2.4, 4.3);
        }
    };

    switch (id) {
    case static_cast<uint16_t>(CANIDs::EngineRPMTemp):
        if (data.size() != 8)
            break;

        std::memcpy(&temp_f, data.data(), sizeof(float));
        std::memcpy(&m_engine_temperature, data.data() + sizeof(float), sizeof(float));

        static constexpr float diameter_mm = 288.f;
        static constexpr float circumference_mm = diameter_mm * 2 * std::numbers::pi_v<float>;

        m_data_collector.set<float>("engine_rpm", temp_f);
        m_data_collector.set<float>("engine_speed", (circumference_mm * temp_f) * 60.f / (1000.f * 1000.f));

        break;

        // clang-format off
    /*case static_cast<uint16_t>(CANIDs::BMS1): if (data.size() != 8) break; read_battery_voltages({ temp.data(), 8 }); m_data_collector.set_array<float>("can_battery_voltage", {temp.data(), 8}, 0); break;
    case static_cast<uint16_t>(CANIDs::BMS2): if (data.size() != 8) break; read_battery_voltages({ temp.data(), 8 }); m_data_collector.set_array<float>("can_battery_voltage", {temp.data(), 8}, 8); break;
    case static_cast<uint16_t>(CANIDs::BMS3): if (data.size() != 8) break; read_battery_voltages({ temp.data(), 8 }); m_data_collector.set_array<float>("can_battery_voltage", {temp.data(), 8}, 16); break;
    case static_cast<uint16_t>(CANIDs::BMS4): if (data.size() != 8) break; read_battery_voltages({ temp.data(), 3 }); m_data_collector.set_array<float>("can_battery_voltage", {temp.data(), 3}, 24);

        for (size_t i = 0; auto& f : temp) {
            f = Stf::map<float>(data[i++ + 3], 0, 255, 0, 100);
        }
        m_data_collector.set_array<float>("can_battery_temp", std::span { temp.data(), 5 });
        break;*/
        // clang-format on

    case static_cast<uint16_t>(CANIDs::BMS1):
    case static_cast<uint16_t>(CANIDs::BMS2):
    case static_cast<uint16_t>(CANIDs::BMS3):
    case static_cast<uint16_t>(CANIDs::BMS4): {
        std::array<float, 8> voltages_buffer {};
        read_battery_voltages(voltages_buffer);

        size_t bms_idx = id - static_cast<uint16_t>(CANIDs::BMS1);
        size_t stride = bms_idx * 8;

#if RACE_MODE == RACE_MODE_ELECTRO
        static constexpr size_t cell_count = 27;
#elif RACE_MODE == RACE_MODE_HYDRO
        static constexpr size_t cell_count = 20;
#endif

        size_t cur_excess = (stride + 8 < cell_count) ? 0 : std::min(8uz, stride + 8 - cell_count);
        for (size_t i = 0; i < cur_excess; i++) {
            voltages_buffer[7 - i] = 0.f;
        }

        m_data_collector.set_array<float>("can_battery_voltage", voltages_buffer, stride);

        if (id == static_cast<uint16_t>(CANIDs::BMS4)) {
            std::array<float, 5> temperatures {};
            for (size_t i = 0; auto& f : temperatures) {
                f = Stf::map<float>(data[i++ + 3], 0, 255, 0, 100);
            }
            m_data_collector.set_array<float>("can_battery_temp", temperatures);
        }
    }

    case static_cast<uint16_t>(CANIDs::BMS5): {
        uint16_t raw_mah = Stf::convert_endian(*reinterpret_cast<const uint16_t*>(data.data()), std::endian::big);
        uint16_t raw_mwh = Stf::convert_endian(*reinterpret_cast<const uint16_t*>(data.data() + 2), std::endian::big);
        uint16_t raw_current
          = Stf::convert_endian(*reinterpret_cast<const uint16_t*>(data.data() + 4), std::endian::big);
        uint16_t raw_percent
          = Stf::convert_endian(*reinterpret_cast<const uint16_t*>(data.data() + 6), std::endian::big);
        m_data_collector.set<float>("can_spent_mah", Stf::map<float>(raw_mah, 0, 65535, 0, 15000));
        m_data_collector.set<float>("can_spent_mwh", Stf::map<float>(raw_mwh, 0, 65535, 0, 15000 * 256));
        m_data_collector.set<float>("can_current", Stf::map<float>(raw_current, 0, 65535, -10, 50));
        m_data_collector.set<float>("can_soc_percent", Stf::map<float>(raw_percent, 0, 65535, -5, 105));
    } break;

    case static_cast<uint16_t>(CANIDs::Hydrogen): {
        m_data_collector.set<float>("can_hydro_ppm", data[0]);
        m_data_collector.set<float>("can_hydro_temp", data[1]);
    }

    default: break;
    }
}

}
