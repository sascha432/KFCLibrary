/**
* Author: sascha_lammers@gmx.de
*/

#pragma once

#include <Arduino_compat.h>
#include <JsonBaseReader.h>
#include "OpenWeatherMapAPI.h"

using namespace KFCJson;

class OpenWeatherMapJsonReader : public JsonBaseReader {
public:
    OpenWeatherMapJsonReader(Stream *stream, OpenWeatherMapAPI::WeatherInfo &info);
    OpenWeatherMapJsonReader(OpenWeatherMapAPI::WeatherInfo &info);

    virtual bool beginObject(bool isArray);
    virtual bool processElement();
    virtual bool recoverableError(JsonErrorEnum_t errorType);

private:
    OpenWeatherMapAPI::WeatherInfo &_info;
};

inline OpenWeatherMapJsonReader::OpenWeatherMapJsonReader(Stream *stream, OpenWeatherMapAPI::WeatherInfo &info) :
    JsonBaseReader(stream),
    _info(info)
{
}

inline OpenWeatherMapJsonReader::OpenWeatherMapJsonReader(OpenWeatherMapAPI::WeatherInfo &info) :
    OpenWeatherMapJsonReader(nullptr, info)
{
}

inline bool OpenWeatherMapJsonReader::recoverableError(JsonErrorEnum_t errorType)
{
    return true;
}
