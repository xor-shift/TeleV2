#pragma once

#include <array>

#include <main.h>

#include <Tele/StaticTask.hpp>

namespace Tele {

struct InterRebootData {
    uint32_t hcf_status;
    char hcf_task_name[configMAX_TASK_NAME_LEN];
    size_t hcf_task_name_sz;

    uint32_t crc_sum;

    uint32_t self_check() volatile;

    /// @return
    /// true if there was an initialization.
    bool initialize_if_needed() volatile;

    void catched_fire(uint32_t status, const char* who) volatile;
};

extern volatile InterRebootData g_inter_reboot_data;

struct DiagnosticWatchdogTask : Tele::StaticTask<2048> {
    DiagnosticWatchdogTask();

    ~DiagnosticWatchdogTask() noexcept override = default;

    void create(const char* name) override { StaticTask::create(name); }

protected:
    [[noreturn]] void operator()() override;

    virtual void process_task_info(TaskStatus_t const& task);

private:
    std::array<TaskStatus_t, 24> m_task_status_buffer;
};

}
