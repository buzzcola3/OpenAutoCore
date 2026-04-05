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

#include <DeviceManager/DeviceManager.hpp>
#include <DeviceManager/DmLog.hpp>

DeviceManager::DeviceManager(DeviceManagerConfig config)
    : wireless_({config.bluetoothEnabled, config.bluetoothAdapterAddress,
                 config.wifiInterface, config.wifiSSID, config.wifiPassword,
                 config.wifiPort}) {}

DeviceManager::~DeviceManager() {
    stop();
}

void DeviceManager::start() {
    // Wire callbacks from sub-managers to our public callbacks
    usb_.onUSBDeviceAvailable = [this](uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid) {
        if (onUSBDeviceAvailable) onUSBDeviceAvailable(bus, port, vid, pid);
    };
    usb_.onUSBPhoneDetected = [this](uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid) {
        if (onUSBPhoneDetected) onUSBPhoneDetected(bus, port, vid, pid);
    };
    wireless_.onWifiClientConnected = [this](const std::string& deviceId, int fd, const std::string& peerAddress) {
        if (onWifiClientConnected) onWifiClientConnected(deviceId, fd, peerAddress);
    };
    wireless_.onBtDeviceAvailable = [this](const std::string& deviceId, const std::string& btAddress) {
        if (onBtDeviceAvailable) onBtDeviceAvailable(deviceId, btAddress);
    };

    usb_.start();
    wireless_.start();
    DM_LOG(info) << "DeviceManager: started";
}

void DeviceManager::stop() {
    usb_.stop();
    wireless_.stop();
}

libusb_context* DeviceManager::usbContext() const {
    return usb_.usbContext();
}

void DeviceManager::beginAoapSetup(const std::string& deviceId) {
    usb_.beginAoapSetup(deviceId);
}

void DeviceManager::rescanUSB() {
    usb_.rescanUSB();
}

void DeviceManager::beginWifiProjection() {
    wireless_.beginWifiProjection();
}

void DeviceManager::reconnectBluetooth() {
    wireless_.reconnectBluetooth();
}

