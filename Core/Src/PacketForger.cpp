#include <PacketForger.hpp>

#include <queue.h>

#include <random>

namespace Tele {

PacketForgerTask::PacketForgerTask()
    : m_sequencer_mutex(xSemaphoreCreateMutex())
    , m_packet_queue(xQueueCreate(queue_size, sizeof(queue_item))) { }

PacketForgerTask::~PacketForgerTask() { vSemaphoreDelete(m_sequencer_mutex); }

void PacketForgerTask::create(const char* name) { StaticTask::create(name); }

void PacketForgerTask::reset_sequencer(std::span<uint32_t, 4> rng_iv) {
    if (xSemaphoreTake(m_sequencer_mutex, portMAX_DELAY) != pdTRUE) {
        throw std::runtime_error("xSemaphoreTake");
    }

    Stf::ScopeExit sema_guard { [this] { xSemaphoreGive(m_sequencer_mutex); } };

    m_sequencer_ready = true;
    m_sequencer.reset(rng_iv);
}

size_t PacketForgerTask::get_pending_packets(std::span<Packet> out) {
    Packet* begin = data(out);
    Packet* ptr = begin;
    Packet* end = data(out) + size(out);

    for (;;) {
        if (ptr == end)
            break;

        if (xQueueReceive(m_packet_queue, ptr, 0) != pdTRUE)
            break;

        ++ptr;
    }

    return std::distance(begin, ptr);
}

[[noreturn]] void PacketForgerTask::operator()() {
    for (;;) {
        TickType_t last_tick = xTaskGetTickCount();
        if (xSemaphoreTake(m_sequencer_mutex, portMAX_DELAY) != pdTRUE) {
            throw std::runtime_error("xSemaphoreTake");
        }

        Stf::ScopeExit sema_guard { [this] { xSemaphoreGive(m_sequencer_mutex); } };

        if (!m_sequencer_ready) {
            continue;
        }

        Packet packet = m_sequencer.sequence(produce_full_packet());

        xQueueSend(m_packet_queue, &packet, 0);

        vTaskDelayUntil(&last_tick, configTICK_RATE_HZ / queue_frequency);
    }
}

FullPacket PacketForgerTask::produce_full_packet() {
    HeapStats_t heap_stats;
    vPortGetHeapStats(&heap_stats);

    std::minstd_rand engine { std::random_device {}() };
    std::uniform_real_distribution<float> rand { 0.f, 1.f };

    FullPacket packet {
        .speed = rand(engine),
        .remaining_wh = rand(engine),

        .longitude = rand(engine),
        .latitude = rand(engine),

        .queue_fill_amt = uxQueueMessagesWaiting(m_packet_queue),
        .tick_counter = HAL_GetTick(),
        .free_heap_space = heap_stats.xAvailableHeapSpaceInBytes,
        .amt_allocs = heap_stats.xNumberOfSuccessfulAllocations,
        .amt_frees = heap_stats.xNumberOfSuccessfulFrees,
        .cpu_usage = 0.92f,
    };

    std::generate(std::begin(packet.battery_voltages), std::end(packet.battery_voltages), [&] { return rand(engine); });
    std::generate(std::begin(packet.battery_temps), std::end(packet.battery_temps), [&] { return rand(engine); });

    return packet;
}

}
