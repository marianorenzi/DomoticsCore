#pragma once

#include <DomoticsCore/Platform_HAL.h>

#if DOMOTICS_PLATFORM_ESP32
#include <DomoticsCore/Network_ESP32.h>
#elif DOMOTICS_PLATFORM_ESP8266
#include <DomoticsCore/Network_ESP8266.h>
#else
#include <DomoticsCore/Network_Stub.h>
#endif
