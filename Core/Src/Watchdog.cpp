#include <Watchdog.hpp>

#include <Stuff/Maths/Check/CRC.hpp>

#include <main.h>

#include <Tele/Log.hpp>
#include <Tele/STUtilities.hpp>

namespace Tele {

void terminate_handler() { halt_and_catch_fire(HCF_STD_TERMINATE, "std::terminate"); }

static GPIO_TypeDef* k_led_port = GPIOD;
static constexpr uint16_t k_led_pin_orange = LD3_Pin;
static constexpr uint16_t k_led_pin_green = LD4_Pin;
static constexpr uint16_t k_led_pin_red = LD5_Pin;
static constexpr uint16_t k_led_pin_blue = LD6_Pin;

[[gnu::section(".ccmram")]] InterRebootData g_inter_reboot_data;

uint32_t InterRebootData::self_check() {
    uint32_t old_sum = crc_sum;
    crc_sum = 0;

    using desc_type = Stf::CRCDescriptions::CRC32ISOHDLC;
    Stf::CRCState<desc_type, false> crc_state {};
    for (size_t i = 0; i < sizeof(InterRebootData); i++) {
        uint8_t b = __atomic_load_n(reinterpret_cast<uint8_t*>(this) + i, __ATOMIC_ACQUIRE);

        crc_state.update(b);
    }

    crc_sum = old_sum;
    return crc_state.finished_value();
}

bool InterRebootData::initialize_if_needed() {
    if (self_check() == crc_sum)
        return false;

    hcf_status = HCF_DIDNT_CATCH_FIRE;
    crc_sum = self_check();

    return true;
}

void InterRebootData::catched_fire(uint32_t status, const char* who) {
    hcf_status = status;

    // we don't trust that is non-null
    who = who == nullptr ? "" : who;

    // we don't trust that `who` is null terminated either
    hcf_task_name_sz = 0;
    for (const char* p = who; *p && hcf_task_name_sz <= configMAX_TASK_NAME_LEN; p++, hcf_task_name_sz++) { }

    // actually it might be unwise to dirty the stack with a lot of calls
    /*std::fill(hcf_task_name, hcf_task_name + configMAX_TASK_NAME_LEN, '\0');
    std::copy_n(who, hcf_task_name_sz, hcf_task_name);*/

    for (size_t i = 0; i < hcf_task_name_sz; i++) {
        // volatile-esque
        __atomic_store_n(hcf_task_name + i, who[i], __ATOMIC_RELEASE);
    }

    for (size_t i = hcf_task_name_sz; i < configMAX_TASK_NAME_LEN; i++) {
        __atomic_store_n(hcf_task_name + i, '\0', __ATOMIC_RELEASE);
    }

    __atomic_store_n(&crc_sum, self_check(), __ATOMIC_RELEASE);
}

enum class WarningStatus : int {
    Clear = 0,
    Warned = 1,
    Errored = 2,
};

DiagnosticWatchdogTask::DiagnosticWatchdogTask() { std::set_terminate(terminate_handler); }

[[noreturn]] void DiagnosticWatchdogTask::operator()() {
    ResetCause last_reset_cause = get_reset_cause();

    Log::info("this is a hack... the top-mode prompt destroys the first log entry");
    Log::info("last reset cause was: {}", enum_name(last_reset_cause));

    if (!g_inter_reboot_data.initialize_if_needed()) {
        if (g_inter_reboot_data.hcf_status != HCF_DIDNT_CATCH_FIRE) {
            Log::warn("the previous reset was a HCF ({})", g_inter_reboot_data.hcf_status);

            char name_buf[configMAX_TASK_NAME_LEN];
            std::copy_n(g_inter_reboot_data.hcf_task_name, configMAX_TASK_NAME_LEN, name_buf);

            Log::warn(
              "task name that threw the HCF or additional info, if any: {}",
              std::string_view(name_buf, g_inter_reboot_data.hcf_task_name_sz)
            );
        }

        g_inter_reboot_data.catched_fire(HCF_DIDNT_CATCH_FIRE, "no fire");
    }

    for (;;) {
        vTaskDelay(333);

        if (HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_SET) {
            HAL_GPIO_WritePin(k_led_port, k_led_pin_orange, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(k_led_port, k_led_pin_red, GPIO_PIN_RESET);
        }

        // signal that the watchdog is alive and well
        HAL_GPIO_TogglePin(k_led_port, k_led_pin_blue);

        HeapStats_t heap_stats;
        vPortGetHeapStats(&heap_stats);

        unsigned long rt;
        UBaseType_t num_tasks = uxTaskGetSystemState(data(m_task_status_buffer), uxTaskGetNumberOfTasks(), &rt);

        for (UBaseType_t i = 0; i < num_tasks; i++) {
            TaskStatus_t& task = m_task_status_buffer[i];
            process_task_info(task);
        }
    }
}

void DiagnosticWatchdogTask::process_task_info(TaskStatus_t const& task) {
    static std::array<uint16_t, 32> s_task_watermarks;
    static bool s_initialised = false;

    if (!s_initialised) {
        s_initialised = true;
        std::fill(begin(s_task_watermarks), end(s_task_watermarks), 0xFFFF);
    }

    if (task.xTaskNumber >= s_task_watermarks.size())
        return;

    uint16_t& old_watermark = s_task_watermarks[task.xTaskNumber];
    uint16_t watermark = task.usStackHighWaterMark;

    if (old_watermark <= watermark)
        return;

    old_watermark = watermark;

    if (watermark > 48)
        return;

    if (watermark >= 24) {
        Log::warn(
          "Watchdog", "Task#{} (\"{}\") is sussy! (watermark: {})", //
          task.xTaskNumber, task.pcTaskName, task.usStackHighWaterMark
        );
        return;
    }

    halt_and_catch_fire(HCF_WDT_STACK_OVERFLOW, task.pcTaskName);
}

}

extern "C" __attribute((noreturn)) void halt_and_catch_fire(uint32_t code, const char* who) {
    Tele::breakpoint();

    __disable_irq();

    Tele::g_inter_reboot_data.catched_fire(code, who);

    /*if (from_task) {
        if (from_isr)
            taskENTER_CRITICAL_FROM_ISR();
        else
            taskENTER_CRITICAL();
    }*/

    NVIC_SystemReset();
}

extern "C" void cpp_assert_failed(const char* file, uint32_t line) {
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
        return;

    // Log::error("assertion failed at {}:{}", file, line);
}

extern "C" void assert_failed_strong(uint8_t *file, uint32_t line) {
    halt_and_catch_fire(HCF_ASSERT_FAILURE, "");
}

extern "C" void vApplicationMallocFailedHook() { //
    halt_and_catch_fire(HCF_RTOS_MALLOC, "malloc failed");
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t task, signed char* task_name) {
    halt_and_catch_fire(HCF_RTOS_STACK_OVERFLOW, reinterpret_cast<const char*>(task_name));
}
