/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

#include <Arduino_compat.h>

namespace X9C {

    class Poti {
    public:
        Poti(uint8_t csPin, uint8_t incrPin, uint8_t udPin) :
            _csPin(csPin),
            _incrPin(incrPin),
            _udPin(udPin),
            _value(0)
        {
        }

        /**
         * @brief initialize pins
         *
         */
        void begin()
        {
            pinMode(_csPin, OUTPUT);
            pinMode(_incrPin, OUTPUT);
            pinMode(_udPin, OUTPUT);
            digitalWrite(_csPin, HIGH);
            digitalWrite(_incrPin, LOW);
            digitalWrite(_udPin, LOW);
        }

        /**
         * @brief deselect poti
         *
         */
        void end()
        {
            digitalWrite(_csPin, HIGH); // deselect
        }

        /**
         * @brief set poti value and store in nv memory, if save is true. requires a reset if the cached value is not up to date
         *
         * @param value
         * @param save
         */
        void setValue(uint8_t value, bool save = false)
        {
            if (value == _value) {
                return;
            }
            if (value > 100) {
                value = 100;
            }
            digitalWrite(_csPin, LOW);
            digitalWrite(_udPin, value > _value);
            uint8_t diff = abs(value - _value);
            for(uint8_t i = 0; i < diff; i++) {
                digitalWrite(_incrPin, HIGH);
                delayMicroseconds(1);
                digitalWrite(_incrPin, LOW);
                delayMicroseconds(1);
            }
            if (save) {
                digitalWrite(_csPin, HIGH);
            }
            else {
                digitalWrite(_incrPin, LOW);
                digitalWrite(_csPin, HIGH);
                digitalWrite(_incrPin, HIGH);
            }
            delayMicroseconds(100);
            _value = value;
        }

        /**
         * @brief Get cached value
         *
         * @return uint8_t
         */
        uint8_t getValue() const
        {
            return _value;
        }

        /**
         * @brief reset poti to 0
         */
        void reset()
        {
            // force to 0
            _value = 100;
            setValue(0);
        }

        /**
         * @brief reset poti to 100
         */
        void resetMax()
        {
            // force to max
            _value = 0;
            setValue(100);
        }

        /**
         * @brief reset poti to a specific value
         *
         * @param value
         * @param save
         */
        void reset(uint8_t value, bool save = false)
        {
            reset(false);
            if (value) {
                // set new value
                setValue(value, save);
            }
        }

    private:
        uint8_t _csPin;
        uint8_t _incrPin;
        uint8_t _udPin;
        uint8_t _value;
    };

}
