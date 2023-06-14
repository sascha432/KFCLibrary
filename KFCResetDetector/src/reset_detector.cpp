/**
 * Author: sascha_lammers@gmx.de
 */

#include <Arduino_compat.h>
#include "reset_detector.h"
#include <stl_ext/memory.h>
#include <LoopFunctions.h>
#include <PluginComponent.h>
#include <plugins_menu.h>
#include <RTCMemoryManager.h>
#include <OSTimer.h>
#include <serial_handler.h>
#include <deep_sleep.h>
#include <PrintHtmlEntitiesString.h>
#include "save_crash.h"
#if IOT_SWITCH
#include "../src/plugins/switch/switch_def.h"
#endif

#if DEBUG_RESET_DETECTOR
#    include "debug_pre_setup.h"
#endif

//
// separate function to initialize objects before the global c'tor function is executed
//
// must be defined as UninitializedClass<CLASS> and use a pointer or reference
//
// using ResetDetectorUninitialized = stdex::UninitializedClass<ResetDetector>;
// static ResetDetectorUninitialized resetDetectorNoInit __attribute__((section(".noinit")));
// ResetDetector &resetDetector = resetDetectorNoInit._object;
//
// all other c'tors are initialized in reverse order by do_global_ctors(void) in core_esp8266_main.cpp or esp_system/startup.c

using ResetDetectorUninitialized = stdex::UninitializedClass<ResetDetector>;
static ResetDetectorUninitialized resetDetectorNoInit __attribute__((section(".noinit")));
ResetDetector &resetDetector = resetDetectorNoInit._object;

using PluginsVectorUninitialized = stdex::UninitializedClass<PluginComponents::PluginsVector>;
static PluginsVectorUninitialized pluginsVectorNoInit __attribute__((section(".noinit")));

using PluginRegisterUninitialized = stdex::UninitializedClass<PluginComponents::RegisterEx>;
static PluginRegisterUninitialized pluginRegisterNoInit __attribute__((section(".noinit")));

using RtcMemoryLockUninitialized = stdex::UninitializedClass<SemaphoreMutexStatic>;
static RtcMemoryLockUninitialized rtcMemoryLockNoInit __attribute__((section(".noinit")));

SemaphoreMutexStatic &RTCMemoryManager::_lock = rtcMemoryLockNoInit._object;

namespace PluginComponents {

    PluginsVector &_plugins = pluginsVectorNoInit._object;
    RegisterEx &_pluginRegister = pluginRegisterNoInit._object;

}

#if ENABLE_DEEP_SLEEP

    using DeepSleepPinStateUninitialized = stdex::UninitializedClass<DeepSleep::PinState>;
    static DeepSleepPinStateUninitialized deepSleepPinStateNoInit __attribute__((section(".noinit")));

    using DeepSleepParamUninitialized = stdex::UninitializedClass<DeepSleepParam>;
    static DeepSleepParamUninitialized deepSleepParamNoInit __attribute__((section(".noinit")));

    namespace DeepSleep {
        PinState &deepSleepPinState = deepSleepPinStateNoInit._object;
        DeepSleepParam &deepSleepParams = deepSleepParamNoInit._object;
    }

#endif

#if ESP32

    using ETSTimerExTimersUninitialized = stdex::UninitializedClass<ETSTimerExTimerVector>;
    static ETSTimerExTimersUninitialized timersNoInit __attribute__((section(".noinit")));

    ETSTimerExTimerVector *timer_list = &timersNoInit._object;

#endif

#if DEBUG_RESET_DETECTOR || ESP32
    uint32_t reset_detector_setup_global_ctors_counter = 0;
#endif

// needs to be called from preinit()
void reset_detector_setup_global_ctors()
{
    #if DEBUG_RESET_DETECTOR || ESP32
        reset_detector_setup_global_ctors_counter++;
        __LDBG_printf("reset_detector_setup_global_ctors=%u", reset_detector_setup_global_ctors_counter);
        if (reset_detector_setup_global_ctors_counter > 1) {
            return;
        }
    #endif

    #if ESP32
        timersNoInit.ctor();
    #endif
    #if ENABLE_DEEP_SLEEP
        DeepSleep::enableWiFiOnBoot = false;
        deepSleepPinStateNoInit.ctor();
        deepSleepParamNoInit.ctor();
    #endif
    rtcMemoryLockNoInit.ctor();
    pluginsVectorNoInit.ctor();
    pluginRegisterNoInit.ctor();
    resetDetectorNoInit.ctor();
    resetDetector.begin();
    #if ENABLE_DEEP_SLEEP
        DeepSleep::preinit();
        #warning this has not been tested yet
    #endif
}

ResetDetector::ResetDetector()
{
    __LDBG_printf("c'tor ResetDetector");
    #if ESP8266 && DEBUG_RESET_DETECTOR
        _uart = nullptr;
    #endif
}

void ResetDetector::end()
{
    #if ESP8266 && DEBUG_RESET_DETECTOR
        __LDBG_printf("end _uart=%p", _uart);
        if (_uart) {
            __LDBG_printf("\r\n");
            uart_flush(_uart);
            uart_uninit(_uart);
            _uart = nullptr;
        }
    #endif
}

void ResetDetector::begin(HardwareSerial *serial, int baud)
{
    #if ESP8266
        #if DEBUG_RESET_DETECTOR
            __LDBG_printf("begin _uart=%p serial=%p baud=%u ctors=%u", _uart, serial, baud, reset_detector_setup_global_ctors_counter);

            if (_uart) {
                __LDBG_printf("\r\n");
                uart_flush(_uart);
                uart_uninit(_uart);
                _uart = nullptr;
                __LDBG_printf("_uart=%p", _uart);
            }
            __LDBG_printf("serial=%p begin=%u", serial, baud);
        #endif
        #if IOT_LED_MATRIX_STANDBY_PIN == 3 || IOT_LED_MATRIX_OUTPUT_PIN == 3
            serial->begin(baud, SERIAL_8N1, SERIAL_TX_ONLY, 1);
        #else
            serial->begin(baud, SERIAL_8N1);
        #endif
    #elif ESP32
        serial->begin(baud);
    #endif
}

void ResetDetector::begin()
{
    #if ESP8266
        #if DEBUG_RESET_DETECTOR
            auto oldUart = _uart;
            __LDBG_printf("rd::begin(), _uart=%p", _uart);
            if (_uart) {
                __LDBG_printf("begin() called multiple times without end()");
                end();
            }
            _uart = uart_init(UART0, 115200, (int) SERIAL_8N1, (int) SERIAL_FULL, 1, 64, false);
            __LDBG_printf("debug_begin has been called, old_uart=%p _uart=%p ctors=%u", oldUart, _uart, reset_detector_setup_global_ctors_counter);
        #endif

        __LDBG_printf("init reset detector");

        _readData();
        ++_data;

        struct rst_info &resetInfo = *system_get_rst_info();
        _data.pushReason(resetInfo.reason);
        __LDBG_printf("depc=%08x epc1=%08x epc2=%08x epc3=%08x exccause=%08x excvaddr=%08x reason=%u", resetInfo.depc, resetInfo.epc1, resetInfo.epc2, resetInfo.epc3, resetInfo.exccause, resetInfo.excvaddr, resetInfo.reason);

        #if ENABLE_DEEP_SLEEP == 0
            __DBG_printf("WIFI mode=%u default=%u", wifi_get_opmode(), wifi_get_opmode_default());
            if (wifi_get_opmode() != WIFI_OFF) {
                wifi_set_opmode_current(WIFI_OFF);
            }
            if (wifi_get_opmode_default() != WIFI_OFF) {
                wifi_set_opmode(WIFI_OFF);
            }
        #endif

        __LDBG_printf("valid=%d safe_mode=%u counter=%u new=%u", !!_storedData, _storedData.isSafeMode(), _storedData.getResetCounter(), _data.getResetCounter());
        __LDBG_printf("info=%s crash=%u reset=%u reboot=%u wakeup=%u", getResetInfo().c_str(), hasCrashDetected(), hasResetDetected(), hasRebootDetected(), hasWakeUpDetected());

    #elif ESP32

        struct rst_info resetInfo = {};
        resetInfo.reason = esp_reset_reason();

        _readData();
        ++_data;
        _data.pushReason(resetInfo.reason);

        __LDBG_printf("depc=%08x epc1=%08x epc2=%08x epc3=%08x exccause=%08x excvaddr=%08x reason=%u", resetInfo.depc, resetInfo.epc1, resetInfo.epc2, resetInfo.epc3, resetInfo.exccause, resetInfo.excvaddr, resetInfo.reason);

    #endif

    #if DEBUG_RESET_DETECTOR
        __LDBG_printf("reason history");
        for(auto reason: _storedData) {
            __LDBG_printf("%u - %s", reason, getResetReason(reason));
        }
        __LDBG_printf("%u - %s (recent)", _data.getReason(), getResetReason());
    #endif

    _writeData();

    #if ESP8266
        // ets_timer_* seems to be ready at this point
        armTimer();
    #elif ESP32
        // armTimer();
        // the timer must be started in setup()
        // calling it before will cause an exception
    #endif

    #if IOT_SWITCH && IOT_SWITCH_STORE_STATES_RTC_MEM
        SwitchPlugin_rtcMemLoadState();
    #endif

    __LDBG_printf("return");
}

const __FlashStringHelper *ResetDetector::getResetReason(uint8_t reason)
{
#if USE_ESP_GET_RESET_REASON
    if (static_cast<int>(reason) == 254) {
        return F("User exception (panic/abort/assert)");
    }
    switch(reason) {
        // normal startup by power on
        case REASON_DEFAULT_RST:
            return F("Power On");
        // hardware watch dog reset
        case REASON_WDT_RST:
            return F("Hardware Watchdog");
        // exception reset, GPIO status won’t change
        case REASON_EXCEPTION_RST:
            return F("Exception");
        // software watch dog reset, GPIO status won’t change
        case REASON_SOFT_WDT_RST:
            return F("Software Watchdog");
        // software restart ,system_restart , GPIO status won’t change
        case REASON_SOFT_RESTART:
            return F("Software/System restart");
        // wake up from deep-sleep
        case REASON_DEEP_SLEEP_AWAKE:
            return F("Deep-Sleep Wake");
        // // external system reset
        case REASON_EXT_SYS_RST:
            return F("External System");
        default:
            break;
    }
    return F("Unknown");
#elif defined(ESP32)
    switch(reason) {
        case ESP_RST_POWERON:
            return F("Normal startup");
        case ESP_RST_PANIC:
            return F("Panic reset");
        case ESP_RST_INT_WDT:
            return F("Int. WDT reset");
        case ESP_RST_TASK_WDT:
            return F("Task WDT reset");
        case ESP_RST_WDT:
            return F("WDT reset");
        case ESP_RST_SW:
            return F("System restart");
        case ESP_RST_DEEPSLEEP:
            return F("Wake up from deep-sleep");
        case ESP_RST_EXT:
            return F("External system reset");
        case ESP_RST_BROWNOUT:
            return F("Brownout");
        case ESP_RST_SDIO:
            return F("Reset over SDIO");
        default:
            break;
    }
    return F("Unknown");
#else
    switch(reason) {
        case REASON_DEFAULT_RST:
            return F("Normal startup");
        case REASON_WDT_RST:
            return F("WDT reset");
        case REASON_EXCEPTION_RST:
            return F("Exception reset");
        case REASON_SOFT_WDT_RST:
            return F("Soft WDT reset");
        case REASON_SOFT_RESTART:
            return F("System restart");
        case REASON_DEEP_SLEEP_AWAKE:
            return F("Wake up from deep-sleep");
        case REASON_EXT_SYS_RST:
            return F("External system reset");
    }
    return F("Unknown");
#endif
}


#if DEBUG

void ResetDetector::__setResetCounter(Counter_t counter)
{
    __LDBG_printf("_writeData()");
    _storedData = counter;
    _data = counter;
    _writeData();
}
#endif

void ResetDetector::_readData()
{
    __LDBG_printf("_readData()");
    if (RTCMemoryManager::read(PluginComponent::RTCMemoryId::RESET_DETECTOR, &_storedData, sizeof(_storedData))) {
        _storedData.setValid(true);
        _data = _storedData;
    }
    else {
        _storedData = Data();
        _data = Data(0, false);
    }
}

#include "logger.h"

ResetDetectorPlugin plugin;

PROGMEM_DEFINE_PLUGIN_OPTIONS(
    ResetDetectorPlugin,
    "rd",               // name
    "Reset Detector",   // friendly name
    "",                 // web_templates
    "",                 // config_forms
    "",                 // reconfigure_dependencies
    PluginComponent::PriorityType::RESET_DETECTOR,
    PluginComponent::RTCMemoryId::RESET_DETECTOR,
    static_cast<uint8_t>(PluginComponent::MenuType::CUSTOM),
    true,               // allow_safe_mode
    true,               // setup_after_deep_sleep
    true,               // has_get_status
    false,              // has_config_forms
    false,              // has_web_ui
    true,               // has_web_templates
    true,               // has_at_mode
    0                   // __reserved
);

ResetDetectorPlugin::ResetDetectorPlugin() : PluginComponent(PROGMEM_GET_PLUGIN_OPTIONS(ResetDetectorPlugin))
{
    REGISTER_PLUGIN(this, "ResetDetectorPlugin");
}

void ResetDetectorPlugin::getStatus(Print &output)
{
    auto info = SaveCrash::createFlashStorage().getInfo();
    output.printf_P(PSTR("%u crash report(s), total size "), info.numTraces());
    output.print(formatBytes(info.size()));
    output.printf_P(PSTR(HTML_S(br) "%s of %s available"), formatBytes(info.available()).c_str(), formatBytes(info.capacity()).c_str());
}


#if !RESET_DETECTOR_INCLUDE_HPP_INLINE
#include "reset_detector.hpp"
#endif

