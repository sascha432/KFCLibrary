/**
* Author: sascha_lammers@gmx.de
*/

/*
+rtcm=set,0x11,0x01014161,0x02024262,0x03034363
+rtcm=set,22,0x4464
+rtcm=set,33,0x4565

+rtcm=get,0x11
+rtcm=clr
+rtcm=dump
+rtcm=qc

+rtcm=clr
+rtcm=set,0x11,0x11111111
+rtcm=set,0x22,0x22222222
+rtcm=set,0x33,0x33333333
+rtcm=set,0x44,0x44444444
+rtcm=set,0x55,0x55555555
+rtcm=set,0x66,0x66666666
+rtcm=dump
*/

#include "RTCMemoryManager.h"
#include <crc16.h>
#include <DumpBinary.h>
#include <Buffer.h>
#include <reset_detector.h>

#if DEBUG_RTC_MEMORY_MANAGER
#    if DEBUG_RESET_DETECTOR
#        include "debug_pre_setup.h"
#    else
#        include "debug_helper_enable.h"
#    endif
#else
#    include "debug_helper_disable.h"
#endif

#if ESP8266
#    include <coredecls.h>
#    include <user_interface.h>
#endif

#if RTC_SUPPORT == 0
    RTCMemoryManager::RtcTimer RTCMemoryManager::_rtcTimer;
#endif

namespace RTCMemoryManagerNS {

    #if ESP8266

        #if DEBUG_RTC_MEMORY_MANAGER

            bool system_rtc_mem_read(uint8_t ofs, void *data, uint16_t len) {
                if (!((ofs * 4) >= 256 && (ofs * 4 + len) <= 512 && len != 0)) {
                    __DBG_panic("system_rtc_mem_read ofs=%u len=%d %d-%d", ofs, len, ofs*4, ofs*4+len);
                }
                auto result = ::system_rtc_mem_read(ofs, data, len);
                // __DBG_printf("rtc_read result=%u ofs=%u length=%u data=%p", result, ofs, len, data);
                // PrintString str;
                // DumpBinary dumper(str);
                // dumper.setPerLine(len);
                // dumper.setGroupBytes(4);
                // dumper.dump(data, len);
                // __DBG_printf("rtc_read result=%u ofs=%u length=%u data=%s", result, ofs, len, str.c_str());
                return result;
            }

            bool system_rtc_mem_write(uint8_t ofs, const void *data, uint16_t len) {
                if (!((ofs * 4) >= 256 && (ofs * 4 + len) <= 512 && len != 0)) {
                    __DBG_panic("system_rtc_mem_write ofs=%u len=%d %d-%d", ofs, len, ofs*4, ofs*4+len);
                }
                auto result = ::system_rtc_mem_write(ofs, data, len);
                // __DBG_printf("rtc_write result=%u ofs=%u length=%u data=%p", result, ofs, len, data);
                // PrintString str;
                // DumpBinary dumper(str);
                // dumper.setPerLine(len);
                // dumper.setGroupBytes(4);
                // dumper.dump(data, len);
                // __DBG_printf("rtc_write result=%u ofs=%u length=%u data=%s", result, ofs, len, str.c_str());
                return result;
            }

        #else

            inline static bool system_rtc_mem_read(uint8_t ofs, void *data, uint16_t len)
            {
                __LDBG_assert_printf(
                    (ofs >= RTCMemoryManager::kBaseAddress) &&
                    (((ofs * RTCMemoryManager::kBlockSize) + len) <= (RTCMemoryManager::kMemorySize + (RTCMemoryManager::kBaseAddress * RTCMemoryManager::kBlockSize))) &&
                    (len != 0),
                    "read OOB ofs=%d len=%d", ofs, len
                );
                return ::system_rtc_mem_read(ofs, data, len);
            }

            inline static bool system_rtc_mem_write(uint8_t ofs, const void *data, uint16_t len)
            {
                __LDBG_assert_printf(
                    (ofs >= RTCMemoryManager::kBaseAddress) &&
                    (((ofs * RTCMemoryManager::kBlockSize) + len) <= (RTCMemoryManager::kMemorySize + (RTCMemoryManager::kBaseAddress * RTCMemoryManager::kBlockSize))) &&
                    (len != 0),
                    "write OOB ofs=%d len=%d", ofs, len
                );
                return ::system_rtc_mem_write(ofs, data, len);
            }

        #endif

    #elif ESP32

        RTC_NOINIT_ATTR uint8_t rtcMemoryBlock[RTCMemoryManager::kMemorySize];

        bool system_rtc_mem_read(size_t ofs, void *data, size_t len)
        {
            ofs *= RTCMemoryManager::kBlockSize;
            __LDBG_assert_printf(ofs + len <= (sizeof(rtcMemoryBlock) - RTCMemoryManager::kBlockSize * RTCMemoryManager::kBaseAddress), "read OOB ofs=%d len=%d max=%u", ofs, len, sizeof(rtcMemoryBlock));
            memmove(data, rtcMemoryBlock + ofs, len);
            return true;
        }

        bool system_rtc_mem_write(size_t ofs, void *data, size_t len)
        {
            ofs *= RTCMemoryManager::kBlockSize;
            __LDBG_assert_printf(ofs + len <= (sizeof(rtcMemoryBlock) - RTCMemoryManager::kBlockSize * RTCMemoryManager::kBaseAddress), "write OOB ofs=%d len=%d max=%u", ofs, len, sizeof(rtcMemoryBlock));
            memmove(rtcMemoryBlock + ofs, data, len);
            return true;
        }

    #endif

}

bool RTCMemoryManager::_readHeader(RTCMemoryManager::Header_t &header) {

    if (RTCMemoryManagerNS::system_rtc_mem_read(kHeaderAddress, &header, sizeof(header))) {
        if (header.length < kMemoryLimit) {
            return true;
        }
        __LDBG_printf("invalid length=%d size=%d limit=%d", header.length, header.length, kMemoryLimit);
    }
    else {
        __LDBG_printf("read error ofs=%d size=%d", kHeaderAddress, sizeof(header));
    }
    return false;
}

uint8_t *RTCMemoryManager::_readMemory(Header_t &header, uint16_t extraSize)
{
    uint8_t *buf = nullptr;
    auto memUniquePtr = std::unique_ptr<uint8_t[]>(buf);

    while (_readHeader(header)) {
        auto size = header.data_length() + sizeof(header) + sizeof(Entry_t) + extraSize;
        if __CONSTEXPR17 (kBlockSize > 1) {
            size = ((size + (kBlockSize - 1)) / kBlockSize) * kBlockSize;
        }
        __LDBG_printf("_readMemory crc_ofs=%d data_len=%u mem_alloc=%u", header.crc_offset(), header.data_length(), size);
        if (size >= kMemorySize) {
            __DBG_printf_E("malloc failed length=%u max_size=%u", size, kMemorySize);
            break;
        }
        memUniquePtr.reset(buf = new uint8_t[size]());
        if (!buf) {
            __DBG_printf_E("malloc failed length=%u", size);
            break;
        }
        __LDBG_printf("allocated aligned=%u address=%u crc_len=%u", size, header.start_address(), header.crc_offset());
        uint16_t crc = static_cast<uint16_t>(~0U);
        if (header.crc_offset() > size) {
            __DBG_printf_E("read OOB size=%u max=%u", header.crc_offset(), size);
            break;
        }
        if (!RTCMemoryManagerNS::system_rtc_mem_read(header.start_address(), buf, header.crc_offset())) {
            __LDBG_printf("failed to read data address=%u offset=%u size=%u", header.start_address(), header.start_address() * kBlockSize, header.crc_offset());
            break;
        }
        if ((crc = crc16_update(buf, header.crc_offset())) != header.crc) {
            __LDBG_printf("CRC mismatch %04x != %04x, size=%u crclen=%u", crc, header.crc, header.length, header.crc_offset());
            break;
        }
        __LDBG_printf("read: address=%u[%u-%u] length=%u crc=0x%04x", header.start_address(), kBaseAddress, kLastAddress, header.length, header.crc);
        return memUniquePtr.release();
    }
    return nullptr;
}

uint8_t RTCMemoryManager::read(RTCMemoryId id, void *dataPtr, uint8_t maxSize)
{
    Header_t header __attribute__((aligned(4)));
    Entry_t entry __attribute__((aligned(4)));
    uint8_t *data;
    std::fill_n(reinterpret_cast<uint8_t *>(dataPtr), maxSize, 0);
    auto memUniquePtr = std::unique_ptr<uint8_t[]>(_read(data, header, entry, id));
    if (!memUniquePtr) {
        __LDBG_printf("read = nullptr");
        return 0;
    }
    auto copyLen = std::min(entry.length, maxSize);
    memmove(dataPtr, data, copyLen);
    return copyLen;
}

uint8_t *RTCMemoryManager::_read(uint8_t *&data, Header_t &header, Entry_t &entry, RTCMemoryId id)
{
    MUTEX_LOCK_BLOCK(_lock) {
        __LDBG_printf("_read %p", &header);
        auto memUniquePtr = std::unique_ptr<uint8_t[]>(_readMemory(header, 0));
        if (!memUniquePtr) {
            __LDBG_printf("read = nullptr");
            return nullptr;
        }
        auto memPtr = memUniquePtr.get();
        auto ptr = header.begin(memPtr);
        auto endPtr = header.end(memPtr);
        __LDBG_printf("read from=%p to=%p", ptr, endPtr);
        while(ptr + sizeof(Entry_t) < endPtr) {
            entry = Entry_t(ptr);
            if (!entry) {
                __DBG_printf_E("read eof id=0x00 offset=%u", header.distance(memPtr, ptr));
                break;
            }
            ptr += sizeof(entry);
            if (ptr >= endPtr) {
                __DBG_printf_E("read eof id=0x%02x length=%d offset=%u", entry.mem_id, entry.length, header.distance(memPtr, ptr));
                break;
            }
            __LDBG_printf("read id=%u len=%u", entry.mem_id, entry.length);
            if (static_cast<RTCMemoryId>(entry.mem_id) == id) {
                data = ptr;
                __LDBG_printf("return %p", memPtr);
                return memUniquePtr.release();
            }
            ptr += entry.length;
        }
        __LDBG_printf("return nullptr");
    }
    return nullptr;
}

bool RTCMemoryManager::write(RTCMemoryId id, const void *dataPtr, uint8_t dataLength)
{
    bool result = false;
    MUTEX_LOCK_BLOCK(_lock) {
        if (id == RTCMemoryId::NONE) {
            dataLength = 0;
        }
        __LDBG_printf("write id=%u data=%p len=%u", id, dataPtr, dataLength);

        Header_t header __attribute__((aligned(4)));
        uint16_t newLength = sizeof(header);
        uint8_t *memPtr, *outPtr;
        auto memUniquePtr = std::unique_ptr<uint8_t[]>(memPtr = _readMemory(header, dataLength));
        if (memPtr) {
            // copy existing items
            auto ptr = header.begin(memPtr);
            auto endPtr = header.end(memPtr);
            outPtr = memPtr;
            while(ptr + sizeof(Entry_t) < endPtr) {
                Entry_t entry(ptr);
                __LDBG_printf("read id=%u len=%u distance=%d", entry.mem_id, entry.length, header.distance(memPtr, ptr));
                if (!entry) { // invalid id, NUL bytes for alignment
                    __LDBG_printf("id 0x00 distance=%d", header.distance(memPtr, ptr));
                    break;
                }
                ptr += sizeof(entry);
                if (ptr >= endPtr) {
                    __DBG_printf_E("memory full id=0x%02x length=%d distance=%u", entry.mem_id, entry.length, header.distance(memPtr, ptr));
                    return false;
                }
                if (entry.mem_id != static_cast<decltype(entry.mem_id)>(id)) {
                    memmove(outPtr, &entry, sizeof(entry));
                    outPtr += sizeof(entry);
                    memmove(outPtr, ptr, entry.length);
                    outPtr += entry.length;
                    newLength += entry.length + sizeof(entry);
                    __LDBG_assert_panic(outPtr - ptr < kMemoryLimit, "read OOB size=%d limit=%u", (outPtr - ptr), kMemoryLimit);
                    __LDBG_printf("copy id=%u len=%u distance=%d nl=%u", entry.mem_id, entry.length, header.distance(outPtr, ptr), newLength);
                }
                else {
                    __LDBG_printf("skip id=%u len=%u distance=%u nl=%u", entry.mem_id, entry.length, header.distance(outPtr, ptr), newLength);
                }
                ptr += entry.length;
            }
            __LDBG_printf("rewritten header=%u new_header=%u", header.length, newLength);
            header.length = newLength;
        }
        else {
            // align length to kBlockSize and add header/entry size
            auto size = (((dataLength + (kBlockSize - 1)) / kBlockSize) * kBlockSize) + sizeof(header) + sizeof(Entry_t);
            if (size >= kMemoryLimit) {
                __DBG_printf_E("malloc failed length=%u limit=%u", size, kMemoryLimit);
                return false;
            }
            memUniquePtr.reset(new uint8_t[size]());
            if (!memUniquePtr) {
                __DBG_printf_E("malloc failed length=%u", size);
                return false;
            }
            outPtr = memPtr = memUniquePtr.get();
            header.crc = static_cast<uint16_t>(~0U);
        }

        if (dataLength) {
            // append new data
            Entry_t entry(id, dataLength);
            __LDBG_printf("new id=%u len=%u", entry.mem_id, entry.length);

            memmove(outPtr, &entry, sizeof(entry));
            outPtr += sizeof(entry);
            memmove(outPtr, dataPtr, entry.length);
            outPtr += entry.length;

            newLength += entry.length + sizeof(entry);
        }
        header.length = newLength;

        if __CONSTEXPR17 (kBlockSize > 1) {
            __LDBG_printf("unaligned length=%u", header.length);
            // align before adding header
            while (!_isAligned(header.length)) {
                *outPtr++ = 0;
                header.length++;
            }
            __LDBG_printf("aligned length=%u", header.length);
        }
        header.crc = static_cast<uint16_t>(~0U);

        // append header
        memmove(outPtr, &header, sizeof(header));

        // update CRC in newData and store
        header.crc = crc16_update(memPtr, header.crc_offset());
        memmove(outPtr + offsetof(Header_t, crc), &header.crc, sizeof(header.crc));

        __LDBG_printf("write: address=%u-%u[%u-%u] length=%u crc=0x%04x", header.start_address(), header.start_address() + header.length - 1, kBaseAddress, kLastAddress, header.length, header.crc);
        result = RTCMemoryManagerNS::system_rtc_mem_write(header.start_address(), memPtr, header.length);
        __LDBG_printf("result=%d", result);
    }
    return result;
}

bool RTCMemoryManager::clear()
{
    bool result = false;
    MUTEX_LOCK_BLOCK(_lock) {
        #if ESP32
            __LDBG_printf("clearing RTC memory");
            std::fill(std::begin(RTCMemoryManagerNS::rtcMemoryBlock), std::end(RTCMemoryManagerNS::rtcMemoryBlock), 0xff);
        #else
            // clear kClearNumBlocks blocks including header
            constexpr auto kAddress = (kMemorySize - (kClearNumBlocks * kBlockSize)) / kBlockSize;
            static_assert(((kClearNumBlocks * kBlockSize) % kBlockSize) == 0, "not a multiple of kBlockSize");
            uint32_t data[(kClearNumBlocks * kBlockSize) / sizeof(uint32_t)];
            std::fill(std::begin(data), std::end(data), 0xffffffffU);
            __LDBG_printf("clear: address=%u[%u-%u] length=%u", kBaseAddress + kAddress, kBaseAddress, kLastAddress, sizeof(data));
            if (!RTCMemoryManagerNS::system_rtc_mem_write(kBaseAddress + kAddress, &data, sizeof(data))) {
                return false;
            }
        #endif
    }
    result = write(RTCMemoryId::NONE, nullptr, 0);
    __LDBG_printf("result=%d", result);
    return result;
}

#if DEBUG

    #include "plugins.h"

    bool RTCMemoryManager::dump(Print &output, RTCMemoryId displayId)
    {
        bool result = false;
        MUTEX_LOCK_BLOCK(_lock) {
            Header_t header __attribute__((aligned(4)));
            auto memPtr = _readMemory(header, 0);
            if (!memPtr) {
                output.println(F("RTC data not set or invalid"));
                return false;
            }
            auto memUniquePtr = std::unique_ptr<uint8_t[]>(memPtr);

            #if RTC_SUPPORT == 0
                output.printf_P(PSTR("RTC memory time: %u\n"), RTCMemoryManager::readTime().getTime());
            #endif

            output.printf_P(PSTR("RTC data length: %u\n"), header.data_length());

            DumpBinary dumper(output);
            auto ptr = header.begin(memPtr);
            auto endPtr = header.end(memPtr);
            while(ptr + sizeof(Entry_t) < endPtr) {
                Entry_t entry(ptr);
                __LDBG_printf("read id=%u len=%u", entry.mem_id, entry.length);
                if (!entry) {
                    break;
                }
                #if DEBUG_RTC_MEMORY_MANAGER
                {
                    PrintString out;
                    DumpBinary dumper(out);
                    dumper.dump(ptr + sizeof(entry), entry.length);
                    dumper.setPerLine(entry.length);
                    dumper.setGroupBytes(4);
                    __LDBG_printf("rtcm=%d id=0x%02x length=%u data=%s", header.distance(memPtr, ptr), entry.mem_id, entry.length, out.c_str());
                }
                #endif
                ptr += sizeof(entry);
                if (ptr >= endPtr) {
                    __LDBG_printf("entry length exceeds total size. id=0x%02x entry_length=%u size=%u", entry.mem_id, entry.length, header.length);
                    break;
                }
                if (displayId == RTCMemoryId::NONE || displayId == static_cast<RTCMemoryId>(entry.mem_id)) {
                    if (entry.length) {
                        result = true;
                    }
                    #if HAVE_KFC_PLUGINS
                        output.printf_P(PSTR("id: 0x%02x (%s), length %d "), entry.mem_id, PluginComponent::getMemoryIdName(entry.mem_id), entry.length);
                    #else
                        output.printf_P(PSTR("id: 0x%02x, length %d "), entry.mem_id, entry.length);
                    #endif
                    dumper.setGroupBytes(4);
                    dumper.setPerLine(entry.length);
                    dumper.dump(ptr, entry.length);
                }
                ptr += entry.length;
            }
            __LDBG_printf("result=%d", result);
        }
        return result;
    }

#endif

RTCMemoryManager::RtcTime RTCMemoryManager::_readTime()
{
    RtcTime time;
    #if RTC_SUPPORT == 0
        if (read(RTCMemoryId::RTC, &time, sizeof(time)) == sizeof(time)) {
            __LDBG_printf("read time=%u status=%s", time.getTime(), time.getStatus());
            return time;
        }
        __LDBG_printf("invalid RtcTime");
    #endif
    return RtcTime();
}

void RTCMemoryManager::_writeTime(const RtcTime &time)
{
    #if RTC_SUPPORT == 0
        __LDBG_printf("write time=%u status=%s", time.getTime(), time.getStatus());
        write(RTCMemoryId::RTC, &time, sizeof(time));
    #endif
}

#if RTC_SUPPORT == 0

    void RTCMemoryManager::setupRTC()
    {
        _rtcTimer.startTimer(1000, true);
    }

    void RTCMemoryManager::updateTimeOffset(uint32_t offset)
    {
        __LDBG_printf("update time offset=%ums", offset);
        offset /= 1000;
        if (offset) {
            auto rtc = _readTime();
            rtc.time += offset;
            _writeTime(rtc);
        }
    }

#endif

void RTCMemoryManager::_clearTime()
{
    __LDBG_print("clearTime");
    remove(RTCMemoryId::RTC);
}
