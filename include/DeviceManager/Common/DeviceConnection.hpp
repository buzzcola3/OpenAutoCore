// Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
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
#include <memory>
#include <string>

// DeviceConnection — universal byte-stream device abstraction.
//
// Wraps either a USB AOAP device (libusb bulk transfers) or a TCP socket
// (ELL fd watches). DeviceManager delivers a DeviceConnection when a device
// is ready; the consumer can either use the raw I/O (send/read) directly,
// or extract the underlying resource for bridging to legacy stacks.
//
// I/O is not active until start() is called.

class DeviceConnection {
public:
    using Pointer = std::shared_ptr<DeviceConnection>;
    using ReadCallback = std::function<void(const uint8_t* data, size_t len)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    enum class Type { USB, TCP };

    virtual ~DeviceConnection() = default;

    virtual Type type() const = 0;

    // Start/stop the read loop. I/O is not active until start() is called.
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void send(const uint8_t* data, size_t len) = 0;

    void setReadCallback(ReadCallback cb) { readCallback_ = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { errorCallback_ = std::move(cb); }
    void setDisconnectCallback(ErrorCallback cb) { disconnectCallback_ = std::move(cb); }

protected:
    void fireError(const std::string& error) {
        if (errorCallback_) errorCallback_(error);
        if (disconnectCallback_) disconnectCallback_(error);
    }

    ReadCallback readCallback_;
    ErrorCallback errorCallback_;
    ErrorCallback disconnectCallback_;
};
