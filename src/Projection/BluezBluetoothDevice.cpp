/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#include <Projection/BluezBluetoothDevice.hpp>
#include <Common/Log.hpp>
#include <algorithm>

namespace f1x::openauto::autoapp::projection {

using VariantMap = std::map<std::string, sdbus::Variant>;
using InterfaceMap = std::map<std::string, VariantMap>;
using ManagedObjects = std::map<sdbus::ObjectPath, InterfaceMap>;

namespace {
    std::string toLower(const std::string& input) {
        std::string out = input;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return out;
    }
}

BluezBluetoothDevice::BluezBluetoothDevice(std::string adapterAddress)
    : adapterAddress_(std::move(adapterAddress)),
      bus_(sdbus::createSystemBusConnection()),
      objectManager_(sdbus::createProxy(*bus_, "org.bluez", "/")) {
    objectManager_->finishRegistration();
}

void BluezBluetoothDevice::stop() {
}

bool BluezBluetoothDevice::isPaired(const std::string& address) const {
    return getDevicePaired(address);
}

std::string BluezBluetoothDevice::getAdapterAddress() const {
    return adapterAddress_;
}

bool BluezBluetoothDevice::isAvailable() const {
    if (!bus_) {
        return false;
    }
    return !resolveAdapterPath().empty();
}

std::string BluezBluetoothDevice::resolveAdapterPath() const {
    if (!objectManager_) {
        return "/org/bluez/hci0";
    }

    ManagedObjects managed;
    objectManager_->callMethod("GetManagedObjects")
        .onInterface("org.freedesktop.DBus.ObjectManager")
        .storeResultsTo(managed);

    for (const auto& entry : managed) {
        const auto& path = entry.first;
        const auto& interfaces = entry.second;
        auto adapterIt = interfaces.find("org.bluez.Adapter1");
        if (adapterIt == interfaces.end()) {
            continue;
        }

        if (adapterAddress_.empty()) {
            return path;
        }

        const auto& props = adapterIt->second;
        auto addrIt = props.find("Address");
        if (addrIt == props.end()) {
            continue;
        }

        const auto adapterAddr = addrIt->second.get<std::string>();
        if (toLower(adapterAddr) == toLower(adapterAddress_)) {
            return path;
        }
    }

    return "/org/bluez/hci0";
}

bool BluezBluetoothDevice::getDevicePaired(const std::string& deviceAddress) const {
    if (!objectManager_) {
        return false;
    }

    ManagedObjects managed;
    objectManager_->callMethod("GetManagedObjects")
        .onInterface("org.freedesktop.DBus.ObjectManager")
        .storeResultsTo(managed);

    const auto target = toLower(deviceAddress);

    for (const auto& entry : managed) {
        const auto& interfaces = entry.second;
        auto deviceIt = interfaces.find("org.bluez.Device1");
        if (deviceIt == interfaces.end()) {
            continue;
        }

        const auto& props = deviceIt->second;
        auto addrIt = props.find("Address");
        if (addrIt == props.end()) {
            continue;
        }

        const auto addr = toLower(addrIt->second.get<std::string>());
        if (addr != target) {
            continue;
        }

        auto pairedIt = props.find("Paired");
        if (pairedIt == props.end()) {
            return false;
        }
        return pairedIt->second.get<bool>();
    }

    return false;
}

}
