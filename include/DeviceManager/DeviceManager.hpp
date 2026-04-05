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

// DeviceManager — unified device discovery for Android Auto.
//
// Composes USBDeviceManager (USB hotplug, AOAP) and WirelessDeviceManager
// (Bluetooth, TCP WiFi) behind a single interface. All callbacks and
// operations are forwarded to the appropriate sub-manager.

#include <cstdint>
#include <functional>
#include <string>
#include <DeviceManager/USBDeviceManager.hpp>
#include <DeviceManager/WirelessDeviceManager.hpp>

struct libusb_context;

// Configuration for DeviceManager — pass before calling start().
struct DeviceManagerConfig {
    bool bluetoothEnabled = true;            // Register BlueZ profile for WiFi projection
    std::string bluetoothAdapterAddress;     // empty = auto-detect first adapter
    std::string wifiInterface;               // empty = auto-detect wireless interface
    std::string wifiSSID = "OpenAutoAP";
    std::string wifiPassword = "OpenAutoPass123";
    uint16_t wifiPort = 5000;                // TCP listen port announced to phone
};

class DeviceManager {
public:
    explicit DeviceManager(DeviceManagerConfig config = {});
    ~DeviceManager();

    void start();
    void stop();

    // Expose the libusb context so consumers can create USB objects on it.
    libusb_context* usbContext() const;

    // ── USB operations ──
    void beginAoapSetup(const std::string& deviceId);
    void rescanUSB();

    // ── Wireless operations ──
    void beginWifiProjection();
    void reconnectBluetooth();

    // ── Sub-manager access ──
    USBDeviceManager&      usb()      { return usb_; }
    WirelessDeviceManager& wireless() { return wireless_; }

    // ── Callbacks (forwarded to sub-managers in start()) ──
    std::function<void(uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid)> onUSBDeviceAvailable;
    std::function<void(uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid)> onUSBPhoneDetected;
    std::function<void(const std::string& deviceId, int fd, const std::string& peerAddress)> onWifiClientConnected;
    std::function<void(const std::string& deviceId, const std::string& btAddress)> onBtDeviceAvailable;

private:
    USBDeviceManager usb_;
    WirelessDeviceManager wireless_;
};
