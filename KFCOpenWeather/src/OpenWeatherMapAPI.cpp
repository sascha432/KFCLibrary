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

String OpenWeatherMapAPI::getApiUrl() const
{
    String url = F(OPENWEATHERMAP_ONECALL_API_URL);
    url.replace(F("{lat}"), String(_lat, 6));
    url.replace(F("{lon}"), String(_long, 6));
    url.replace(F("{api_key}"), urlEncode(_apiKey));
    return url;
}

bool OpenWeatherMapAPI::hasApiKey() const
{
    return _apiKey.length() != 0;
}

bool OpenWeatherMapAPI::parseData(const String &data)
{
    // StreamString stream; // create a copy of data
    // stream.print(data);
    HeapStream stream(data); // use data directly
    return parseData(reinterpret_cast<Stream &>(stream));
}

bool OpenWeatherMapAPI::parseData(Stream &stream)
{
    __LDBG_printf("data=%u", stream.available());
    return OpenWeatherMapJsonReader(&stream, _info).parse();
}

JsonBaseReader *OpenWeatherMapAPI::getParser()
{
    auto parser = new OpenWeatherMapJsonReader(_info);
    parser->initParser();
    return parser;
}

void OpenWeatherMapAPI::WeatherInfo::dump(Print &output) const
{
    if (hasError()) {
        output.print(F("Error: "));
        output.print(error);
    }
    if (hasNoData()) {
        output.print(F("No Data"));

    }
    output.printf_P(PSTR(" Forecast: %u/%u/%d TZ: %d\n"), daily.size(), limit, limitReached, timezone);

    output.print(F("Current "));
    output.printf_P(PSTR("Temp.: %.1fC (%.1f) (min/max %.1f/%.1f) "), current.temperature, current.feels_like, current.temperature_min, current.temperature_max);
    output.printf_P(PSTR("Humidity: %d %% Press.: %d hPa"), current.humidity, current.pressure);
    output.printf_P(PSTR(" Descr: %s\n"), current.descr.c_str());

    int day = 0;
    for(auto const &current: daily) {
        output.printf_P(PSTR("Day #%d "), day++);
        output.printf_P(PSTR("Temp.: %.1fC (%.1f) (min/max %.1f/%.1f) "), current.temperature, current.feels_like, current.temperature_min, current.temperature_max);
        output.printf_P(PSTR("Humidity: %d %% Press.: %d hPa"), current.humidity, current.pressure);
        if (getDailyDescr) {
            output.printf_P(PSTR(" Descr: %s\n"), current.descr.c_str());
        }
        else {
            output.println();
        }
    }
}

void OpenWeatherMapAPI::dump(Print &output) const
{
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
