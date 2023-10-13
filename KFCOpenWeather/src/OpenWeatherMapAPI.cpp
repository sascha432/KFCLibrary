/**
* Author: sascha_lammers@gmx.de
*/

#include "OpenWeatherMapAPI.h"
#include "OpenWeatherMapJsonReader.h"
// #include <StreamString.h>
#include <HeapStream.h>
#include <misc.h>

#if DEBUG_OPENWEATHERMAPAPI
#    include "debug_helper_enable.h"
#else
#    include "debug_helper_disable.h"
#endif

OpenWeatherMapAPI::OpenWeatherMapAPI()
{
}

OpenWeatherMapAPI::OpenWeatherMapAPI(const String &apiKey) : _apiKey(apiKey)
{
}

void OpenWeatherMapAPI::clear()
{
    _info.clear();
}

void OpenWeatherMapAPI::setAPIKey(const String &key)
{
    _apiKey = key;
}

void OpenWeatherMapAPI::setLatitude(double lat)
{
    _lat = lat;
}

void OpenWeatherMapAPI::setLongitude(double lon)
{
    _long = lon;
}

String OpenWeatherMapAPI::getApiUrl() const
{
    String url = F(OPENWEATHERMAP_ONECALL_API_URL);
    url.replace(F("{lat}"), String(_lat, 6));
    url.replace(F("{lon}"), String(_long, 6));
    url.replace(F("{api_key}"), urlEncode(_apiKey));
    return url;
}

bool OpenWeatherMapAPI::parseData(const String &data)
{
    // StreamString stream;
    // stream.print(data);
    HeapStream stream(data);
    _info.clear();
    return parseData(reinterpret_cast<Stream &>(stream));
}

bool OpenWeatherMapAPI::parseData(Stream &stream)
{
    __LDBG_printf("data=%u", stream.available());
    #if DEBUG_OPENWEATHERMAPAPI
        _info._updated = time(nullptr);
    #endif
    return OpenWeatherMapJsonReader(&stream, _info).parse();
}

JsonBaseReader *OpenWeatherMapAPI::getParser()
{
    auto parser = new OpenWeatherMapJsonReader(_info);
    parser->initParser();
    return parser;
}

OpenWeatherMapAPI::WeatherInfo &OpenWeatherMapAPI::getWeatherInfo()
{
    return _info;
}

time_t OpenWeatherMapAPI::WeatherInfo::getSunRiseAsGMT() const
{
    return current.sunrise + timezone;
}

time_t OpenWeatherMapAPI::WeatherInfo::getSunSetAsGMT() const
{
    return current.sunset + timezone;
}

void OpenWeatherMapAPI::WeatherInfo::dump(Print &output) const
{
    #if DEBUG_OPENWEATHERMAPAPI
        output.printf_P(PSTR("Updated: " TIME_T_FMT "\n"), _updated);
    #endif
    output.printf_P(PSTR("Error: %s\n"), error.c_str());
    output.printf_P(PSTR("Timezone: %d\n"), timezone);

    output.printf_P(PSTR("Temperature: %.1fC (%.1f) (min/max %.1f/%.1f)\n"), current.temperature, current.feels_like, current.temperature_min, current.temperature_max);
    output.printf_P(PSTR("Humidity: %d %%\n"), current.humidity);
    output.printf_P(PSTR("Pressure: %d hPa\n"), current.pressure);
    output.printf_P(PSTR("Descr: %s\n"), current.descr.c_str());

    int day = 0;
    for(auto const &current: daily) {
        output.printf_P(PSTR("Day #%d:\n"), day++);
        output.printf_P(PSTR("Temperature: %.1fC (%.1f) (min/max %.1f/%.1f)\n"), current.temperature, current.feels_like, current.temperature_min, current.temperature_max);
        output.printf_P(PSTR("Humidity: %d %%\n"), current.humidity);
        output.printf_P(PSTR("Pressure: %d hPa\n"), current.pressure);
        output.printf_P(PSTR("Descr: %s\n"), current.descr.c_str());
    }
}

void OpenWeatherMapAPI::dump(Print &output) const
{
    #if DEBUG_OPENWEATHERMAPAPI
        __LDBG_printf("updated=" TIME_T_FMT, _info._updated);
    #endif
    if (_info.hasData()) {
        _info.dump(output);
    }
    else {
        if (_info.hasError()) {
            output.println(_info.getError());
        }
        else {
            output.println(F("No data available"));
        }
    }
}

float OpenWeatherMapAPI::CtoF(float temp)
{
    return temp * (9.0f / 5.0f) + 32;
}

float OpenWeatherMapAPI::kmToMiles(float km)
{
    return km / 1.60934f;
}

