#pragma once

#include <list>

#include <cmsis_os.h>
#include <semphr.h>

#include <Stuff/Maths/Scalar.hpp>
#include <Stuff/Util/Scope.hpp>

#include <Packets.hpp>
#include <Tele/CANTask.hpp>
#include <Tele/DataCollector.hpp>
#include <Tele/GPSTask.hpp>
#include <Tele/StaticTask.hpp>

namespace Tele {

struct PacketForgerTask : Tele::StaticTask<2048> {
    inline static constexpr size_t queue_size = 100;
    using queue_item = Packet;

    PacketForgerTask(DataCollectorTask& data_collector);

    ~PacketForgerTask();

    void reset_sequencer(std::span<uint32_t, 4> rng_iv);

    size_t get_pending_packets(std::span<Packet> out);

protected:
    [[noreturn]] void operator()() override;

private:
    DataCollectorTask& m_data_collector;

    std::atomic_bool m_sequencer_ready = false;
    SemaphoreHandle_t m_sequencer_mutex = nullptr;
    PacketSequencer m_sequencer {};

    std::array<uint8_t, queue_size * sizeof(queue_item)> m_static_queue_storage;
    StaticQueue_t m_static_queue;
    QueueHandle_t m_packet_queue = nullptr;

    FullPacket produce_full_packet();
};

}
