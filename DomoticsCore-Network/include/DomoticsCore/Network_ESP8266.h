#pragma once

#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiUdp.h>

namespace DomoticsCore { 
namespace HAL {

using NetworkClient = ::WiFiClient;
using SecureNetworkClient = ::BearSSL::WiFiClientSecure;
using NetworkServer = ::WiFiServer;
using NetworkUDP = ::WiFiUDP;
using IPAddress = ::IPAddress;
using WiFiClient = NetworkClient;
using WiFiServer = NetworkServer;

} // namespace HAL
} // namespace DomoticsCore
