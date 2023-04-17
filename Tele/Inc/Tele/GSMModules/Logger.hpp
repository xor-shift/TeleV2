#pragma once

#include <Tele/GSMCoordinator.hpp>

namespace Tele::GSM {

struct LoggerModule final : Module {
    virtual ~LoggerModule() override = default;

    void registered(Coordinator* coordinator) final override;

    void incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) final override;
};

}
