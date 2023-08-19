/**
 * Author: sascha_lammers@gmx.de
 */

#if defined(ESP32)

#include <Arduino.h>
#include <time.h>
#include <LoopFunctions.h>
#include "esp32_compat.h"
// #include <esp_adc_cal.h>

bool can_yield()
{
    return !xPortInIsrContext();
}

String ESPGetFlashChipSpeedAndModeStr()
{
    PrintString str;
    auto speed = ESP.getFlashChipSpeed() / 1000000;
    if (speed) {
        str.printf_P(PSTR("%uMHz@"), speed);
    }
    auto mode = ESP.getFlashChipMode();
    switch(mode) {
        case FM_DIO:
            str.print(F("DIO"));
            break;
        case FM_DOUT:
            str.print(F("DOUT"));
            break;
        case FM_QIO:
            str.print(F("QIO"));
            break;
        case FM_QOUT:
            str.print(F("QOUT"));
            break;
        case FM_FAST_READ:
            str.print(F("FAST_READ"));
            break;
        case FM_SLOW_READ:
            str.print(F("SLOW_READ"));
            break;
        case FM_UNKNOWN:
        default:
            str.printf_P(PSTR("MODE#%u"), mode);
            break;
    }
    return str;
}

extern "C" {

    uint32_t sntp_update_delay_MS_rfc_not_less_than_15000();

    uint32_t __wrap_sntp_get_sync_interval()
    {
        return sntp_update_delay_MS_rfc_not_less_than_15000();
    }

    #if 0

        float esp32TemperatureRead()
        {
            adc1_config_width(ADC_WIDTH_BIT_12);
            adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_0);

            uint32_t rawTemperature = adc1_get_raw(ADC1_CHANNEL_6);

            esp_adc_cal_characteristics_t adcCal;
            esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_0, ADC_WIDTH_BIT_12, ESP_ADC_CAL_VAL_EFUSE_VREF, &adcCal);
            auto adcValue = esp_adc_cal_raw_to_voltage(rawTemperature, &adcCal);
            return (adcValue - 760) / 2.5f;
        }

    #else

        #if CONFIG_IDF_TARGET_ESP32S3

            // S3 temperature support will be fully supported in IDF 5.0, it's part of driver refactoring and there are breaking changes.
            // Workaround solution for temp sensor for S3 will be backported to IDF 4.4 but it's not ready yet.

            float esp32TemperatureRead()
            {
                return 99.9f;
            }

        #else

            uint8_t temprature_sens_read();

            float esp32TemperatureRead()
            {
                auto temp = temprature_sens_read();
                if (temp == 128) {
                    return NAN;
                }
                return (temp - 32) / 1.8f;
            }

        #endif

    #endif

}

#endif
