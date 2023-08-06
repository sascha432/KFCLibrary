/**
* Author: sascha_lammers@gmx.de
*/

#pragma once

#include "Configuration.h"
#include "JsonConfigReader.h"

inline Configuration::~Configuration()
{
    clear();
    #if defined(HAVE_NVS_FLASH)
        _nvs_close();
        #ifdef KFC_CFG_NVS_PARTITION_NAME
            _Timer(_nvsDeinitTimer).remove();
            if (_nvsHavePartitionInitialized) {
                esp_err_t err = nvs_flash_deinit_partition(KFC_CFG_NVS_PARTITION_NAME);
                if (err != ESP_OK) {
                    __DBG_printf_E("failed to deinit NVS partition=%s err=%08x", KFC_CFG_NVS_PARTITION_NAME, err);
                }
            }
        #endif
    #endif
}

inline void Configuration::clear()
{
    __LDBG_printf("params=%u", _params.size());
    _params.clear();
}

inline void Configuration::discard()
{
    // do not allow writes during discard
    MUTEX_LOCK_BLOCK(_writeLock) {
        __LDBG_printf("discard params=%u", _params.size());
        for(auto &parameter: _params) {
            ConfigurationHelper::deallocate(parameter);
        }
        _readAccess = 0;

        #if defined(HAVE_NVS_FLASH)
            _nvs_close();
        #endif
    }
}

inline bool Configuration::read()
{
    // do not allow writes during read
    MUTEX_LOCK_BLOCK(_writeLock) {
        __LDBG_printf("params=%u", _params.size());
        clear();
        #if DEBUG_CONFIGURATION_GETHANDLE
            ConfigurationHelper::readHandles();
        #endif
        if (!_readParams()) {
            __LDBG_printf("readParams()=false");
            clear();
            return false;
        }
    }
    return true;
}

inline const char *Configuration::getString(HandleType handle)
{
    __LDBG_printf("handle=%04x", handle);
    uint16_t offset;
    auto param = _findParam(ParameterType::STRING, handle, offset);
    if (param == _params.end()) {
        return emptyString.c_str();
    }
    auto result = param->getString(*this, offset);;
    if (!result) {
        __DBG_panic("handle=%04x string=%p", result);
    }
    return result;
}

inline const uint8_t *Configuration::getBinary(HandleType handle, size_type &length)
{
    __LDBG_printf("handle=%04x", handle);
    uint16_t offset;
    auto param = _findParam(ParameterType::BINARY, handle, offset);
    if (param == _params.end()) {
        length = 0;
        return nullptr;
    }
    return param->getBinary(*this, length, offset);
}

inline void Configuration::makeWriteable(ConfigurationParameter &param, size_type length)
{
    #if DEBUG_CONFIGURATION
        delay(1);
    #endif
    param._makeWriteable(*this, length);
}

inline char *Configuration::getWriteableString(HandleType handle, size_type maxLength)
{
    __LDBG_printf("handle=%04x max_len=%u", handle, maxLength);
    auto &param = getWritableParameter<char *>(handle, maxLength);
    return param._getParam().string();
}

inline void *Configuration::getWriteableBinary(HandleType handle, size_type length)
{
    __LDBG_printf("handle=%04x len=%u", handle, length);
    auto &param = getWritableParameter<void *>(handle, length);
    return param._getParam().data();
}

inline void Configuration::_setString(HandleType handle, const char *string, size_type length, size_type maxLength)
{
    __LDBG_printf("handle=%04x length=%u max_len=%u", handle, length, maxLength);
    if (maxLength != (size_type)~0U) {
        if (length >= maxLength - 1) {
            length = maxLength - 1;
        }
    }
    _setString(handle, string, length);
}

inline void Configuration::_setString(HandleType handle, const char *string, size_type length)
{
    __LDBG_printf("handle=%04x length=%u str='%s'#%u", handle, length, string, strlen_P(string));
    uint16_t offset;
    auto &param = _getOrCreateParam(ParameterType::STRING, handle, offset);

    #if DEBUG
        // TODO debug code, this should not happen
        auto len = strlen_P(string);
        if (length == 1 && len == 0) {
            // this is allowed for empty strings
        }
        else if (len < length) {
            __DBG_printf_E("handle=%04x str size changed %u!=%u", handle, length, len);
            length = len;
        }
    #endif

    param.setData(*this, reinterpret_cast<const uint8_t *>(string), length);
}

inline void Configuration::setBinary(HandleType handle, const void *data, size_type length)
{
    __LDBG_printf("handle=%04x data=%p length=%u", handle, data, length);
    uint16_t offset;
    auto &param = _getOrCreateParam(ParameterType::BINARY, handle, offset);
    param.setData(*this, reinterpret_cast<const uint8_t *>(data), length);
}

inline bool Configuration::isDirty()
{
    for(auto &param: _params) {
        if (param.isWriteable() && param.hasDataChanged(*this)) {
            return true;
        }
    }
    return false;
}

inline bool Configuration::importJson(Stream &stream, HandleType *handles)
{
    KFCJson::JsonConfigReader reader(&stream, *this, handles);
    reader.initParser();
    return reader.parseStream();
}

#include "ConfigurationHelper.hpp"
#include "ConfigurationParameter.hpp"
#include "WriteableData.hpp"
