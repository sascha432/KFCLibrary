# Changelog

## Version 0.1.6

- ESP8266 PIO_FRAMEWORK_ARDUINO_MMU_CACHE16_IRAM48_SECHEAP_SHARED support for form rendering using free IRAM instead of the regular heap

## Version 0.1.5

- Removed EEPROM support from configuration. A single flash sector can be used instead of NVS, but the code is not maintained anymore
- Fixed issue with storing strings in the configuration
- Added NVS support to KFCConfiguration for ESP8266
- Added namespace KFCJson to avoid conflicts with ArduinoJson
- Fixed ConfigStringArray class

## Version 0.1.4

- Separate repo for KFCLibrary
- Changes prior to 0.1.4 can be found at [https://github.com/sascha432/esp8266-kfc-fw](https://github.com/sascha432/esp8266-kfc-fw) / commit [419dacb](https://github.com/sascha432/esp8266-kfc-fw/commit/419dacb3902fcd4d30c3536d1a20d32b22a1c6b8)
