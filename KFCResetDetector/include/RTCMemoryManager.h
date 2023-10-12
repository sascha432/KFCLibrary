/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include <Arduino_compat.h>
#include <push_pack.h>

#if HAVE_KFC_FIRMWARE_VERSION
#    include <EventScheduler.h>
#    include <PluginComponent.h>
#endif

#ifndef DEBUG_RTC_MEMORY_MANAGER
#    define DEBUG_RTC_MEMORY_MANAGER (0 || defined(DEBUG_ALL))
#endif

#if DEBUG_RTC_MEMORY_MANAGER
#    include "debug_helper_enable.h"
#else
#    include "debug_helper_disable.h"
#endif

#include <Utility/ProgMemHelper.h>

// any data stored in RTC memory is kept during deep sleep and device reboots
// data integrity is ensured
// NOTE for ESP32: during normal boots, the data is erased and the reset button seems to be detected as normal boot (tested with heltec wifi lora32)

class RTCMemoryManager {
private:
    RTCMemoryManager() {} // pure static class
public:
    #if HAVE_KFC_FIRMWARE_VERSION
        using RTCMemoryId = PluginComponent::RTCMemoryId;
    #else
        using RTCMemoryId = uint8_t;
    #endif

    static constexpr auto kMemorySize = 256;
    static_assert(kMemorySize % 4 == 0 && kMemorySize <= 384, "invalid kMemorySize");
    #if ESP8266
        /*
        Layout of RTC Memory is as follows:
        Ref: Espressif doc 2C-ESP8266_Non_OS_SDK_API_Reference, section 3.3.23 (system_rtc_mem_write)

        |<------system data (256 bytes)------->|<-----------------user data (512 bytes)--------------->|

        SDK function signature:
        bool	system_rtc_mem_read	(
                        uint32	des_addr,
                        void	*	src_addr,
                        uint32	save_size
        )

        The system data section can't be used by the user, so:
        des_addr must be >=64 (i.e.: 256/4) and <192 (i.e.: 768/4)
        src_addr is a pointer to data
        save_size is the number of bytes to write

        For the method interface:
        offset is the user block number (block size is 4 bytes) must be >= 0 and <128
        data is a pointer to data, 4-byte aligned
        size is number of bytes in the block pointed to by data

        Same for write

        Note: If the Updater class is in play, e.g.: the application uses OTA, the eboot
        command will be stored into the first 128 bytes of user data, then it will be
        retrieved by eboot on boot. That means that user data present there will be lost.
        Ref:
        - discussion in PR #5330.
        - https://github.com/esp8266/esp8266-wiki/wiki/Memory-Map#memmory-mapped-io-registers
        - Arduino/bootloaders/eboot/eboot_command.h RTC_MEM definition
        */
        static constexpr auto kRTCMemorySize = 768; // total size
        static constexpr auto kBlockSize = 4; // block size
        static constexpr auto kBaseAddress = (kRTCMemorySize - kMemorySize) / kBlockSize; // block number
        static_assert(kBaseAddress >= 96 && kBaseAddress < 192, "invalid kBaseAddress");
    #elif ESP32
        static constexpr auto kBaseAddress = 0;
        static constexpr auto kBlockSize = 1;
        static constexpr auto kRTCMemorySize = kMemorySize * kBlockSize;
        static_assert(kBaseAddress != 0, "invalid kBaseAddress");
    #else
        #error invalid target
    #endif
    static constexpr auto kLastAddress = kBaseAddress + (kMemorySize / kBlockSize) - 1;

    struct Header_t {
        uint16_t length;
        uint16_t crc;
        // get length of the data
        inline uint16_t data_length() const
        {
            return length - sizeof(Header_t);
        }
        // get header length without crc
        inline uint16_t header_len_without_crc() const
        {
            return offsetof(Header_t, crc);
        }
        // get offset of the crc
        inline uint16_t crc_offset() const
        {
            return data_length() + header_len_without_crc();
        }
        // get start address
        inline uint16_t start_address() const
        {
            return ((kMemorySize - length) / kBlockSize) + kBaseAddress;
        }
        // get start pointer to the start of the data
        inline uint8_t *begin(void *ptr) const
        {
            return reinterpret_cast<uint8_t *>(ptr);
        }
        // get pointer to the end of the data
        inline uint8_t *end(void *ptr) const
        {
            return begin(ptr) + data_length();
        }
    };

    struct Entry_t {
        uint8_t mem_id;
        uint8_t length;
        // constructors
        Entry_t() :
            mem_id(0),
            length(0)
        {
        }
        Entry_t(const uint8_t *ptr) :
            mem_id(ptr[0]),
            length(ptr[1])
        {
        }
        Entry_t(RTCMemoryManager::RTCMemoryId id, uint8_t len) :
            mem_id(static_cast<uint8_t>(id)),
            length(len)
        {
        }
        // validation
        operator bool() const
        {
            return mem_id != 0 && length != 0;
        }
        uint16_t append(uint8_t *&dstPtr, const void *srcPtr) const
        {
            memmove(dstPtr, this, sizeof(*this));
            dstPtr += sizeof(*this);
            memmove(dstPtr, srcPtr, length);
            dstPtr += length;
            return length + sizeof(*this);
        }
    };

    struct ReadReturn_t {
        uint8_t *_memPtr;
        uint8_t *_dataPtr;
        Entry_t _entry;
        // constructors
        ReadReturn_t(uint8_t *memPtr = nullptr, uint8_t *dataPtr = nullptr, Entry_t entry = Entry_t()) :
            _memPtr(memPtr),
            _dataPtr(dataPtr),
            _entry(entry)
        {
        }
        // validation
        operator bool() const
        {
            return _memPtr != nullptr;
        }
        // get memory pointer
        auto getUniqueMemPtr() const
        {
            return std::unique_ptr<uint8_t[]>(_memPtr);
        }
    };

public:
    enum class SyncStatus : uint8_t {
        NO = 0,
        YES,
        UNKNOWN,
        NTP_UPDATE
    };

    struct RtcTime {
        #if RTC_SUPPORT == 0
            uint32_t time;
        #endif
        SyncStatus status;

        RtcTime(time_t _time = 0, SyncStatus _status = SyncStatus::UNKNOWN) :
            #if RTC_SUPPORT == 0
                time(_time),
            #endif
            status(_status)
        {
        }

        uint32_t getTime() const {
            #if RTC_SUPPORT
                return 0;
            #else
                return time;
            #endif
        }

        static const __FlashStringHelper *getStatus(SyncStatus status) {
            switch(status) {
                case SyncStatus::YES:
                    return F("In sync");
                case SyncStatus::NO:
                    return F("Out of sync");
                case SyncStatus::NTP_UPDATE:
                    return F("NTP update in progress");
                case SyncStatus::UNKNOWN:
                    break;
            }
            return F("Unknown");
        }

        const __FlashStringHelper *getStatus() const {
            return getStatus(status);
        }
    };

public:
    static constexpr uint16_t kHeaderOffset = kMemorySize - sizeof(Header_t);
    static constexpr uint16_t kHeaderAddress = (kHeaderOffset / kBlockSize) + kBaseAddress;

    inline static bool _isAligned(size_t len) {
        if  __CONSTEXPR17 (kBlockSize > 1) {
            return (len % kBlockSize) == 0;
        }
        return true;
    }

    inline static uint16_t _getAlignedLength(uint16_t len) {
        if  __CONSTEXPR17 (kBlockSize > 1) {
            return ((len + (kBlockSize - 1)) / kBlockSize) * kBlockSize;
        }
        return len;
    }

    static_assert(sizeof(Header_t) % kBlockSize == 0, "header not aligned");

public:
    // read data for id
    // if data is nullptr, it returns the length only
    // returns 0 as error or the length<=maxSize of the data
    // maxSize is limited to 255 bytes
    // data is filled with maxSize zero bytes even if an error occurs
    static uint8_t read(RTCMemoryId id, void *data, uint8_t maxSize);

    // write data for id
    // maxSize is limited to 255 bytes
    static bool write(RTCMemoryId id, const void *, uint8_t maxSize);

    // remove data for id
    static bool remove(RTCMemoryId id);

    // clear entire RTC memory
    static bool clear();

    // returns the number of dumped objects or -1 for error
    static int dump(Print &output, RTCMemoryId id = RTCMemoryId::NONE/*dump all*/);

    static SemaphoreMutexStatic &_lock;

private:
    static bool _readHeader(Header_t &header);
    static uint8_t *_readMemory(Header_t &header, uint16_t extraSize);
    static ReadReturn_t _read(RTCMemoryId id);

    static bool _write(RTCMemoryId id, const void *, uint8_t maxSize);

public:
    // methods for the internal RTC
    static void setupRTC();
    static void updateTimeOffset(uint32_t millis_offset);
    static void storeTime();

    // methods for the RTC
    static void setTime(time_t time, SyncStatus status);
    static SyncStatus getSyncStatus();
    static void setSyncStatus(SyncStatus status);

    inline static RtcTime readTime() {
        return _readTime();
    }

private:
    static RtcTime _readTime();
    static void _writeTime(const RtcTime &time);
    static void _clearTime();

    #if RTC_SUPPORT == 0 && RTC_SUPPORT_NO_TIMER == 0
    public:
        class RtcTimer : public OSTimer {
        public:
            RtcTimer() : OSTimer(OSTIMER_NAME("RtcTimer")) {}

            virtual void run() {
                RTCMemoryManager::storeTime();
            }
        };

        static RtcTimer _rtcTimer;
    #endif
};

inline void RTCMemoryManager::storeTime()
{
    #if RTC_SUPPORT == 0
        auto rtc = _readTime();
        rtc.time = time(nullptr);
        _writeTime(rtc);
    #endif
}

inline RTCMemoryManager::RtcTime RTCMemoryManager::_readTime()
{
    RtcTime rtc;
    if (read(RTCMemoryId::RTC, &rtc, sizeof(rtc)) == sizeof(rtc)) {
        return rtc;
    }
    return RtcTime();
}

inline void RTCMemoryManager::_writeTime(const RtcTime &time)
{
    write(RTCMemoryId::RTC, &time, sizeof(time));
}

inline bool RTCMemoryManager::remove(RTCMemoryId id)
{
    return write(id, nullptr, 0);
}

inline void RTCMemoryManager::_clearTime()
{
    remove(RTCMemoryId::RTC);
}

inline RTCMemoryManager::SyncStatus RTCMemoryManager::getSyncStatus()
{
    return _readTime().status;
}

inline void RTCMemoryManager::setTime(time_t time, SyncStatus status)
{
    auto rtc = RtcTime(time, status);
    __LDBG_printf("set time=%u status=%s", rtc.getTime(), rtc.getStatus());
    _writeTime(rtc);
}

inline void RTCMemoryManager::setSyncStatus(SyncStatus newStatus)
{
    auto rtc = _readTime();
    __LDBG_printf("new status=%u old status=%u", newStatus, rtc.status);
    if (newStatus != rtc.status) {
        rtc.status = newStatus;
        _writeTime(rtc);
    }
}

inline void RTCMemoryManager::setupRTC()
{
    #if RTC_SUPPORT == 0 && RTC_SUPPORT_NO_TIMER == 0
        _rtcTimer.startTimer(1000, true);
    #endif
}

inline void RTCMemoryManager::updateTimeOffset(uint32_t offset)
{
    #if RTC_SUPPORT == 0
        __LDBG_printf("update time offset=%ums", offset);
        offset /= 1000;
        if (offset) {
            auto rtc = _readTime();
            rtc.time += offset;
            _writeTime(rtc);
        }
    #endif
}

#include <pop_pack.h>

#if DEBUG_RTC_MEMORY_MANAGER
#    include "debug_helper_disable.h"
#endif
