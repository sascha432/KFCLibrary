/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

#include <Arduino_compat.h>
#include <PrintString.h>
#include <stl_ext/utility.h>
#include <stl_ext/fixed_circular_buffer.h>
#include <pin.h>

#if DEBUG_PIN_MONITOR
#    include <debug_helper_enable.h>
#else
#    include <debug_helper_disable.h>
#endif

namespace PinMonitor {

    namespace Interrupt {

        struct PinAndMask {
            constexpr PinAndMask(const int index) : pin(index), mask(GPIO_PIN_TO_MASK(index)) {
            }
            constexpr operator int() const {
                return pin;
            }
            const uint8_t pin;
            const GPIOMaskType mask;

            template<typename _Ta>
            static constexpr GPIOMaskType mask_of(const _Ta &arr, const size_t index = 0, const GPIOMaskType mask = 0) {
                return index < arr.size() ? (GPIO_PIN_TO_MASK(arr[index]) | mask_of(arr, index + 1, mask)) : mask;
            }
        };

        static constexpr size_t kEventQueueSize = PIN_MONITOR_EVENT_QUEUE_SIZE;

        #if ESP8266

            static constexpr uint16_t kValueMask = 0b1111000000111111;
            static constexpr uint8_t kPinNumMask = 0b11111;
            static constexpr uint8_t kPinNumBit = 6;
            static constexpr uint8_t kGPIO16Bit = 11;

            struct __attribute__((packed)) Event {

                Event() : _time(0), _value(0)
                {}

                // value = GPI
                // pin16 = GP16I
                Event(uint32_t time, uint8_t pin, uint16_t value, bool pin16) :
                    _time(time),
                    _value(value)
                {
                    #if DEBUG
                        if (pin >= NUM_DIGITAL_PINS) {
                            __DBG_panic("invalid pin=%u", pin);
                        }
                    #endif
                    _pinNum = pin;
                    _pin16 = pin16;
                }

                // value = GPI
                // GPIO16 is ignored
                Event(uint32_t time, uint8_t pin, uint16_t value) :
                    _time(time),
                    _value(value)
                    // _value((value & kValueMask) | ((pin & kPinNumMask) << kPinNumBit))
                {
                    #if DEBUG
                        if (pin >= NUM_DIGITAL_PINS) {
                            __DBG_panic("invalid pin=%u", pin);
                        }
                    #endif
                    _pinNum = pin;
                    _pin16 = 0;
                }

                inline __attribute__((__always_inline__))
                bool operator==(uint8_t pin) const {
                    return _pinNum == pin;
                }

                inline __attribute__((__always_inline__))
                bool operator!=(uint8_t pin) const {
                    return _pinNum != pin;
                }

                inline __attribute__((__always_inline__))
                uint32_t getTime() const {
                    return _time;
                }

                inline __attribute__((__always_inline__))
                bool value() const {
                    return (_value & GPIO_PIN_TO_MASK(_pinNum)) != 0;
                }

                inline __attribute__((__always_inline__))
                bool value(uint8_t pin) const {
                    return (_value & GPIO_PIN_TO_MASK(pin)) != 0;
                }

                inline __attribute__((__always_inline__))
                uint8_t pin() const {
                    return _pinNum;
                }

                // include GPIO16
                GPIOValueType gpiReg16Value() const {
                    return (_value & kValueMask) | (_pin16 << 16);
                }

                inline __attribute__((__always_inline__))
                uint16_t gpiRegValue() const {
                    return _value & kValueMask;
                }

                String toString() const {
                    return PrintString(F("time=%u pin=%u (%u[0]%u[1]%u[2]%u[3]%u[4]%u[5]%u[12]%u[13]%u[14]%u[15]%u[16])"),
                        getTime(), pin(), _pin0, _pin1, _pin2, _pin3, _pin4, _pin5, _pin12, _pin13, _pin14, _pin15, _pin16
                    );
                }

                inline __attribute__((__always_inline__))
                void setTime(uint32_t time) {
                    _time = time;
                }

                inline __attribute__((__always_inline__))
                void setValue(uint16_t value) {
                    _value = value;
                }

            protected:
                uint32_t _time;
                union {
                    uint16_t _value;
                    struct {
                        uint16_t _pin0: 1;          // GPIO0
                        uint16_t _pin1: 1;
                        uint16_t _pin2: 1;
                        uint16_t _pin3: 1;
                        uint16_t _pin4: 1;
                        uint16_t _pin5: 1;          // GPIO5
                        uint16_t _pinNum: 5;        // used to store the pin (6 - 11 = are used for the flash interface)
                        uint16_t _pin16: 1;         // GPIO16 (!)
                        uint16_t _pin12: 1;         // GPIO12
                        uint16_t _pin13: 1;
                        uint16_t _pin14: 1;
                        uint16_t _pin15: 1;         // GPIO15
                    };
                };
            };

        #elif ESP32

            static constexpr GPIOValueType kValueMask = SOC_GPIO_VALID_GPIO_MASK;

            struct __attribute__((packed)) Event {

                Event() : _time(0), _value(0)
                {}

                Event(uint32_t time, uint8_t pin, GPIOValueType value) :
                    _time(time),
                    _value(value)
                {
                    #if DEBUG
                        if (GPIO_IS_VALID_GPIO(pin)) {
                            __DBG_panic("invalid pin=%u", pin);
                        }
                    #endif
                    _pinNum = pin;
                }

                inline __attribute__((__always_inline__))
                bool operator==(uint8_t pin) const {
                    return _pinNum == pin;
                }

                inline __attribute__((__always_inline__))
                bool operator!=(uint8_t pin) const {
                    return _pinNum != pin;
                }

                inline __attribute__((__always_inline__))
                uint32_t getTime() const {
                    return _time;
                }

                inline __attribute__((__always_inline__))
                bool value() const {
                    return (_value & GPIO_PIN_TO_MASK(_pinNum)) != 0;
                }

                inline __attribute__((__always_inline__))
                bool value(uint8_t pin) const {
                    return (_value & GPIO_PIN_TO_MASK(pin)) != 0;
                }

                inline __attribute__((__always_inline__))
                uint8_t pin() const {
                    return _pinNum;
                }

                inline __attribute__((__always_inline__))
                GPIOValueType gpiRegValue() const {
                    return _value & kValueMask;
                }

                String toString() const {
                    auto out = PrintString(F("time=%u pin=%u ("), getTime(), pin());
                    for(uint8_t i = 0; i < SOC_GPIO_PIN_COUNT; i++) {
                        if (GPIO_IS_VALID_GPIO(i)) {
                            out.printf_P(PSTR("%u[%u]"), ((_value & GPIO_PIN_TO_MASK(i)) ? 1 : 0), i);
                        }
                    }
                    out.print(')');
                    return out;
                }

                inline __attribute__((__always_inline__))
                void setTime(uint32_t time) {
                    _time = time;
                }

                inline __attribute__((__always_inline__))
                void setValue(uint16_t value) {
                    _value = value;
                }

            protected:
                static constexpr auto kGPIOPins = SOC_GPIO_PIN_COUNT; // we need enough bits for all pins
                static constexpr auto kPinBits = (sizeof(GPIOValueType) * 8) - kGPIOPins; // the rest can be used to stored the pin
                static_assert(kGPIOPins < (1 << kPinBits), "bitfield _pinNum too small");

                uint32_t _time;
                union {
                    GPIOValueType _value: kGPIOPins;
                    GPIOValueType _pinNum: kPinBits;
                };
            };

        #endif

        using EventBuffer = stdex::fixed_circular_buffer<Event, kEventQueueSize>;

        static constexpr auto kEventSize = sizeof(Event);
        static constexpr auto kEventBufferSize = sizeof(EventBuffer);
        static constexpr float kEventBufferElementSize = sizeof(EventBuffer) / static_cast<float>(kEventQueueSize);

    }

}

#if DEBUG_PIN_MONITOR
#    include <debug_helper_disable.h>
#endif
