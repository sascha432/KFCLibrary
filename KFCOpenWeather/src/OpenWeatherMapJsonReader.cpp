/**
* Author: sascha_lammers@gmx.de
*/

#include "OpenWeatherMapJsonReader.h"

using namespace KFCJson;

OpenWeatherMapJsonReader::OpenWeatherMapJsonReader(Stream *stream, OpenWeatherMapAPI::WeatherInfo & info) :
    JsonBaseReader(stream),
    _info(info)
{
}

OpenWeatherMapJsonReader::OpenWeatherMapJsonReader(OpenWeatherMapAPI::WeatherInfo & info) :
    OpenWeatherMapJsonReader(nullptr, info)
{
}

bool OpenWeatherMapJsonReader::beginObject(bool isArray)
{
    if (getObjectPath(false) == F("daily[]")) {
        if (_info.limitReached) { // limit has been reached, ignore data
            return true;
        }
        if (_info.daily.size() == _info.limit) { // new daily record, check if we have reached the limit
            _info.limitReached = true; // do not read anymore
            return true;
        }
        _info.daily.emplace_back();
    }
    // auto pathStr = getObjectPath(false);
    // auto path = pathStr.c_str();
    // Serial.printf("begin path %s array=%u\n", path, isArray);
    return true;
}

bool OpenWeatherMapJsonReader::processElement() {

    auto key = getKey();
    auto path = getPath(false);

    // Serial.printf("key %s value %s type %s path %s index %d\n", key.c_str(), getValue().c_str(), (PGM_P)jsonType2String(getType()), path.c_str(), getObjectIndex());

    if (key == (F("timezone_offset"))) {
        _info.timezone = getIntValue();
    }
    else {
        bool getDescr = true;
        OpenWeatherMapAPI::Weather_t *item = nullptr;
        if (path.startsWith(F("daily[]."))) {
            if (!_info.limitReached) { // ignore data if limit has been reached
                item = &_info.daily.back();
                getDescr = _info.getDailyDescr;
            }
        }
        else if (path.startsWith(F("current."))) {
            item = &_info.current;
        }
        if (item) {
            if (key.equals(F("dt"))) {
                item->dt = getIntValue();
            }
            else if (key.equals(F("temp")) || path.endsWith(F("temp.day"))) {
                item->temperature = getFloatValue();
            }
            else if (key.equals(F("feels_like")) || path.endsWith(F("feels_like.day"))) {
                item->feels_like = getFloatValue();
            }
            else if (path.endsWith(F("temp.min"))) {
                item->temperature_min = getFloatValue();
                if (_info.daily.size() == 1) {
                    _info.current.temperature_min = item->temperature_min;
                }
            }
            else if (path.endsWith(F("temp.max"))) {
                item->temperature_max = getFloatValue();
                if (_info.daily.size() == 1) {
                    _info.current.temperature_max = item->temperature_max;
                }
            }
            else if (key.equals(F("pressure"))) {
                item->pressure = (uint16_t)getIntValue();
            }
            else if (key.equals(F("humidity"))) {
                item->humidity = (uint8_t)getIntValue();
            }
            else if (key.equals(F("wind_speed"))) {
                item->wind_speed = getFloatValue();
            }
            else if (key.equals(F("wind_deg"))) {
                item->wind_deg = (uint16_t)getIntValue();
            }
            else if (key.equals(F("sunrise"))) {
                item->sunrise = getIntValue();
            }
            else if (key.equals(F("sunset"))) {
                item->sunset = getIntValue();
            }
            else if (path.endsWith(F("weather[].description"))) {
                if (getDescr) {
                    item->descr = getValue();
                }
            }
            else if (path.endsWith(F("weather[].icon"))) {
                item->icon = getValue();
            }
        }
    }

    return true;
}

bool OpenWeatherMapJsonReader::recoverableError(JsonErrorEnum_t errorType) {
    return true;
}
