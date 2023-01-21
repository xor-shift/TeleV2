#include <Tele/GSMModules/Timer.hpp>

namespace GSM {

[[noreturn]] void TimerModule::operator()() {
    for (;;) {
        vTaskDelay(500);

        if (!m_timer_cleared || m_coordinator == nullptr)
            continue;

        m_timer_cleared = false;
        m_coordinator->forge_reply(this, Reply::PeriodicMessage { .time = 500 });
    }
}

void TimerModule::incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) {
    if (std::holds_alternative<Reply::PeriodicMessage>(reply))
        m_timer_cleared = true;
}

}
