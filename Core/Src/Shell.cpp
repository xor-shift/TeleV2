#include <Shell.hpp>

#include <usbd_cdc_if.h>

namespace Tele {

template<typename Callback> struct LineBuffer {
    constexpr LineBuffer(Callback&& callback)
        : m_callback(std::move(callback)) { }

    constexpr void new_char(char c) {
        if (c >= ' ' && c <= '~') {
            if (m_buffer_utilization == size(m_buffer))
                return;

            m_buffer[m_buffer_utilization++] = c;
            return;
        }

        if (c == '\r' || c == '\n') {
            std::invoke(m_callback, cur_line());
            m_buffer_utilization = 0;
            return;
        }

        if (c == '\x7F') {
            if (m_buffer_utilization == 0)
                return;

            --m_buffer_utilization;
            return;
        }
    }

    constexpr std::string_view cur_line() { return { data(m_buffer), m_buffer_utilization }; }

private:
    Callback m_callback;

    std::array<char, 80> m_buffer;
    uint32_t m_buffer_utilization = 0;
};

void TerminalTask::create(const char* name) {
    m_queue_handle = xQueueCreateStatic(k_queue_size, sizeof(QueueElement), data(m_queue_storage), &m_static_queue);

    if (m_queue_handle == nullptr) {
        throw std::runtime_error("m_queue_handle is null");
    }

    m_stream_buffer_handle = xStreamBufferCreateStatic(
      size(m_stream_buffer_storage), 1, data(m_stream_buffer_storage), &m_static_stream_buffer
    );

    if (m_stream_buffer_handle == nullptr) {
        throw std::runtime_error("m_stream_buffer_handle is null");;
    }

    StaticTask::create(name);
}

void TerminalTask::isr_new_chars(std::string_view str) {
    while (!str.empty()) {
        QueueElement elem {
            .sz = std::min(str.size(), QueueElement::max_sz),
            .incoming = true,
        };

        std::copy_n(begin(str), elem.sz, elem.data.data());
        str = str.substr(elem.sz);

        BaseType_t higher_prio_task_awoken = pdFALSE;
        xQueueSendFromISR(m_queue_handle, &elem, &higher_prio_task_awoken);
        portYIELD_FROM_ISR(higher_prio_task_awoken);
    }
}

void TerminalTask::send_str(std::string_view str) {
    std::string_view sv = str;
    while (!sv.empty()) {
        QueueElement elem {
            .sz = std::min(QueueElement::max_sz, sv.size()),
            .incoming = false,
        };
        std::copy_n(begin(sv), elem.sz, elem.data.data());
        sv = sv.substr(elem.sz);

        if (xQueueSend(m_queue_handle, &elem, /*portMAX_DELAY*/ 0) != pdTRUE) {
            break;
        }
    }
}

void TerminalTask::operator()() {
    LineBuffer line_buffer { [this](std::string_view line) { input_line_callback(line); } };

    auto push_num = [](auto& stream, size_t n) {
        if (n > 9999)
            n = 9999;

        std::array<char, 4> buf;
        auto res = std::to_chars(begin(buf), end(buf), n);
        if (res.ec != std::errc())
            return false;

        for (auto it = begin(buf); it != res.ptr; it++)
            stream << *it;

        return true;
    };

    auto set_cursor = [this, &push_num](size_t i, size_t j) {
        std::array<char, 4 + 2 * 4> buffer;
        auto res = fmt::format_to_n(data(buffer), buffer.size(), "\033[{};{}f", i, j);
        raw_send({ data(buffer), res.out });
    };

    auto save_cursor = [this] {
        // raw_send("\0337");
        raw_send("\033[s");
    };

    auto restore_cursor = [this] {
        // raw_send("\0338");
        raw_send("\033[u");
    };

    enum class EraseLineMode {
        CursorToEnd = 0,
        CursorToBeg = 1,
        WholeLine = 2,
    };

    auto erase_line = [this](EraseLineMode mode) {
        char buf[] = "\033[ K";
        buf[2] = static_cast<char>(mode) + '0';
        raw_send(buf);
    };

    // positive amount will scroll up as if \n was sent a few times
    auto scroll = [this, &push_num](int v) {
        bool up = v > 0;
        v = v < 0 ? -v : v;

        std::array<char, 3 + 4> buffer;
        BufCharStream stream { buffer };
        stream << "\033[";
        if (!push_num(stream, v))
            return;
        stream << (up ? 'S' : 'T');

        raw_send(stream);
    };

    auto display_prompt_top = [&] {
        save_cursor();
        set_cursor(0, 0);
        erase_line(EraseLineMode::WholeLine);
        raw_send(m_prompt);
        raw_send(line_buffer.cur_line());
    };

    auto clear_prompt_top = [&] {
        // no-op, since the prompt is at the top, the buffer can
        // just scroll over it.
        restore_cursor();
    };

    auto display_prompt_bottom = [&] {
        save_cursor();
        // scroll(1);
        // erase_line(EraseLineMode::WholeLine);
        raw_send("\r\n");
        raw_send(m_prompt);
        raw_send(line_buffer.cur_line());
    };

    auto clear_prompt_bottom = [&] {
        // scroll(-1);
        erase_line(EraseLineMode::WholeLine);
        restore_cursor();
    };

    auto& clear_prompt = clear_prompt_top;
    auto& display_prompt = display_prompt_top;

    for (;;) {
        QueueElement elem;
        if (xQueueReceive(m_queue_handle, &elem, portMAX_DELAY) != pdTRUE) {
            throw std::runtime_error("xQueueReceive failed");
        }

        if (elem.incoming) {
            clear_prompt();
            for (char c : elem.str()) {
                line_buffer.new_char(c);
            }
            display_prompt();
        } else {
            clear_prompt();
            raw_send(elem.str());
            display_prompt();
        }
    }
}

void TerminalTask::raw_send(std::string_view buffer) {
    /*for (char c : buffer) {
        CDC_Transmit_FS((uint8_t*)&c, 1);
        vTaskDelay(10);
    }*/

    while (CDC_Transmit_FS((uint8_t*)buffer.data(), buffer.size()) != USBD_OK)
        vTaskDelay(1);
}

void TerminalTask::input_line_callback(std::string_view line) {
    send_str(m_prompt);
    send_str(line);
    send_str("\r\n");
    new_line(line);
}

}
