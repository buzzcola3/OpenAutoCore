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

#include <vector>
#include <DeviceManager/IDevice.hpp>

struct l_io;

// TCP-connected Android Auto device (WiFi projection).
// Wraps a non-blocking socket fd with ELL l_io for async I/O.
class WifiDevice : public IDevice {
public:
    WifiDevice(DeviceInfo info, int socketFd);
    ~WifiDevice() override;

    const DeviceInfo& info() const override { return info_; }

    void send(const uint8_t* data, size_t size,
              SendHandler onComplete, ErrorHandler onError) override;
    void receive(uint8_t* buffer, size_t maxSize,
                 ReceiveHandler onData, ErrorHandler onError) override;
    void stop() override;

private:
    static bool onWriteReady(struct l_io* io, void* userData);
    static bool onReadReady(struct l_io* io, void* userData);
    static void onDisconnect(struct l_io* io, void* userData);

    void trySend();

    DeviceInfo info_;
    int fd_;
    struct l_io* io_ = nullptr;

    // Pending send
    std::vector<uint8_t> sendBuffer_;
    size_t sendOffset_ = 0;
    SendHandler pendingSendComplete_;
    ErrorHandler pendingSendError_;

    // Pending receive
    uint8_t* recvBuffer_ = nullptr;
    size_t recvMaxSize_ = 0;
    ReceiveHandler pendingRecvData_;
    ErrorHandler pendingRecvError_;

    bool stopped_ = false;
};
