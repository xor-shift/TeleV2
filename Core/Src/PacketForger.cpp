#include <PacketForger.hpp>

#include <queue.h>

#include <random>

namespace Tele {

PacketForgerTask::PacketForgerTask(DataCollectorTask& data_collector)
    : m_data_collector(data_collector)
    , m_sequencer_mutex(xSemaphoreCreateMutex())
    // , m_packet_queue(xQueueCreate(queue_size, sizeof(queue_item)))
    , m_packet_queue(xQueueCreateStatic(queue_size, sizeof(queue_item), data(m_static_queue_storage), &m_static_queue)
      ) { }

PacketForgerTask::~PacketForgerTask() { vSemaphoreDelete(m_sequencer_mutex); }

void PacketForgerTask::reset_sequencer(std::span<uint32_t, 4> rng_iv) {
    /*if (xSemaphoreTake(m_sequencer_mutex, portMAX_DELAY) != pdTRUE) {
        throw std::runtime_error("xSemaphoreTake");
    }

    Stf::ScopeExit sema_guard { [this] { xSemaphoreGive(m_sequencer_mutex); } };*/

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
        /*if (xSemaphoreTake(m_sequencer_mutex, portMAX_DELAY) != pdTRUE) {
            throw std::runtime_error("xSemaphoreTake");
        }

        Stf::ScopeExit sema_guard { [this] { xSemaphoreGive(m_sequencer_mutex); } };*/

        if (!m_sequencer_ready) {
            vTaskDelay(1);
            continue;
        }

        TickType_t last_tick = xTaskGetTickCount();
        Packet packet = m_sequencer.sequence(produce_full_packet());

        xQueueSend(m_packet_queue, &packet, 0);

        static auto smooth_step = [](float t) {
            if (t <= 0.f)
                return 0.f;

            if (t >= 1.f)
                return 1.f;

            return t * t * (3 - 2 * t);
        };

        const float min_delay = 2.f / 3.f;
        const float max_delay = 5.f;
        const float fill_ratio = static_cast<float>(uxQueueMessagesWaiting(m_packet_queue)) / queue_size;
        const float smooth_step_start = 0.2f;
        const float smooth_step_end = 0.5f;

        const float t = Stf::inv_lerp(fill_ratio, smooth_step_start, smooth_step_end);
        const float ss_res = smooth_step(t);
        const float delay_secs = (1.f - ss_res) * min_delay + ss_res * max_delay;

        /*
         * Calculate queue size in seconds:
         *
         * let ss=t=>t<=0?0:t>=1?1:t*t*(3-2*t);
         * let il=(v,a,b)=>a==b?0:(v-a)/(b-a);
         * let d=(fill,start,end,min,max)=>{ const t=il(fill,start,end), ss_res=ss(t); return (1-ss_res)*min +
         * ss_res*max }
         *
         * let ql=(qsz,start,end,min,max)=>{ let ret=0; for(let i=0;i<qsz;i++) ret+=d(i/qsz,start,end,min,max); return
         * ret; }
         *
         * call `ql` with `queue_size`, `smooth_step_start`, `smooth_step_end`, `min_delay`, `max_delay`
         * the result is the duration this function can run without anyone emptying the queue
         * the instant the resultant time passes, there will be an overflow
         */

        vTaskDelayUntil(&last_tick, static_cast<UBaseType_t>(delay_secs * configTICK_RATE_HZ));
        // vTaskDelay(667);
    }
}

FullPacket PacketForgerTask::produce_full_packet() {
    HeapStats_t heap_stats;
    vPortGetHeapStats(&heap_stats);

    std::minstd_rand engine { std::random_device {}() };
    std::uniform_real_distribution<float> rand { 0.f, 1.f };

    float engine_rpm = m_data_collector.get<float>("engine_rpm");
    const float diameter_mm = 288.f;

    const float circumference = diameter_mm * 2 * std::numbers::pi_v<float>;
    const float mm_per_minute = circumference * engine_rpm;
    const float km_per_hour = mm_per_minute * 60.f / (1000.f * 1000.f);



    FullPacket packet {
        .spent_mah = m_data_collector.get<float>("can_spent_mah"),
        .spent_mwh = m_data_collector.get<float>("can_spent_mwh"),
        .current = m_data_collector.get<float>("can_current"),
        .soc_percent = m_data_collector.get<float>("can_soc_percent"),

        .hydro_current = 0.f,
        .hydro_ppm = m_data_collector.get<float>("can_hydro_ppm", 3.1415926f),
        .hydro_temp = m_data_collector.get<float>("can_hydro_temp", 3.1415926f),

        .rpm = m_data_collector.get<float>("engine_rpm"),
        .speed = m_data_collector.get<float>("engine_speed"),

        .longitude = m_data_collector.get<float>("gps_longitude"),
        .latitude = m_data_collector.get<float>("gps_latitude"),

        .queue_fill_amt = uxQueueMessagesWaiting(m_packet_queue),
        .tick_counter = m_data_collector.get<uint32_t>("hal_lf_ticks"),
        .free_heap_space = m_data_collector.get<uint32_t>("rtos_heap_free"),
        .amt_allocs = m_data_collector.get<uint32_t>("rtos_heap_allocations"),
        .amt_frees = m_data_collector.get<uint32_t>("rtos_heap_deallocations"),
        .cpu_usage = 3.1415926f,
    };

    auto battery_voltages = m_data_collector.get_array<float, 27>("can_battery_voltage");
    auto battery_temperatures = m_data_collector.get_array<float, 5>("can_battery_temp");

    std::copy(begin(battery_voltages), end(battery_voltages), std::begin(packet.battery_voltages));
    std::copy(begin(battery_temperatures), end(battery_temperatures), std::begin(packet.battery_temps));

    // std::generate(std::begin(packet.battery_voltages), std::end(packet.battery_voltages), [&] { return rand(engine); });
    // std::generate(std::begin(packet.battery_temps), std::end(packet.battery_temps), [&] { return rand(engine); });

    return packet;
}

}
