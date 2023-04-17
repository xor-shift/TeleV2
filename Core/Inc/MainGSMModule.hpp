#pragma once

#include <memory>

#include <Tele/GPSTask.hpp>
#include <Tele/GSMCoordinator.hpp>
#include <Tele/GyroTask.hpp>
#include <Packets.hpp>
#include <PacketForger.hpp>

namespace Tele::GSM {

struct CustomGyroTask;

struct MainModule
    : Module
    , Task<4096, true> {
    friend struct CustomGyroTask;

    MainModule(Tele::PacketForgerTask& packet_forger);

    virtual ~MainModule() override = default;

    void isr_gyro_notify();

    void create(const char* name) final override;

    void incoming_reply(Coordinator& coordinator, Reply::reply_type const& reply) final override;

protected:
    [[noreturn]] void operator()() final override;

    void isr_gyro_new(Stf::Vector<uint16_t, 3> raw);

private:
    Tele::PacketForgerTask& m_packet_forger;

    std::unique_ptr<Tele::GyroTask> m_gyro_task;
    std::optional<Reply::HTTPResponseReady> m_last_http_response {};

    std::atomic_bool m_gyro_guard;
    Stf::Vector<uint16_t, 3> m_gyro_data;

    bool initialize_device();

    bool initialize_session(std::span<uint32_t, 4> out_rng_vector);
    int packet_loop();
    int main();

    std::optional<std::vector<Reply::reply_type>> http_request(
      std::string_view url, HTTPRequestType method, std::string_view content_type = "", std::string_view content = ""
    );

    std::optional<Reply::HTTPResponseReady> wait_for_http(BaseType_t timeout_decisecs = 600 * 3);
};

}
