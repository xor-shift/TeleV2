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

static std::atomic<int32_t> timestamp_base = 0;

namespace Tele {

void set_time(int32_t timestamp) {
    static_assert(std::is_same_v<long, int32_t>);
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

static uint8_t s_pre_kernel_bump_arena[256];
static std::atomic<uint32_t> s_bump_ptr = 0;

extern "C" void* malloc(size_t sz) {
    /*if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        uint32_t ptr = (s_bump_ptr += sz) - sz;
        return s_pre_kernel_bump_arena + ptr;
    }*/

    return pvPortMalloc(sz);
}

extern "C" void free(void* ptr) {
    if (s_pre_kernel_bump_arena <= ptr && ptr <= s_pre_kernel_bump_arena + 256)
        return;

    return vPortFree(ptr);
}
