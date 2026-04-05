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

#include <memory>
#include <vector>
#include <libusb.h>
#include <DeviceManager/IDevice.hpp>

// AOAP USB device. Wraps libusb async bulk transfers on IN/OUT endpoints.
// Created via USBDevice::create() after AOAP handshake and re-enumeration.
class USBDevice : public IDevice {
public:
    USBDevice(DeviceInfo info, libusb_device_handle* handle,
              uint8_t inEndpoint, uint8_t outEndpoint, int interfaceNumber);
    ~USBDevice() override;

    const DeviceInfo& info() const override { return info_; }

    void send(const uint8_t* data, size_t size,
              SendHandler onComplete, ErrorHandler onError) override;
    void receive(uint8_t* buffer, size_t maxSize,
                 ReceiveHandler onData, ErrorHandler onError) override;
    void stop() override;

    // Open AOAP device, discover endpoints, claim interface.
    // Returns nullptr on failure.
    static std::unique_ptr<USBDevice> create(DeviceInfo info, libusb_device* device);

private:
    void submitSend();
    static void onSendComplete(libusb_transfer* transfer);
    static void onReceiveComplete(libusb_transfer* transfer);

    DeviceInfo info_;
    libusb_device_handle* handle_;
    uint8_t inEndpoint_;
    uint8_t outEndpoint_;
    int interfaceNumber_;

    // Pending send
    std::vector<uint8_t> sendBuffer_;
    size_t sendOffset_ = 0;
    SendHandler pendingSendComplete_;
    ErrorHandler pendingSendError_;
    libusb_transfer* sendTransfer_ = nullptr;

    // Pending receive
    ReceiveHandler pendingRecvData_;
    ErrorHandler pendingRecvError_;
    libusb_transfer* recvTransfer_ = nullptr;
};
