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

#if RTC_SUPPORT == 0 && RTC_SUPPORT_NO_TIMER == 0
    RTCMemoryManager::RtcTimer RTCMemoryManager::_rtcTimer;
#endif

namespace RTCMemoryManagerNS {

    #if ESP8266

        inline static bool system_rtc_mem_read(uint8_t ofs, void *data, uint16_t len)
        {
            __DBG_assertf(
                (ofs >= RTCMemoryManager::kBaseAddress) &&
                ((((ofs - RTCMemoryManager::kBaseAddress) * RTCMemoryManager::kBlockSize) + len) <= RTCMemoryManager::kMemorySize) &&
                (len != 0),
                "read OOB ofs=%d len=%d", ofs, len
            );
            // __DBG_printf("rtcr ofs=%u end=%u len=%u", ofs, ((ofs * RTCMemoryManager::kBlockSize) + len), len);
            return ::system_rtc_mem_read(ofs, data, len);
        }

        inline static bool system_rtc_mem_write(uint8_t ofs, const void *data, uint16_t len)
        {
            __DBG_assertf(
                (ofs >= RTCMemoryManager::kBaseAddress) &&
                ((((ofs - RTCMemoryManager::kBaseAddress) * RTCMemoryManager::kBlockSize) + len) <= RTCMemoryManager::kMemorySize) &&
                (len != 0),
                "write OOB ofs=%d len=%d", ofs, len
            );
            // __DBG_printf("rtcw ofs=%u end=%u len=%u", ofs, ((ofs * RTCMemoryManager::kBlockSize) + len), len);
            return ::system_rtc_mem_write(ofs, data, len);
        }

    #elif ESP32

        RTC_NOINIT_ATTR uint8_t rtcMemoryBlock[RTCMemoryManager::kMemorySize];

        bool system_rtc_mem_read(size_t ofs, void *data, size_t len)
        {
            ofs *= RTCMemoryManager::kBlockSize;
            __LDBG_assertf(ofs + len <= (sizeof(rtcMemoryBlock) - RTCMemoryManager::kBlockSize * RTCMemoryManager::kBaseAddress), "read OOB ofs=%d len=%d max=%u", ofs, len, sizeof(rtcMemoryBlock));
            memmove(data, rtcMemoryBlock + ofs, len);
            return true;
        }

        bool system_rtc_mem_write(size_t ofs, void *data, size_t len)
        {
            ofs *= RTCMemoryManager::kBlockSize;
            __LDBG_assertf(ofs + len <= (sizeof(rtcMemoryBlock) - RTCMemoryManager::kBlockSize * RTCMemoryManager::kBaseAddress), "write OOB ofs=%d len=%d max=%u", ofs, len, sizeof(rtcMemoryBlock));
            memmove(rtcMemoryBlock + ofs, data, len);
            return true;
        }

    #endif

}

bool RTCMemoryManager::_readHeader(RTCMemoryManager::Header_t &header)
{
    // the header is always located at the end of the memory
    auto result = RTCMemoryManagerNS::system_rtc_mem_read(kHeaderAddress, &header, sizeof(header));
    __LDBG_printf("_readHeader addr=%u crc=%04x@%u len=%u dlen=%u res=%u lim=%u", header.start_address(), header.crc, header.crc_offset(), header.length, header.data_length(), result, kMemorySize);
    if (result) {
        if (header.length <= kMemorySize) {
            return true;
        }
        __LDBG_printf_E("invalid len=%d size=%d limit=%d", header.length, header.length, kMemorySize);
    }
    else {
        __LDBG_printf_E("read error ofs=%d size=%d", kHeaderAddress, sizeof(header));
    }
    return false;
}

uint8_t *RTCMemoryManager::_readMemory(Header_t &header, uint16_t extraSize)
{
    // get header
    while (_readHeader(header)) {
        auto size = _getAlignedLength(header.length + extraSize);
        __LDBG_printf("_readMemory addr=%u crc_ofs=%d dlen=%u alloc=%u", header.start_address(), header.crc_offset(), header.data_length(), size);
        if (size > kMemorySize) {
            __LDBG_printf_E("size=%u>%u", size, kMemorySize);
            break;
        }
        auto buf = new uint8_t[size]();
        if (!buf) {
            __LDBG_printf_E("malloc failed size=%u", size);
            break;
        }
        auto memUniquePtr = std::unique_ptr<uint8_t[]>(buf);
        if (header.length > size) {
            __LDBG_printf_E("read OOB len=%u size=%u", header.length, size);
            break;
        }
        // read data except the crc at the end
        if (!RTCMemoryManagerNS::system_rtc_mem_read(header.start_address(), buf, header.crc_offset())) {
            __LDBG_printf_E("read error addr=%u size=%u", header.start_address(), header.crc_offset());
            break;
        }
        // generate crc in buffer and compare with header
        auto crc = crc16_update(buf, header.crc_offset());
        if (crc != header.crc) {
            __LDBG_printf_E("addr=%u crc=%04x!=%04x len=%u", header.start_address(), crc, header.crc, header.length);
            break;
        }
        __LDBG_printf("read: address=%u[%u-%u] len=%u crc=0x%04x", header.start_address(), kBaseAddress, kLastAddress, header.length, header.crc);
        return memUniquePtr.release();
    }
    return nullptr;
}

uint8_t RTCMemoryManager::read(RTCMemoryId id, void *dataPtr, uint8_t maxSize)
{
    if (dataPtr) {
        std::fill_n(reinterpret_cast<uint8_t *>(dataPtr), maxSize, 0);
    }
    auto result = _read(id);
    __LDBG_printf("read id=%d src=%p dst=%p len=%d max=%d", id, result._memPtr, dataPtr, result._entry.length, maxSize);
    if (!result) {
        return 0;
    }
    auto memUniquePtr = result.getUniqueMemPtr();
    auto copyLen = std::min(result._entry.length, maxSize);
    if (dataPtr) {
        memmove(dataPtr, result._dataPtr, copyLen);
    }
    return copyLen;
}

RTCMemoryManager::ReadReturn_t RTCMemoryManager::_read(RTCMemoryId id)
{
    MUTEX_LOCK_BLOCK(_lock) {
        Header_t header;
        auto memPtr = _readMemory(header, 0);
        if (!memPtr) {
            __LDBG_printf("read=nullptr id=%u", id);
            return ReadReturn_t();
        }
        auto memUniquePtr = std::unique_ptr<uint8_t[]>(memPtr);
        auto ptr = header.begin(memPtr);
        auto endPtr = header.end(memPtr);
        __LDBG_printf("read from=%p to=%p", ptr, endPtr);
        while(ptr + sizeof(Entry_t) < endPtr) {
            auto entry = Entry_t(ptr);
            if (!entry) { // invalid id, NUL bytes for alignment
                break;
            }
            ptr += sizeof(entry);
            if (ptr + entry.length >= endPtr) {
                __LDBG_printf_E("read OOB id=0x%02x len=%d", entry.mem_id, entry.length);
                break;
            }
            __LDBG_printf("read id=%u len=%u", entry.mem_id, entry.length);
            if (static_cast<RTCMemoryId>(entry.mem_id) == id) {
                __LDBG_printf("return %u ReadReturnType(%p, %p, %u)", id, memPtr, ptr, entry.length);
                #if DEBUG_RTC_MEMORY_MANAGER
                {
                    PrintString out;
                    DumpBinary dumper(out);
                    dumper.dump(ptr, entry.length);
                    dumper.setPerLine(entry.length);
                    dumper.setGroupBytes(4);
                    __LDBG_printf("id=0x%02x len=%u data=%s", entry.mem_id, entry.length, out.c_str());
                }
                #endif
                return ReadReturn_t(memUniquePtr.release(), ptr, entry);
            }
            ptr += entry.length;
        }
        __LDBG_printf("return=nullptr id=%u", id);
        return ReadReturn_t();
    }
    __builtin_unreachable();
}

bool RTCMemoryManager::write(RTCMemoryId id, const void *dataPtr, uint8_t dataLength)
{
    MUTEX_LOCK_BLOCK(_lock) {
        return _write(id, dataPtr, dataLength);
    }
    __builtin_unreachable();
}

bool RTCMemoryManager::_write(RTCMemoryId id, const void *dataPtr, uint8_t dataLength)
{
    if (id == RTCMemoryId::NONE || dataPtr == nullptr) {
        dataLength = 0;
    }
    __LDBG_printf("write id=%u data=%p len=%u", id, dataPtr, dataLength);

    Header_t header;
    uint16_t currentLength = sizeof(header);
    uint8_t *outPtr;
    auto memPtr = _readMemory(header, dataLength + sizeof(Entry_t));
    auto memUniquePtr = std::unique_ptr<uint8_t[]>(memPtr);
    if (memPtr) {
        // copy existing items
        auto srcPtr = header.begin(memPtr);
        auto endPtr = header.end(memPtr);
        outPtr = memPtr;
        while(srcPtr + sizeof(Entry_t) < endPtr) {
            auto entry = Entry_t(srcPtr);
            if (!entry) { // invalid id, NUL bytes for alignment
                break;
            }
            srcPtr += sizeof(entry);
            if (entry.mem_id != static_cast<decltype(entry.mem_id)>(id)) {
                __LDBG_printf("copy id=%u len=%u nl=%u", entry.mem_id, entry.length, currentLength);
                currentLength += entry.append(outPtr, srcPtr);
            }
            else {
                __LDBG_printf("skip id=%u len=%u nl=%u", entry.mem_id, entry.length, currentLength);
            }
            srcPtr += entry.length;
        }
        __LDBG_printf("rewritten header len=%u old=%u", currentLength, header.length);
    }
    else {
        // create new items
        // align length to kBlockSize and add header/entry size
        auto size = _getAlignedLength(dataLength + sizeof(Entry_t) + sizeof(header));
        if (size > kMemorySize) {
            __LDBG_printf_E("size=%u>%u", size, kMemorySize);
            return false;
        }
        memPtr = outPtr = new uint8_t[size]();
        if (!memPtr) {
            __LDBG_printf_E("malloc failed length=%u", size);
            return false;
        }
        memUniquePtr.reset(memPtr);
        __LDBG_printf("new header size=%u", size);
    }

    if (dataLength) {
        // append new data
        auto entry = Entry_t(id, dataLength);
        __LDBG_printf("new item id=%u len=%u", entry.mem_id, entry.length);
        currentLength += entry.append(outPtr, dataPtr);
    }

    if __CONSTEXPR17 (kBlockSize > 1) {
        // align before adding header
        while (!_isAligned(currentLength)) {
            *outPtr++ = 0;
            currentLength++;
        }
    }

    // update header
    header.length = currentLength;
    // crc of the data
    auto crc = crc16_update(memPtr, header.data_length());
    // append crc of the header
    header.crc = crc16_update(crc, &header, offsetof(Header_t, crc));

    // append header
    memmove(outPtr, &header, sizeof(header));

    __LDBG_printf("write: address=%u-%u[%u-%u] len=%u crc=0x%04x", header.start_address(), header.start_address() + (header.length / kBlockSize) - 1, kBaseAddress, kLastAddress, header.length, header.crc);
    bool result = RTCMemoryManagerNS::system_rtc_mem_write(header.start_address(), memPtr, header.length);
    __LDBG_printf("result=%d", result);
    return result;
}

bool RTCMemoryManager::clear()
{
    MUTEX_LOCK_BLOCK(_lock) {
        #if ESP32
            __LDBG_printf("clearing RTC memory");
            std::fill(std::begin(RTCMemoryManagerNS::rtcMemoryBlock), std::end(RTCMemoryManagerNS::rtcMemoryBlock), 0xff);
        #else
            __LDBG_printf("clear: address=%u-%u", kBaseAddress, kLastAddress);
            uint8_t data[kBlockSize];
            std::fill(std::begin(data), std::end(data), 0xff);
            uint16_t addr = kBaseAddress;
            while(addr <= kLastAddress) {
                if (!RTCMemoryManagerNS::system_rtc_mem_write(addr, &data, sizeof(data))) {
                    __LDBG_printf_E("failed to clear block=%u[%u-%u]", addr, kBaseAddress, kLastAddress);
                    return false;
                }
                addr++;
            }
        #endif
        return _write(RTCMemoryId::NONE, nullptr, 0);
    }
    __builtin_unreachable();
}

#if DEBUG

    #include "plugins.h"

    int RTCMemoryManager::dump(Print &output, RTCMemoryId displayId)
    {
        MUTEX_LOCK_BLOCK(_lock) {
            Header_t header;
            auto memPtr = _readMemory(header, 0);
            if (!memPtr) {
                output.println(F("RTC data not set or invalid"));
                return -1;
            }
            auto memUniquePtr = std::unique_ptr<uint8_t[]>(memPtr);
            output.printf_P(PSTR("RTC data length: %u\n"), header.data_length());
            output.printf_P(PSTR("RTC memory usage: %u\n"), header.length);

            DumpBinary dumper(output);
            auto ptr = header.begin(memPtr);
            auto endPtr = header.end(memPtr);
            int result = 0;
            while(ptr + sizeof(Entry_t) < endPtr) {
                auto entry = Entry_t(ptr);
                if (!entry) { // invalid id, NUL bytes for alignment
                    break;
                }
                ptr += sizeof(entry);
                if (ptr + entry.length >= endPtr) {
                    __LDBG_printf_E("read OOB id=0x%02x len=%d", entry.mem_id, entry.length);
                    break;
                }
                #if DEBUG_RTC_MEMORY_MANAGER
                {
                    PrintString out;
                    DumpBinary dumper(out);
                    dumper.dump(ptr, entry.length);
                    dumper.setPerLine(entry.length);
                    dumper.setGroupBytes(4);
                    __LDBG_printf("id=0x%02x len=%u data=%s", entry.mem_id, entry.length, out.c_str());
                }
                #endif
                if (displayId == RTCMemoryId::NONE || displayId == static_cast<RTCMemoryId>(entry.mem_id)) {
                    if (entry.length) {
                        result++;
                    }
                    output.printf_P(PSTR("RTC 0x%02x: (%s), length %d "), entry.mem_id, PluginComponent::getMemoryIdName(entry.mem_id), entry.length);
                    dumper.setGroupBytes(4);
                    dumper.setPerLine(entry.length);
                    dumper.dump(ptr, entry.length);
                }
                ptr += entry.length;
            }

            #if RTC_SUPPORT == 0 && RTC_SUPPORT_NO_TIMER == 0
                _rtcTimer.detach();
            #endif

            uint32_t start = micros();
            uint32_t heap = ESP.getFreeHeap();
            constexpr int kRepeats = 1000;
            for(int i = 0; i < kRepeats; i++) {
                storeTime();
            }
            uint32_t dur = micros() - start;
            int heapUsage = heap - ESP.getFreeHeap();

            output.printf_P(PSTR("RTC benchmark (%ux storeTime) time: %.3fms heap: %d\n"), kRepeats, dur / 1000.0, heapUsage);

            #if RTC_SUPPORT == 0 && RTC_SUPPORT_NO_TIMER == 0
                setupRTC();
            #endif


            __LDBG_printf("result=%d", result);
            return result;
        }
        __builtin_unreachable();
    }

#endif
