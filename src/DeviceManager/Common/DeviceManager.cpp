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

#include <algorithm>
#include <nlohmann/json.hpp>
#include <DeviceManager/Common/DeviceManager.hpp>
#include <DeviceManager/Common/DmLog.hpp>

using json = nlohmann::json;

// ── Construction / lifetime ──

DeviceManager::DeviceManager(DeviceManagerConfig config)
    : usb_(std::move(config.aoapConfig)),
      wireless_(
                {config.bluetoothEnabled, config.bluetoothAdapterAddress,
                 config.wifiInterface, config.wifiSSID, config.wifiPassword,
                 config.wifiPort}) {}

DeviceManager::~DeviceManager() {
    stop();
}

void DeviceManager::start() {
    usb_.onUSBDeviceAvailable = [this](uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid,
                                       const std::string& name) {
        onUSBDeviceAvailable(bus, port, vid, pid, name);
    };
    usb_.onUSBPhoneDetected = [this](uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid,
                                     const std::string& name) {
        onUSBPhoneDetected(bus, port, vid, pid, name);
    };
    wireless_.onBtDeviceAvailable = [this](const std::string& deviceId, const std::string& btAddress,
                                           const std::string& name) {
        onBtDeviceAvailable(deviceId, btAddress, name);
    };
    wireless_.onWifiClientConnected = [this](const std::string& deviceId,
                                              DeviceConnection::Pointer connection) {
        onWifiClientConnected(deviceId, std::move(connection));
    };

    usb_.start();
    wireless_.start();
    DM_LOG(info) << "DeviceManager: started";
}

void DeviceManager::stop() {
    usb_.stop();
    wireless_.stop();
}

// ── Device list ──

std::string DeviceManager::getDeviceListJson() const {
    json arr = json::array();
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& d : devices_) {
        arr.push_back({
            {"id",          d.id},
            {"displayName", d.displayName},
            {"transport",   d.transport},
            {"status",      d.status}
        });
    }
    return arr.dump();
}

// ── Connection control ──

void DeviceManager::connectDevice(const std::string& deviceId) {
    DeviceEntry target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        DeviceEntry* d = find(deviceId);
        if (!d) {
            DM_LOG(warning) << "connectDevice: unknown id " << deviceId;
            return;
        }
        target = *d;
    }

    if (target.transport == "usb") {
        if (target.aoapReady) {
            // AOAP device already enumerated — create connection and fire callback
            auto connection = usb_.openDeviceConnection(target.vid, target.pid);
            if (!connection) {
                DM_LOG(error) << "Failed to create USB connection for " << deviceId;
                return;
            }
            setStatus(deviceId, "connected");
            DM_LOG(info) << "Connecting USB device " << deviceId;
            deliverConnection(deviceId, std::move(connection));
        } else {
            // Phone needs AOAP setup first — auto-connect when it re-enumerates
            pendingUSBAutoConnect_.store(true);
            DM_LOG(info) << "Starting AOAP setup for " << deviceId;
            usb_.beginAoapSetup(deviceId);
            setStatus(deviceId, "connecting");
            notifyListChanged();
        }
    } else if (target.transport == "wireless") {
        if (target.pendingConnection) {
            // Connection already created — hand it off
            DeviceConnection::Pointer connection;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (DeviceEntry* d = find(deviceId)) {
                    connection = std::move(d->pendingConnection);
                    d->status = "connected";
                }
            }
            DM_LOG(info) << "Connecting wireless device " << deviceId;
            deliverConnection(deviceId, std::move(connection));
        } else {
            // Need BT→WiFi handshake first
            pendingWifiAutoConnect_.store(true);
            DM_LOG(info) << "Starting WiFi projection for " << deviceId;
            wireless_.beginWifiProjection();
            setStatus(deviceId, "connecting");
            notifyListChanged();
        }
    }
}

void DeviceManager::disconnectDevice(const std::string& deviceId) {
    std::string transport;
    uint16_t vid = 0, pid = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (DeviceEntry* d = find(deviceId)) {
            transport = d->transport;
            vid = d->vid;
            pid = d->pid;
            removeWhere([&](const DeviceEntry& e) { return e.id == deviceId; });
        }
    }
    DM_LOG(info) << "Disconnecting device " << deviceId;
    notifyListChanged();

    if (transport == "wireless") {
        wireless_.reconnectBluetooth();
    } else if (transport == "usb" && vid && pid) {
        addTimer(200, [usb = &usb_, v = vid, p = pid] { usb->resetDevice(v, p); });
    }
}

// ── Sub-manager callbacks ── (all fire on the ELL thread)

void DeviceManager::onUSBDeviceAvailable(uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid,
                                         const std::string& name) {
    std::string id = "usb:" + std::to_string(bus) + ":" + std::to_string(port);
    std::string displayName = name.empty() ? "Android Auto (USB)" : name + " (USB)";
    DM_LOG(info) << "AOAP device available: " << id
                 << " " << std::hex << vid << ":" << pid << std::dec
                 << " (" << displayName << ")";

    if (pendingUSBAutoConnect_.exchange(false)) {
        DM_LOG(info) << "Auto-connecting AOAP device " << id;

        // Small delay for udev to settle before opening the device
        addTimer(500, [this, id, displayName, vid, pid, bus, port] {
            auto connection = usb_.openDeviceConnection(vid, pid);
            if (connection) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    removeWhere([](const DeviceEntry& d) { return d.transport == "usb"; });
                    devices_.push_back(makeUsbEntry(id, displayName, "connected",
                                                    vid, pid, bus, port, true));
                }
                deliverConnection(id, std::move(connection));
            } else {
                DM_LOG(error) << "Failed to open auto-connect AOAP device";
                pendingUSBAutoConnect_.store(true);
            }
        });
        return;
    }

    // Not auto-connecting — just add to the device list
    {
        std::lock_guard<std::mutex> lock(mutex_);
        removeWhere([](const DeviceEntry& d) { return d.transport == "usb" && d.status != "connected"; });
        devices_.push_back(makeUsbEntry(id, displayName, "available",
                                        vid, pid, bus, port, true));
    }
    notifyListChanged();
}

void DeviceManager::onUSBPhoneDetected(uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid,
                                       const std::string& name) {
    std::string id = "usb:" + std::to_string(bus) + ":" + std::to_string(port);
    std::string displayName = name.empty() ? "Android Phone (USB)" : name + " (USB)";
    DM_LOG(info) << "USB phone detected: " << id
                 << " " << std::hex << vid << ":" << pid << std::dec
                 << " (" << displayName << ")";

    {
        std::lock_guard<std::mutex> lock(mutex_);
        removeWhere([&id](const DeviceEntry& d) { return d.id == id; });
        devices_.push_back(makeUsbEntry(id, displayName, "available",
                                        vid, pid, bus, port, false));
    }
    notifyListChanged();
}

void DeviceManager::onBtDeviceAvailable(const std::string& deviceId, const std::string& btAddress,
                                        const std::string& name) {
    DM_LOG(info) << "Wireless device available: " << deviceId;

    std::string displayName = name.empty()
        ? "Android Auto (Wireless - " + btAddress + ")"
        : name + " (Wireless)";

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // If this device is already connected or connecting, don't overwrite it.
        // BT can reconnect while WiFi is still active.
        if (DeviceEntry* d = find(deviceId);
            d && (d->status == "connected" || d->status == "connecting")) {
            DM_LOG(info) << "Wireless device " << deviceId
                         << " already " << d->status << ", ignoring BT re-announce";
            return;
        }

        removeWhere([](const DeviceEntry& d) { return d.transport == "wireless"; });
        devices_.push_back(makeWirelessEntry(deviceId, displayName, "available"));
    }
    notifyListChanged();
}

void DeviceManager::onWifiClientConnected(const std::string& deviceId,
                                           DeviceConnection::Pointer connection) {
    DM_LOG(info) << "WiFi connection ready for " << deviceId;

    if (pendingWifiAutoConnect_.exchange(false)) {
        DM_LOG(info) << "Auto-connecting wireless device " << deviceId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (DeviceEntry* d = find(deviceId)) {
                d->status = "connected";
                d->pendingConnection = nullptr;
            }
        }
        deliverConnection(deviceId, std::move(connection));
        return;
    }

    // WiFi arrived without pending connect — store connection for later
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (DeviceEntry* d = find(deviceId)) {
            d->pendingConnection = std::move(connection);
        }
    }
}

// ── Connection error handling ──

void DeviceManager::onConnectionError(const std::string& deviceId,
                                      const std::string& error) {
    DM_LOG(info) << "Connection error for " << deviceId << ": " << error;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (DeviceEntry* d = find(deviceId)) {
            d->status = "available";
            d->pendingConnection = nullptr;
        }
    }
    notifyListChanged();
}

// ── Registry helpers ──

DeviceManager::DeviceEntry* DeviceManager::find(const std::string& deviceId) {
    for (auto& d : devices_) {
        if (d.id == deviceId) return &d;
    }
    return nullptr;
}

void DeviceManager::setStatus(const std::string& deviceId, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (DeviceEntry* d = find(deviceId)) d->status = status;
}

void DeviceManager::notifyListChanged() {
    if (onDeviceListChanged) onDeviceListChanged();
}

void DeviceManager::deliverConnection(const std::string& deviceId,
                                      DeviceConnection::Pointer connection) {
    if (connection) {
        connection->setDisconnectCallback([this, deviceId](const std::string& error) {
            onConnectionError(deviceId, error);
        });
    }
    if (onDeviceReady) onDeviceReady(deviceId, std::move(connection));
    notifyListChanged();
}

DeviceManager::DeviceEntry DeviceManager::makeUsbEntry(
    const std::string& id, const std::string& name, const std::string& status,
    uint16_t vid, uint16_t pid, uint8_t bus, uint8_t port, bool aoapReady) {
    DeviceEntry e;
    e.id = id;
    e.displayName = name;
    e.transport = "usb";
    e.status = status;
    e.vid = vid;
    e.pid = pid;
    e.bus = bus;
    e.port = port;
    e.aoapReady = aoapReady;
    return e;
}

DeviceManager::DeviceEntry DeviceManager::makeWirelessEntry(
    const std::string& id, const std::string& name, const std::string& status) {
    DeviceEntry e;
    e.id = id;
    e.displayName = name;
    e.transport = "wireless";
    e.status = status;
    return e;
}

// ── pollDevices / timers ──

void DeviceManager::pollDevices() {
    usb_.pollDevices();
    wireless_.pollDevices();
    executeTimers();
}

void DeviceManager::addTimer(unsigned long ms, TimerCallback cb) {
    timers_.push_back({std::chrono::steady_clock::now() + std::chrono::milliseconds(ms),
                       std::move(cb)});
}

void DeviceManager::executeTimers() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = timers_.begin(); it != timers_.end(); ) {
        if (now >= it->deadline) {
            it->callback();
            it = timers_.erase(it);
        } else {
            ++it;
        }
    }
}

