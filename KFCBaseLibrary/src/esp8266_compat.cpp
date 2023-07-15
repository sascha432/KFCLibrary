/**
 * Author: sascha_lammers@gmx.de
 */

#if ESP8266

#include <Arduino.h>
#include <PrintString.h>
#include "global.h"
#include "esp8266_compat.h"

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
        case FM_UNKNOWN:
        default:
            str.printf_P(PSTR("MODE#%u"), mode);
            break;
    }
    return str;
}

#if ARDUINO_ESP8266_MAJOR == 2 && ARDUINO_ESP8266_MINOR == 6

    #warning not supported anymore

#endif

#if ARDUINO_ESP8266_MAJOR >= 3

    #include <LwipDhcpServer.h>

    #if ARDUINO_ESP8266_MINOR >= 1

        // the thwo functions are back in this release

    #else

        bool wifi_softap_get_dhcps_lease(struct dhcps_lease *please) {
            return dhcpSoftAP.get_dhcps_lease(please);
        }

        bool wifi_softap_set_dhcps_lease(struct dhcps_lease *please) {
            return dhcpSoftAP.set_dhcps_lease(please);
        }

    #endif

#endif

#endif
