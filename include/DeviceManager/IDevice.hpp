// Copyright (C) 2024 CubeOne (Simon Dean - simon.dean@cubeone.co.uk)
//
// This file is part of OpenAutoCore.
//
// OpenAutoCore is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// OpenAutoCore is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <DeviceManager/DeviceInfo.hpp>

// Abstract raw-byte I/O device. USBDevice and WifiDevice implement this.
// All methods must be called from the ELL main loop thread.
class IDevice {
public:
    using SendHandler    = std::function<void()>;
    using ReceiveHandler = std::function<void(size_t bytesRead)>;
    using ErrorHandler   = std::function<void(const std::string& reason)>;

    virtual ~IDevice() = default;
    virtual const DeviceInfo& info() const = 0;

    // Async send. Copies data internally; caller may free buffer after call returns.
    virtual void send(const uint8_t* data, size_t size,
                      SendHandler onComplete, ErrorHandler onError) = 0;

    // Async receive. Caller owns buffer and must keep it alive until callback.
    virtual void receive(uint8_t* buffer, size_t maxSize,
                         ReceiveHandler onData, ErrorHandler onError) = 0;

    virtual void stop() = 0;
};
