#pragma once

#include <DomoticsCore/Platform_HAL.h>
#include <functional>

namespace DomoticsCore {
namespace Components {

class INetworkProvider {
public:
    virtual ~INetworkProvider() = default;
    virtual const char* getProviderId() const = 0;
    virtual bool isConnected() const = 0;
    virtual String getLocalIP() const = 0;
    virtual String getNetworkType() const = 0;
    virtual String getConnectionStatus() const = 0;
    virtual String getNetworkInfo() const = 0;
    virtual bool setRoutePriority(int priority) = 0;
    virtual void setConnectionCallback(std::function<void(bool)> callback) { (void)callback; }
    virtual int32_t getSignalStrength() const { return 0; }
    virtual String getMacAddress() const { return ""; }
};

} // namespace Components
} // namespace DomoticsCore
