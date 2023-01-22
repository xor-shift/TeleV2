#pragma once

#include <Tele/GSMCoordinator.hpp>

namespace GSM {

struct TimerModule
    : Module
    , Tele::StaticTask<1024> {
    // REMINDER TO SELF: this one needs a lot of stack space because of forge_reply works.
    // when we forge a reply, all modules get executed by the forger itself.

    virtual ~TimerModule() override = default;

    void incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) final override;

protected:
    [[noreturn]] void operator()() final override;

private:
    std::atomic_bool m_timer_cleared = true;
};

}
