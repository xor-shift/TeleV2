#pragma once

#include <stdexcept>

#include <cmsis_os.h>

namespace Tele {

namespace Detail {

template<size_t StackSize, bool Static = true> struct TaskStorage;

template<size_t StackSize> struct TaskStorage<StackSize, true> {
    TaskHandle_t handle;

    StackType_t stack[StackSize];
    StaticTask_t static_task;

    TaskStorage() = default;

    TaskStorage(TaskStorage const&) = delete;
    TaskStorage(TaskStorage&&) = delete;

    void create(const char* name, void (*task)(void*), void* arg) {
        const auto prio = tskIDLE_PRIORITY + 2;

        handle = xTaskCreateStatic(task, name, StackSize, arg, prio, stack, &static_task);
    }
};

template<size_t StackSize> struct TaskStorage<StackSize, false> {
    TaskHandle_t handle;

    TaskStorage() = default;

    TaskStorage(TaskStorage const&) = delete;
    TaskStorage(TaskStorage&&) = delete;

    void create(const char* name, void (*task)(void*), void* arg) {
        const auto prio = tskIDLE_PRIORITY + 2;

        BaseType_t res = xTaskCreate(task, name, StackSize, arg, prio, &handle);
        if (!res) {
            throw std::runtime_error("failed to create task");
        }
    }
};

}

template<size_t StackSize, bool Static = true> struct Task {
    virtual ~Task() noexcept = default;

    virtual void create(const char* name) {
        if (m_created) {
            throw std::runtime_error("double create");
        }
        m_created = true;
        m_task_storage.create(name, task, this);
    }

    [[gnu::cold]] static void task(void* arg) {
        auto& self = *reinterpret_cast<Task*>(arg);
        self();
    }

    constexpr TaskHandle_t handle() const { return m_task_storage.handle; }

protected:
    [[noreturn]] virtual void operator()() = 0;

private:
    bool m_created = false;
    Detail::TaskStorage<StackSize, Static> m_task_storage;
};

template<size_t StackSize> using StaticTask = Task<StackSize, true>;
template<size_t StackSize> using DynamicTask = Task<StackSize, false>;

}
