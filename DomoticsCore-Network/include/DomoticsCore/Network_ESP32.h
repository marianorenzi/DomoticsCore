#pragma once

#if __has_include(<Network.h>)
#include <Network.h>
#include <NetworkClient.h>
#include <NetworkServer.h>
#include <NetworkUdp.h>
#else
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#endif

#if __has_include(<NetworkClientSecure.h>)
#include <NetworkClientSecure.h>
#define DOMOTICS_HAS_GENERIC_SECURE_CLIENT 1
#elif __has_include(<WiFiClientSecure.h>)
#include <WiFiClientSecure.h>
#define DOMOTICS_HAS_WIFI_SECURE_CLIENT 1
#endif

namespace DomoticsCore { 
namespace HAL {
    
#if __has_include(<Network.h>)
using NetworkClient = ::NetworkClient;
using NetworkServer = ::NetworkServer;
using NetworkUDP = ::NetworkUDP;
#else
using NetworkClient = ::WiFiClient;
using SecureNetworkClient = ::WiFiClientSecure;
using NetworkServer = ::WiFiServer;
using NetworkUDP = ::WiFiUDP;
#endif
using IPAddress = ::IPAddress;
#if defined(DOMOTICS_HAS_GENERIC_SECURE_CLIENT)
using SecureNetworkClient = ::NetworkClientSecure;
#elif defined(DOMOTICS_HAS_WIFI_SECURE_CLIENT)
using SecureNetworkClient = ::WiFiClientSecure;
#endif
using WiFiClient = NetworkClient;
using WiFiServer = NetworkServer;

} // namespace HAL
} // namespace DomoticsCore

#undef DOMOTICS_HAS_GENERIC_SECURE_CLIENT
#undef DOMOTICS_HAS_WIFI_SECURE_CLIENT
