#pragma once

#include "cmsis_os.h"

namespace Tele {

struct StaticTask {
    virtual ~StaticTask() noexcept = default;

    void create(const char* name) {
        const size_t stack_size = sizeof(m_stack) / sizeof(m_stack[0]);
        const auto prio = tskIDLE_PRIORITY + 2;

        m_handle = xTaskCreateStatic(StaticTask::task, name, stack_size, this, prio, m_stack, &m_static_task);
    }

    [[gnu::cold]] static void task(void* arg) {
        auto& self = *reinterpret_cast<StaticTask*>(arg);
        self();
    }

    constexpr TaskHandle_t handle() const { return m_handle; }

protected:
    [[noreturn]] virtual void operator()() = 0;

private:
    TaskHandle_t m_handle;

    StackType_t m_stack[configMINIMAL_STACK_SIZE];
    StaticTask_t m_static_task;
};

}
