#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

#include <tl/expected.hpp>

extern "C" {
#include <cmsis_gcc.h>
}

template<typename T, size_t N>
    requires std::is_trivial_v<T>
struct CircularBuffer {
    /// @return
    /// if true, the buffer was previously empty
    bool reserve_write_index(uint32_t& index) {
        uint32_t prev_state = spin_on_action(
          [&](uint32_t state) {
              uint16_t* state_ptr = &reinterpret_cast<uint16_t&>(state);
              ++state_ptr[0] %= m_buffer.size();
              return state;
          },
          &m_state
        );

        index = lower(prev_state);
        return index == upper(prev_state);
    }

    /// @return
    /// if false, the buffer was empty and index is not modified
    bool get_read_index(uint32_t& index) {
        uint32_t prev_state = spin_on_action(
          [&](uint32_t state) {
              uint16_t* state_ptr = &reinterpret_cast<uint16_t&>(state);

              uint16_t& write_idx = state_ptr[0];
              uint16_t& read_idx = state_ptr[1];

              if (write_idx == read_idx) [[unlikely]]
                  return state;

              ++read_idx %= m_buffer.size();

              return state;
          },
          &m_state
        );

        if (upper(prev_state) == lower(prev_state))
            return false;

        index = upper(prev_state);
        return true;
    }

    T& push_back(T v, bool& was_empty) {
        uint32_t index;
        was_empty = reserve_write_index(index);
        return m_buffer[index] = v;
    }

    T* pop_front() {
        uint32_t index;
        if (!get_read_index(index))
            return nullptr;

        return &m_buffer[index];
    }

    bool empty() const {
        uint32_t prev_state = m_state;
        return upper(prev_state) == lower(prev_state);
    }

private:
    std::array<T, N> m_buffer;

    // in MSB order, the first 32 bits are the read and the last 32 bits are the write index
    volatile uint32_t m_state = 0;

    [[gnu::always_inline]] static inline uint64_t lower(uint64_t v) { return v & 0xFFFF'FFFFull; }
    [[gnu::always_inline]] static inline uint32_t lower(uint32_t v) { return v & 0xFFFFul; }
    [[gnu::always_inline]] static inline uint64_t upper(uint64_t v) { return lower(v >> 32ul); }
    [[gnu::always_inline]] static inline uint32_t upper(uint32_t v) { return lower(v >> 16ul); }

    template<typename Fn>
    [[gnu::always_inline]] static inline uint32_t spin_on_action(Fn&& action, volatile uint32_t* val) {
        uint32_t read;

        do {
            read = __LDREXW(val);
        } while (__STREXW(std::invoke(action, read), val) != 0);

        return read;
    }
};
