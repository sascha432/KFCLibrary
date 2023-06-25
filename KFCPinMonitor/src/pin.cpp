/**
  Author: sascha_lammers@gmx.de
*/

#if PIN_MONITOR

#include "pin_monitor.h"
#include "pin.h"
#include <Arduino_compat.h>
#if ESP8266
#    include <Schedule.h>
#endif
#include "interrupt_impl.h"
#include "monitor.h"
#include "rotary_encoder.h"

#if DEBUG_PIN_MONITOR
#    include <debug_helper_enable.h>
#else
#    include <debug_helper_disable.h>
#endif

using namespace PinMonitor;

#if PIN_MONITOR_USE_GPIO_INTERRUPT == 0 || PIN_MONITOR_USE_POLLING == 1

    // #if ESP32

    //     #define _GPI GPIO::read()
    //     #define mask GPIO_PIN_TO_MASK(pinPtr->getPin())

    //     void IRAM_ATTR HardwarePin::callback(void *arg)
    //     {
    //         auto pinPtr = reinterpret_cast<HardwarePin *>(arg);
    //         #if PIN_MONITOR_DEBOUNCED_PUSHBUTTON || PIN_MONITOR_ROTARY_ENCODER_SUPPORT
    //             auto _micros = micros();
    //         #endif
    //         bool level = gpio_get_level(gpio_num_t(pinPtr->getPin()));
    //         bool level2 = _GPI & mask;
    //         if (level != level2) {
    //             raise(SIGTRAP);
    //         }
    //         switch(pinPtr->_type) {
    //             #if PIN_MONITOR_DEBOUNCED_PUSHBUTTON
    //                 case HardwarePinType::DEBOUNCE:
    //                     // reinterpret_cast<DebouncedHardwarePin *>(arg)->addEvent(_micros, gpio_get_level(gpio_num_t(pinPtr->getPin())));
    //                     reinterpret_cast<DebouncedHardwarePin *>(arg)->addEvent(_micros, level);
    //                     break;
    //             #endif
    //             #if PIN_MONITOR_SIMPLE_PIN
    //                 case HardwarePinType::SIMPLE:
    //                     reinterpret_cast<SimpleHardwarePin *>(arg)->addEvent(level);
    //                     break;
    //             #endif
    //             #if PIN_MONITOR_ROTARY_ENCODER_SUPPORT
    //                 case HardwarePinType::ROTARY:
    //                     PinMonitor::eventBuffer.emplace_back(_micros, pinPtr->getPin(), _GPI);
    //                     break;
    //             #endif
    //             default:
    //                 break;
    //         }
    //     }

    //     #undef _GPI
    //     #undef mask

    #if 1

        INNER_CALLBACK_ATTR
        void HardwarePin::callback(void *arg, GPIOValueType GPIOValues, GPIOMaskType GPIOMask)
        {
            auto pinPtr = reinterpret_cast<HardwarePin *>(arg);
            #if PIN_MONITOR_DEBOUNCED_PUSHBUTTON || PIN_MONITOR_ROTARY_ENCODER_SUPPORT
                auto _micros = micros();
            #endif
            switch(pinPtr->_type) {
                #if PIN_MONITOR_DEBOUNCED_PUSHBUTTON
                    case HardwarePinType::DEBOUNCE:
                        reinterpret_cast<DebouncedHardwarePin *>(arg)->addEvent(_micros, GPIOValues & GPIOMask);
                        break;
                #endif
                #if PIN_MONITOR_SIMPLE_PIN
                    case HardwarePinType::SIMPLE:
                        reinterpret_cast<SimpleHardwarePin *>(arg)->addEvent(GPIOValues & GPIOMask);
                        break;
                #endif
                #if PIN_MONITOR_ROTARY_ENCODER_SUPPORT
                    case HardwarePinType::ROTARY:
                        PinMonitor::eventBuffer.emplace_back(_micros, pinPtr->getPin(), GPIOValues);
                        break;
                #endif
                default:
                    break;
            }
        }

        void IRAM_ATTR HardwarePin::callback(void *arg)
        {
            auto pinPtr = reinterpret_cast<HardwarePin *>(arg);
            callback(arg, GPIO::read(), GPIO_PIN_TO_MASK(pinPtr->getPin()));
        }

    #endif

#endif

#endif
