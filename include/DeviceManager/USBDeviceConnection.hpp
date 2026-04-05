// Copyright (C) 2024 CubeOne (Simon Dean - simon.dean@cubeone.co.uk)
//
// This file is part of OpenAutoCore.

#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <libusb.h>
#include <DeviceManager/DeviceConnection.hpp>

// USBDeviceConnection — byte-stream over USB AOAP bulk endpoints.
//
// Discovers the AOAP interface and bulk IN/OUT endpoints on construction.
// start() claims the interface and launches a read thread; stop() joins it.
// send() performs synchronous bulk OUT transfers (thread-safe via libusb).
//
// For the AASDK bridge path, call deviceHandle()/usbContext() without
// calling start() — the bridge stack does its own endpoint setup.

class USBDeviceConnection : public DeviceConnection {
public:
    USBDeviceConnection(std::shared_ptr<libusb_device_handle> handle,
                        libusb_context* context);
    ~USBDeviceConnection() override;

    Type type() const override { return Type::USB; }

    void start() override;
    void stop() override;
    void send(const uint8_t* data, size_t len) override;

    // Raw access for AASDK bridge (temporary)
    std::shared_ptr<libusb_device_handle> deviceHandle() const { return handle_; }
    libusb_context* usbContext() const { return context_; }

private:
    bool discoverEndpoints();
    void readLoop();

    std::shared_ptr<libusb_device_handle> handle_;
    libusb_context* context_;
    uint8_t inEndpoint_ = 0;
    uint8_t outEndpoint_ = 0;
    int interfaceNum_ = -1;
    bool claimed_ = false;
    std::atomic_bool reading_{false};
    std::thread readThread_;
};
