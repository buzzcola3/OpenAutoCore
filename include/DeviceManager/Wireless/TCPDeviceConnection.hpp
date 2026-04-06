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

#include <atomic>
#include <thread>
#include <DeviceManager/Common/DeviceConnection.hpp>

// TCPDeviceConnection — byte-stream over a TCP socket fd.
//
// Owns the fd by default; start() launches a dedicated read thread,
// stop() joins it. send() performs synchronous write(). releaseFd()
// transfers ownership to the caller (used by the AASDK bridge path to
// hand the fd to a boost socket).

class TCPDeviceConnection : public DeviceConnection {
public:
    explicit TCPDeviceConnection(int fd);
    ~TCPDeviceConnection() override;

    Type type() const override { return Type::TCP; }

    void start() override;
    void stop() override;
    void send(const uint8_t* data, size_t len) override;

    // Raw access for AASDK bridge (temporary)
    int fd() const { return fd_; }

    // Release fd ownership (prevents close on destruction).
    int releaseFd();

private:
    void readLoop();

    int fd_;
    bool owning_ = true;
    std::atomic_bool reading_{false};
    std::thread readThread_;
};
