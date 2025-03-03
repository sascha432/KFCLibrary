/**
* Author: sascha_lammers@gmx.de
*/

#pragma once

#if ASYNC_TCP_SSL_ENABLED || KFC_REST_API_USE_HTTP_CLIENT
#    define OPENWEATHERMAP_ONECALL_API_SCHEME "https"
#else
#    define OPENWEATHERMAP_ONECALL_API_SCHEME "http"
#endif

#ifndef OPENWEATHERMAP_ONECALL_API_URL
#    define OPENWEATHERMAP_ONECALL_API_URL OPENWEATHERMAP_ONECALL_API_SCHEME "://api.openweathermap.org/data/3.0/onecall?exclude=minutely,hourly,alerts&units=metric&lat={lat}&lon={lon}&appid={api_key}"
#endif

#include <Arduino_compat.h>
#include <JsonBaseReader.h>
#include <map>

#ifndef DEBUG_OPENWEATHERMAPAPI
#    define DEBUG_OPENWEATHERMAPAPI 0
#endif

#if DEBUG_OPENWEATHERMAPAPI
#    include "debug_helper_enable.h"
#else
#    include "debug_helper_disable.h"
#endif

class OpenWeatherMapAPI {
public:
    struct Weather_t {
        time_t dt;
        float temperature;
        float feels_like;
        float temperature_min;
        float temperature_max;
        uint8_t humidity;
        uint16_t pressure;
        time_t sunrise;
        time_t sunset;
        float wind_speed;
        uint16_t wind_deg;
        String descr;
        String icon;

        Weather_t() :
            dt(0),
            temperature(NAN),
            feels_like(NAN),
            temperature_min(NAN),
            temperature_max(NAN),
            humidity(0),
            pressure(0),
            sunrise(0),
            sunset(0),
            wind_speed(NAN),
            wind_deg(0)
        {
        }

        // reset to defaults
        void clear()
        {
            *this = Weather_t();
        }
    };

    #define WEATHER_INFO_NO_DATA_YET_FSTR F("No weather data")

    class WeatherInfo {
    public:
        static constexpr uint32_t kDailyLimit = 31;

        WeatherInfo() :
            error(WEATHER_INFO_NO_DATA_YET_FSTR),
            limit(kDailyLimit),
            limitReached(false),
            getDailyDescr(true)
        {
        }

        time_t getSunRiseAsGMT() const;
        time_t getSunSetAsGMT() const;

        // debug dump data
        void dump(Print &output) const;

        // clear data
        void clear()
        {
            __LDBG_printf("size=%u", daily.size());
            error = WEATHER_INFO_NO_DATA_YET_FSTR;
            timezone = 0;
            current.clear();
            daily.clear();
            limitReached = false;
        }

        // returns true if the data has not been loaded yet
        bool hasNoData() const
        {
            return (error == WEATHER_INFO_NO_DATA_YET_FSTR);
        }

        // returns true if valid data is present
        bool hasData() const
        {
            return !hasError();
        }

        // report error
        // use hasNoData() to determine if an attempt was made to load data
        bool hasError() const
        {
            return daily.empty();
        }

        // get error message
        // use hasError() to determine if an occur has occurred
        const String &getError() const
        {
            return error;
        }

        // set error message
        void setError(const String &message)
        {
            clear(); // clear data to indicate an error has occurred
            error = message;
        }

    public:
        String error;
        int32_t timezone;   // timezone of the weather data
        Weather_t current;  // current weather info
        std::vector<OpenWeatherMapAPI::Weather_t> daily; // daily weather info, starting with today
        uint16_t limit;
        bool limitReached;
        bool getDailyDescr;
    };

    OpenWeatherMapAPI();
    OpenWeatherMapAPI(const String &apiKey);

    // clear data
    void clear();

    // set API key
    void setAPIKey(const String &key);
    // set location
    void setLatitude(double lat);
    // set location
    void setLongitude(double lng);
    // set max. daily forecast to keep, must be >= 1
    void setDailyLimit(uint32_t limit);
    // set to true to read the description for forecasts
    void setGetDailyDescr(bool value);

    // get API url
    String getApiUrl() const;
    bool hasApiKey() const;

    // parse data
    // returns false on EOF, until then more data can be fed. the minimum size is one byte
    bool parseData(const String &data);

    // parse data
    // returns false on EOF, until then more data can be fed. the minimum size is one byte
    bool parseData(Stream &stream);

    // create parser object dynamically. needs to be freed with delete
    KFCJson::JsonBaseReader *getParser();

    // return weather data
    WeatherInfo &getInfo();

    // debug print all data and crcs
    void dump(Print &output) const;

    // helpers
    static float CtoF(float temp);
    static float kmToMiles(float km);

private:
    double _lat;
    double _long;
    String _apiKey;
    WeatherInfo _info;
};

//
// OpenWeatherMapAPI
//

inline OpenWeatherMapAPI::OpenWeatherMapAPI()
{
}

inline OpenWeatherMapAPI::OpenWeatherMapAPI(const String &apiKey) :
    _apiKey(apiKey)
{
}

inline void OpenWeatherMapAPI::clear()
{
    _info.clear();
}

inline void OpenWeatherMapAPI::setAPIKey(const String &key)
{
    _apiKey = key;
}

inline void OpenWeatherMapAPI::setLatitude(double lat)
{
    _lat = lat;
}

inline void OpenWeatherMapAPI::setLongitude(double lon)
{
    _long = lon;
}

inline void OpenWeatherMapAPI::setDailyLimit(uint32_t limit)
{
    _info.limit = limit;
}

inline void OpenWeatherMapAPI::setGetDailyDescr(bool value)
{
    _info.getDailyDescr = value;
}

inline OpenWeatherMapAPI::WeatherInfo &OpenWeatherMapAPI::getInfo()
{
    return _info;
}

inline float OpenWeatherMapAPI::CtoF(float temp)
{
    return temp * (9.0f / 5.0f) + 32;
}

inline float OpenWeatherMapAPI::kmToMiles(float km)
{
    return km / 1.60934f;
}

//
// OpenWeatherMapAPI::WeatherInfo
//

inline time_t OpenWeatherMapAPI::WeatherInfo::getSunRiseAsGMT() const
{
    return current.sunrise + timezone;
}

inline time_t OpenWeatherMapAPI::WeatherInfo::getSunSetAsGMT() const
{
    return current.sunset + timezone;
}

#if DEBUG_OPENWEATHERMAPAPI
#    include "debug_helper_disable.h"
#endif
