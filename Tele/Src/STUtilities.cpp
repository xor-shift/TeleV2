#include <Tele/STUtilities.hpp>

#include <stm32f4xx_hal.h>
#include <stm32f4xx_hal_rcc.h>

namespace Tele {

ResetCause get_reset_cause() {
#define CAUSE_FACTORY(_flg, _res) else if (__HAL_RCC_GET_FLAG(_flg) != 0u) cause = _res
    ResetCause cause = ResetCause::Unknown;

    if (false) {} // NOLINT(readability-simplify-boolean-expr)
    CAUSE_FACTORY(RCC_FLAG_LPWRRST, ResetCause::LowPower);
    CAUSE_FACTORY(RCC_FLAG_WWDGRST, ResetCause::WindowWatchdog);
    CAUSE_FACTORY(RCC_FLAG_IWDGRST, ResetCause::IndependentWatchdog);
    CAUSE_FACTORY(RCC_FLAG_SFTRST, ResetCause::Software);
    CAUSE_FACTORY(RCC_FLAG_PORRST, ResetCause::PowerOnPowerDown);
    CAUSE_FACTORY(RCC_FLAG_PINRST, ResetCause::ExternalResetPin);
    CAUSE_FACTORY(RCC_FLAG_BORRST, ResetCause::Brownout);

    return cause;
#undef CAUSE_FACTORY
}

}
