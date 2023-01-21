#pragma once

#include <Tele/GSMCoordinator.hpp>

namespace GSM {

struct MainModule
    : Module
    , Tele::StaticTask<4096> {
    virtual ~MainModule() override = default;

    void incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) final override;

protected:
    [[noreturn]] void operator()() final override;

private:
    volatile bool m_ready = false;
    volatile bool m_functional = false;
    volatile bool m_have_sim = false;
    volatile bool m_call_ready = false;
    volatile bool m_sms_ready = false;
    volatile bool m_bearer_open = false;
    volatile bool m_gprs_open = false;

    std::optional<Reply::HTTPResponseReady> m_last_http_response {};

    void reset_state();

    bool initialize(std::span<uint32_t, 4> out_rng_vector);
    int main();

    std::optional<std::vector<Reply::reply_type>> http_request(
      std::string_view url, HTTPRequestType method, std::string_view content_type = "", std::string_view content = ""
    );

    std::optional<Reply::HTTPResponseReady> wait_for_http(BaseType_t timeout_decisecs = 150);
};

}
