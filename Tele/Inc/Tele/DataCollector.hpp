#pragma once

#include <algorithm>
#include <span>
#include <unordered_map>

#include <fmt/format.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include <Tele/StaticTask.hpp>

namespace Tele {

struct DataCollectorTask {
    DataCollectorTask()
        : m_mutex(xSemaphoreCreateMutex()) { }

    ~DataCollectorTask() { }

    template<std::floating_point T = float> void set(std::string_view key, T v) {
        lock();
        m_float_map[key] = static_cast<double>(v);
        unlock();
    }

    template<std::integral T = int64_t> void set(std::string_view key, T v) {
        lock();
        m_integral_map[key] = static_cast<int64_t>(v);
        unlock();
    }

    template<typename T>
    void set_array(std::string_view key_base, std::span<const T> vs, size_t offset = 0) {
        for (size_t i = 0; auto v : vs) {
            auto key = fmt::format("{}_{}", key_base, offset + i++);
            set<T>(key, v);
        }
    }

    template<std::floating_point T = float> T get(std::string_view key, T def = T(0)) const {
        if (auto it = std::find_if(
              std::begin(m_special_floats), std::end(m_special_floats), [key](auto v) { return v.first == key; }
            );
            it != std::end(m_special_floats)) {
            return static_cast<T>((it->second)());
        }

        lock();
        float ret = def;
        if (auto it = m_float_map.find(key); it != m_float_map.end()) {
            ret = it->second;
        }
        unlock();
        return ret;
    }

    template<std::integral T = int64_t> T get(std::string_view key, T def = T(0)) const {
        if (auto it = std::find_if(
              std::begin(m_special_integrals), std::end(m_special_integrals), [key](auto v) { return v.first == key; }
            );
            it != std::end(m_special_integrals)) {
            return static_cast<T>((it->second)());
        }

        lock();
        uint64_t ret = def;
        if (auto it = m_integral_map.find(key); it == m_integral_map.end()) {
            ret = it->second;
        }
        unlock();
        return ret;
    }

    template<typename T>
    void get_array(std::string_view key_base, std::span<T> vs) {
        for (size_t i = 0; auto& v : vs) {
            auto key = fmt::format("{}_{}", key_base, i++);
            v = get<T>(key);
        }
    }

    template<typename T, size_t N>
    std::array<T, N> get_array(std::string_view key_base) {
        std::array<T, N> ret;
        get_array<T>(key_base, std::span<T>{ ret.data(), ret.size() });
        return ret;
    }

private:
    SemaphoreHandle_t m_mutex;

    std::unordered_map<std::string_view, double> m_float_map {};
    std::unordered_map<std::string_view, int64_t> m_integral_map {};

    inline static std::pair<std::string_view, double (*)()> m_special_floats[] {
        { "rtos_cpu_usage", []() -> double { return 3.1415926f; } },
    };

    inline static std::pair<std::string_view, int64_t (*)()> m_special_integrals[] {
        { "hal_lf_ticks", []() -> int64_t { return HAL_GetTick(); } },
        { "rtos_heap_free",
          []() -> int64_t {
              HeapStats_t heap_stats;
              vPortGetHeapStats(&heap_stats);
              return heap_stats.xAvailableHeapSpaceInBytes;
          } },
        { "rtos_heap_allocations",
          []() -> int64_t {
              HeapStats_t heap_stats;
              vPortGetHeapStats(&heap_stats);
              return heap_stats.xNumberOfSuccessfulAllocations;
          } },
        { "rtos_heap_deallocations",
          []() -> int64_t {
              HeapStats_t heap_stats;
              vPortGetHeapStats(&heap_stats);
              return heap_stats.xNumberOfSuccessfulFrees;
          } },
    };

    void lock() const { xSemaphoreTake(m_mutex, portMAX_DELAY); }

    void unlock() const { xSemaphoreGive(m_mutex); }
};

}