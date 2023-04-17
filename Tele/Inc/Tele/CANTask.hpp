#pragma once

#include <atomic>

#include <cmsis_os.h>
#include <semphr.h>

#include <Tele/DataCollector.hpp>
#include <Tele/LIS3DSH.hpp>
#include <Tele/STUtilities.hpp>
#include <Tele/StaticTask.hpp>

namespace Tele {

enum class CANIDs : uint16_t {
    TelemetryHeartbeat = 0x00,
    TelemetryEngineControl = 0x01,
    VCSHeartbeat = 0x20,
    VCSTemperatures = 0x21,
    VCSEngine = 0x22,
    VCSTelemetry = 0x23,
    VCSSMPS = 0x24,
    VCSBMS = 0x25,
    VCSACStatus = 0x26,
    EngineHeartbeat = 0x40,
    EngineRPMTemp = 0x41,
    BMSHeartbeat = 0x60,
    BMS1 = 0x61,
    BMS2 = 0x62,
    BMS3 = 0x63,
    BMS4 = 0x64,
    BMS5 = 0x65,
    IsolationHeartbeat = 0x80,
    IsolationFirst = 0x81,
    IsolationSecond = 0x82,
    HydrogenHeartbeat = 0x90,
    Hydrogen = 0x91,
};

struct CANTask : StaticTask<1024> {
    CANTask(DataCollectorTask& data_collector, CAN_HandleTypeDef& handle)
        : m_data_collector(data_collector)
        , m_handle(handle) { }

    virtual ~CANTask() = default;

protected:
    [[noreturn]] void operator()() final override {
        for (;;) {
            for (uint32_t fill_level = HAL_CAN_GetRxFifoFillLevel(&m_handle, CAN_RX_FIFO0); fill_level != 0;
                 fill_level--) {
                // tick_leds();

                CAN_RxHeaderTypeDef header;
                std::array<uint8_t, 8> data { 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8 };
                HAL_CAN_GetRxMessage(&m_handle, CAN_RX_FIFO0, &header, data.data());

                if (header.IDE == CAN_ID_STD) {
                    process_can_rx(header.StdId, std::span(data.data(), header.DLC));
                }
            }
            portYIELD();
        }
    }

private:
    DataCollectorTask& m_data_collector;
    CAN_HandleTypeDef& m_handle;

    // mutable std::atomic_bool m_spinlock { false };
    // mutable Spinlock m_spinlock {};

    float m_engine_rpm;
    float m_engine_temperature;

    void process_can_rx(uint16_t id, std::span<uint8_t> data);
};

}
