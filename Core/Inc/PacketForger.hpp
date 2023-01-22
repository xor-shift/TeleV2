#pragma once

#include <list>

#include <cmsis_os.h>
#include <semphr.h>

#include <Stuff/Maths/Scalar.hpp>
#include <Stuff/Util/Scope.hpp>

#include <Packets.hpp>
#include <Tele/StaticTask.hpp>

namespace Tele {

struct PacketForgerTask : Tele::StaticTask<2048> {
    inline static constexpr float queue_frequency = 1.5f;
    inline static constexpr float queue_delay_seconds = 1 / queue_frequency;
    inline static constexpr float queue_length_seconds = 20;
    inline static constexpr size_t queue_size = static_cast<size_t>(std::ceil(queue_length_seconds * queue_frequency));
    inline static constexpr float effective_queue_length_seconds = queue_size * queue_delay_seconds;

    using queue_item = Packet;

    PacketForgerTask();

    ~PacketForgerTask();

    void create(const char* name) final override;

    void reset_sequencer(std::span<uint32_t, 4> rng_iv);

    size_t get_pending_packets(std::span<Packet> out);

protected:
    [[noreturn]] void operator()() override;

private:
    bool m_sequencer_ready = false;
    SemaphoreHandle_t m_sequencer_mutex = nullptr;
    PacketSequencer m_sequencer {};

    QueueHandle_t m_packet_queue = nullptr;

    FullPacket produce_full_packet();
};

}
