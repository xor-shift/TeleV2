#include "stdcompat.hpp"

#include <algorithm>
#include <atomic>

#include <sys/time.h>

#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "main.h"

extern "C" {
extern RNG_HandleTypeDef hrng;
}

static std::atomic_int32_t timestamp_base = 0;

namespace Tele {

void set_time(int32_t timestamp) {
    timestamp_base = timestamp;
}

}

extern "C" void _gettimeofday(struct timeval* tv, void*) {
    const HAL_TickFreqTypeDef freq = HAL_GetTickFreq();
    const uint32_t ticks = HAL_GetTick();
    const uint64_t milliseconds = static_cast<uint64_t>(freq) * ticks;

    tv->tv_sec = static_cast<long>(milliseconds / 1000L) + timestamp_base;
    tv->tv_usec = static_cast<long>(milliseconds % 1000L) * 1000L;
}

extern "C" void getentropy(void* buffer, size_t length) {
    const size_t whole_words = length / sizeof(uint32_t);
    const size_t excess_bytes = length % sizeof(uint32_t);

    for (auto i = 0uz; i < whole_words; i++) {
        const uint32_t v = HAL_RNG_GetRandomNumber(&hrng);
        reinterpret_cast<uint32_t*>(buffer)[i] = v;
    }

    if (excess_bytes == 0)
        return;

    const uint32_t excess_v = HAL_RNG_GetRandomNumber(&hrng);
    const uint8_t* excess_vp = reinterpret_cast<const uint8_t*>(&excess_v);
    std::copy(excess_vp, excess_vp + excess_bytes, reinterpret_cast<uint8_t*>(buffer) + length - excess_bytes);
}

struct FreeRTOSAllocator {
    [[nodiscard]] void* allocate(size_t sz) { // NOLINT(readability-convert-member-functions-to-static)
        return pvPortMalloc(sz);
    }

    void deallocate(void* ptr, size_t) { // NOLINT(readability-convert-member-functions-to-static)
        return vPortFree(ptr);
    }
};

template<size_t ArenaSize = 256> struct AtomicArenaAllocator {
    inline static constexpr size_t arena_size = ArenaSize;
    [[nodiscard]] void* allocate(size_t sz) {
        size_t excess = sz % alignof(std::max_align_t);
        sz += excess;

        uint32_t offset = m_arena_ptr += sz;
        offset -= sz;

        void* ret = m_arena + offset;

        if (offset + sz >= arena_size)
            ret = nullptr;

        if (ret == nullptr) {
            halt_and_catch_fire(HCF_RTOS_MALLOC, "ISR mallocator failed");
        }

        return ret;
    }

    void deallocate(void*, size_t) { // NOLINT(readability-convert-member-functions-to-static)
        //
    }

    bool ptr_in_arena(const void* ptr) const {
        const char* arena_begin = m_arena;
        const char* arena_end = m_arena + arena_size;
        const char* arg = reinterpret_cast<const char*>(ptr);

        return arena_end > arg && arg >= arena_begin;
    }

private:
    std::atomic_uint32_t m_arena_ptr = 0;
    alignas(std::max_align_t) char m_arena[arena_size];
};

struct MixedRTOSAllocator {
    [[nodiscard]] void* allocate(size_t sz) {
        bool call_rtos_allocator = true;
        // call_rtos_allocator &= xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
        call_rtos_allocator &= xPortIsInsideInterrupt() == pdFALSE;

        if (call_rtos_allocator) {
            return m_allocator.allocate(sz);
        }

        m_isr_allocations++;
        return m_isr_allocator.allocate(sz);
    }

    void deallocate(void* ptr, size_t) {
        if (m_isr_allocator.ptr_in_arena(ptr))
            return;

        return vPortFree(ptr);
    }

    uint32_t isr_alloc_count() const { return m_isr_allocations.load(); }

private:
    std::atomic_uint32_t m_isr_allocations = 0;
    AtomicArenaAllocator<> m_isr_allocator {};

    FreeRTOSAllocator m_allocator {};
};

[[gnu::section(".ccmram")]] uint8_t ucHeap[ configTOTAL_HEAP_SIZE ];

MixedRTOSAllocator s_allocator {};

extern "C" void vApplicationMallocFailedHook();

extern "C" void* malloc(size_t sz) {
    void* ret = s_allocator.allocate(sz);

    return ret;
}

extern "C" void free(void* ptr) { return s_allocator.deallocate(ptr, 0); }
