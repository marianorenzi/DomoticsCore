#pragma once

#include <cstdint>

namespace DomoticsCore { 
namespace HAL {
    
class IPAddress {
    uint32_t address_ = 0;
public:
    IPAddress() = default;
    IPAddress(uint32_t address) : address_(address) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : address_((uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | d) {}
    operator uint32_t() const { return address_; }
    bool operator==(const IPAddress& other) const { return address_ == other.address_; }
    bool operator!=(const IPAddress& other) const { return !(*this == other); }
    uint8_t operator[](int index) const {
        return index >= 0 && index < 4 ? uint8_t(address_ >> ((3 - index) * 8)) : 0;
    }
    uint32_t toUInt32() const { return address_; }
};

} // namespace HAL
} // namespace DomoticsCore
