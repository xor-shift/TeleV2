#include <Tele/GSMModules/Logger.hpp>

#include <Stuff/Util/Visitor.hpp>

#include <Tele/Log.hpp>

namespace GSM {

void LoggerModule::registered(GSM::Coordinator* coordinator) {
    Module::registered(coordinator);

    //Log::debug("Registered to the coordinator at {}", static_cast<void*>(coordinator));
}

void LoggerModule::incoming_reply(Coordinator&, Reply::reply_type const& reply) {
    Stf::MultiVisitor visitor {
        [](Reply::PeriodicMessage const&) { /*Log::trace("Received a periodic message");*/ },
        [](auto const& reply) { Log::debug("Received a \"{}\" reply", reply.name); }
    };

    std::visit(visitor, reply);
}

}
