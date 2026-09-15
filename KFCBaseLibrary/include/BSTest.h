/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include <Arduino_compat.h>

#define BS_ENV_DECLARE() ;

#define TEST_CASE(unit, name) Serial.printf_P(PSTR("TEST_CASE(\"%s\", \"%s\")\n"), unit, name); Serial.flush();

#define CHECK(x) Serial.printf_P(PSTR("CHECK(%s)\n"), x ? PSTR("TRUE") : PSTR("FALSE")); Serial.flush();

#define BS_RUN() ;

