/**
* Author: sascha_lammers@gmx.de
*/

#include "OpenWeatherForecastJsonReader.h"

using namespace KFCJson;

OpenWeatherForecastJsonReader::OpenWeatherForecastJsonReader(Stream * stream, OpenWeatherMapAPI::WeatherForecast &forecast) : JsonBaseReader(stream), _forecast(forecast)
{
}

OpenWeatherForecastJsonReader::OpenWeatherForecastJsonReader(OpenWeatherMapAPI::WeatherForecast &forecast) : OpenWeatherForecastJsonReader(nullptr, forecast)
{
}

bool OpenWeatherForecastJsonReader::beginObject(bool isArray)
{
    auto pathStr = getObjectPath(false);
    auto path = pathStr.c_str();
    //Serial.printf("begin path %s array=%u\n", path, isArray);

    if (!strcmp_P(path, PSTR("list[]"))) {
        _item = OpenWeatherMapAPI::Forecast_t();
        _itemKey = String();
    }
    else if (!strcmp_P(path, PSTR("list[].weather[]"))) {
        _item.weather.emplace_back();
    }

    return true;
}

bool OpenWeatherForecastJsonReader::endObject()
{
    auto pathStr = getObjectPath(false);
    auto path = pathStr.c_str();
    //Serial.printf("end path %s\n", path);

    if (!strcmp_P(path, PSTR("list[]"))) {

        // get min/max/rain% temperature per day
        for(auto &info: _forecast.forecast) {
            if (info.first == _itemKey) {
                auto &item = info.second;
                item.val.temperature_max = std::max(item.val.temperature_max, _item.val.temperature_max);
                item.val.temperature_min = std::min(item.val.temperature_min, _item.val.temperature_min);
                item.val.rain = (item.val.rain + _item.val.rain) / 2.0f;
                return true;
            }
        }
        // first entry
        _forecast.forecast.emplace(_itemKey, std::move(_item));
    }
    //if (getLevel() == 1) {
    //    _forecast.updateKeys();
    //}
    return true;
}

bool OpenWeatherForecastJsonReader::processElement()
{
    auto keyStr = getKey();
    auto key = keyStr.c_str();
    auto pathStr = JsonBaseReader::getPath(false);
    auto path = pathStr.c_str();

    //Serial.printf("key %s value %s type %s path %s index %d\n", key, getValue().c_str(), jsonType2String(getType()).c_str(), path, getObjectIndex());

    if (!strncmp_P(path, PSTR("city."), 5)) {
        if (!strcmp_P(key, PSTR("name"))) {
            _forecast.city = getValue();
        }
        else if (!strcmp_P(key, PSTR("country"))) {
            _forecast.country = getValue();
        }
        else if (!strcmp_P(key, PSTR("timezone"))) {
            _forecast.val.timezone = getValue().toInt();
        }
    }
    else if (!strncmp_P(path, PSTR("list[]."), 7)) {
        if (!strncmp_P(path + 7, PSTR("weather[]."), 10)) {
            auto &item = _item.weather.back();
            // if (!strcmp_P(key, PSTR("main"))) {
            //     item.main = getValue();
            // }
            // else if (!strcmp_P(key, PSTR("description"))) {
            //     item.descr = getValue();
            // }
            // else
            if (!strcmp_P(key, PSTR("icon"))) {
                item.icon = getValue();
            }
            else if (!strcmp_P(key, SPGM(id))) {
                item.id = (uint16_t)getIntValue();;
            }
        }
        else if (!strncmp_P(path + 7, PSTR("rain."), 5)) {
            if (!strcmp_P(key, PSTR("3h"))) {
                _item.val.rain = getFloatValue();
            }
        }
        else if (!strncmp_P(path + 7, PSTR("main."), 5)) {
            if (!strcmp_P(key, PSTR("temp"))) {
                _item.val.temperature = getFloatValue();
            }
            else if (!strcmp_P(key, PSTR("temp_min"))) {
                _item.val.temperature_min = getFloatValue();
            }
            else if (!strcmp_P(key, PSTR("temp_max"))) {
                _item.val.temperature_max = getFloatValue();
            }
            // else if (!strcmp_P(key, PSTR("pressure"))) {
            //     _item.val.pressure = (uint16_t)getIntValue();
            // }
            // else if (!strcmp_P(key, PSTR("humidity"))) {
            //     _item.val.humidity = (uint8_t)getIntValue();
            // }
        }
        else if (!strcmp_P(key, PSTR("dt"))) {
            _item.val.time = getIntValue();
        }
        else if (!strcmp_P(key, PSTR("dt_txt"))) {
            _itemKey = getValue().substring(0, 10);
        }
    }
    return true;
}

bool OpenWeatherForecastJsonReader::recoverableError(JsonErrorEnum_t errorType) {
    return true;
}
