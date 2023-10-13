/**
* Author: sascha_lammers@gmx.de
*/

#pragma once

#ifndef OPENWEATHERMAP_ONECALL_API_URL
#    define OPENWEATHERMAP_ONECALL_API_URL "http://api.openweathermap.org/data/3.0/onecall?exclude=minutely,hourly,alerts&units=metric&lat={lat}&lon={lon}&appid={api_key}"
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
        {}

        void clear() {
            *this = Weather_t();
        }
    };

    #define WEATHER_INFO_NO_DATA_YET_FSTR F("No weather data")

    class WeatherInfo {
    public:

        WeatherInfo() : error(WEATHER_INFO_NO_DATA_YET_FSTR) {}

        time_t getSunRiseAsGMT() const;
        time_t getSunSetAsGMT() const;
        void dump(Print &output) const;

        // returns true if the data has not been loaded yet
        bool noData() const
        {
            return (error == WEATHER_INFO_NO_DATA_YET_FSTR);
        }

        // returns true if valid data is present
        bool hasData() const
        {
            return hasError() == false && daily.size();
        }

        void clear()
        {
            __LDBG_printf("size=%u", daily.size());
            *this = WeatherInfo();
        }

        bool hasError() const {
            return daily.empty();
        }

        const String &getError() const {
            return error;
        }

        void setError(const String &message) {
            clear();
            error = message;
            current.clear();
            daily.clear();
        }

    public:
        String error;
        int32_t timezone;
        Weather_t current;
        std::vector<OpenWeatherMapAPI::Weather_t> daily;
        #if DEBUG_OPENWEATHERMAPAPI
            time_t _updated{0};
        #endif
    };

    OpenWeatherMapAPI();
    OpenWeatherMapAPI(const String &apiKey);

    void clear();

    void setAPIKey(const String &key);
    void setLatitude(double lat);
    void setLongitude(double lng);

    String getApiUrl() const;
    String getWeatherApiUrl() const;
    String getForecastApiUrl() const;

    bool parseData(const String &data);
    bool parseData(Stream &stream);

    KFCJson::JsonBaseReader *getParser();

    WeatherInfo &getWeatherInfo();

    void dump(Print &output) const;

    static float CtoF(float temp);
    static float kmToMiles(float km);

private:
    double _lat;
    double _long;
    String _apiKey;
public:
    WeatherInfo _info;
};

#if DEBUG_OPENWEATHERMAPAPI
#    include "debug_helper_disable.h"
#endif
