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
    usb_.onUSBDeviceAvailable = [this](uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid) {
        onUSBDeviceAvailable(bus, port, vid, pid);
    };
    usb_.onUSBPhoneDetected = [this](uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid) {
        onUSBPhoneDetected(bus, port, vid, pid);
    };
    wireless_.onBtDeviceAvailable = [this](const std::string& deviceId, const std::string& btAddress) {
        onBtDeviceAvailable(deviceId, btAddress);
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
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& d : devices_) {
            if (d.id == deviceId) { target = d; found = true; break; }
        }
    }
    if (!found) {
        DM_LOG(warning) << "connectDevice: unknown id " << deviceId;
        return;
    }

    if (target.transport == "usb") {
        if (target.aoapReady) {
            // AOAP device already enumerated — create connection and fire callback
            auto connection = usb_.openDeviceConnection(target.vid, target.pid);
            if (!connection) {
                DM_LOG(error) << "Failed to create USB connection for " << deviceId;
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& d : devices_) {
                    if (d.id == deviceId) { d.status = "connected"; break; }
                }
            }
            DM_LOG(info) << "Connecting USB device " << deviceId;
            if (onDeviceReady) onDeviceReady(deviceId, std::move(connection));
            if (onDeviceListChanged) onDeviceListChanged();
        } else {
            // Phone needs AOAP setup first — auto-connect when it re-enumerates
            pendingUSBAutoConnect_.store(true);
            DM_LOG(info) << "Starting AOAP setup for " << deviceId;
            usb_.beginAoapSetup(deviceId);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& d : devices_) {
                    if (d.id == deviceId) { d.status = "connecting"; break; }
                }
            }
            if (onDeviceListChanged) onDeviceListChanged();
        }
    } else if (target.transport == "wireless") {
        if (target.pendingConnection) {
            // Connection already created — hand it off
            DeviceConnection::Pointer connection;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& d : devices_) {
                    if (d.id == deviceId) {
                        connection = std::move(d.pendingConnection);
                        d.status = "connected";
                        break;
                    }
                }
            }
            DM_LOG(info) << "Connecting wireless device " << deviceId;
            if (onDeviceReady) onDeviceReady(deviceId, std::move(connection));
            if (onDeviceListChanged) onDeviceListChanged();
        } else {
            // Need BT→WiFi handshake first
            pendingWifiAutoConnect_.store(true);
            DM_LOG(info) << "Starting WiFi projection for " << deviceId;
            wireless_.beginWifiProjection();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& d : devices_) {
                    if (d.id == deviceId) { d.status = "connecting"; break; }
                }
            }
            if (onDeviceListChanged) onDeviceListChanged();
        }
    }
}

void DeviceManager::disconnectDevice(const std::string& deviceId) {
    std::string transport;
    uint16_t vid = 0, pid = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = devices_.begin(); it != devices_.end(); ++it) {
            if (it->id == deviceId) {
                transport = it->transport;
                vid = it->vid;
                pid = it->pid;
                devices_.erase(it);
                break;
            }
        }
    }
    DM_LOG(info) << "Disconnecting device " << deviceId;
    if (onDeviceListChanged) onDeviceListChanged();

    if (transport == "wireless") {
        wireless_.reconnectBluetooth();
    } else if (transport == "usb" && vid && pid) {
        addTimer(200, [usb = &usb_, v = vid, p = pid] { usb->resetDevice(v, p); });
    }
}

// ── Sub-manager callbacks ── (all fire on the ELL thread)

void DeviceManager::onUSBDeviceAvailable(uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid) {
    std::string id = "usb:" + std::to_string(bus) + ":" + std::to_string(port);
    DM_LOG(info) << "AOAP device available: " << id
                 << " " << std::hex << vid << ":" << pid << std::dec;

    if (pendingUSBAutoConnect_.exchange(false)) {
        DM_LOG(info) << "Auto-connecting AOAP device " << id;

        // Small delay for udev to settle before opening the device
        addTimer(500, [this, id, vid, pid, bus, port] {
            auto connection = usb_.openDeviceConnection(vid, pid);
            if (connection) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    devices_.erase(
                        std::remove_if(devices_.begin(), devices_.end(),
                                       [](const DeviceEntry& d) { return d.transport == "usb"; }),
                        devices_.end());
                    devices_.push_back({id, "Android Auto (USB)", "usb", "connected",
                                       vid, pid, bus, port, true, nullptr, ""});
                }
                if (onDeviceReady) onDeviceReady(id, std::move(connection));
                if (onDeviceListChanged) onDeviceListChanged();
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
        devices_.erase(
            std::remove_if(devices_.begin(), devices_.end(),
                           [](const DeviceEntry& d) { return d.transport == "usb" && d.status != "connected"; }),
            devices_.end());
        devices_.push_back({id, "Android Auto (USB)", "usb", "available",
                           vid, pid, bus, port, true, nullptr, ""});
    }
}

void DeviceManager::onUSBPhoneDetected(uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid) {
    std::string id = "usb:" + std::to_string(bus) + ":" + std::to_string(port);
    DM_LOG(info) << "USB phone detected: " << id
                 << " " << std::hex << vid << ":" << pid << std::dec;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        devices_.erase(
            std::remove_if(devices_.begin(), devices_.end(),
                           [&id](const DeviceEntry& d) { return d.id == id; }),
            devices_.end());
        devices_.push_back({id, "Android Phone (USB)", "usb", "available",
                           vid, pid, bus, port, false, nullptr, ""});
    }
}

void DeviceManager::onBtDeviceAvailable(const std::string& deviceId, const std::string& btAddress) {
    DM_LOG(info) << "Wireless device available: " << deviceId;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // If this device is already connected or connecting, don't overwrite it.
        // BT can reconnect while WiFi is still active.
        for (const auto& d : devices_) {
            if (d.id == deviceId && (d.status == "connected" || d.status == "connecting")) {
                DM_LOG(info) << "Wireless device " << deviceId
                             << " already " << d.status << ", ignoring BT re-announce";
                return;
            }
        }

        devices_.erase(
            std::remove_if(devices_.begin(), devices_.end(),
                           [](const DeviceEntry& d) { return d.transport == "wireless"; }),
            devices_.end());
        devices_.push_back({deviceId, "Android Auto (Wireless - " + btAddress + ")",
                           "wireless", "available", 0, 0, 0, 0, false, nullptr, ""});
    }
}

void DeviceManager::onWifiClientConnected(const std::string& deviceId,
                                           DeviceConnection::Pointer connection) {
    DM_LOG(info) << "WiFi connection ready for " << deviceId;

    if (pendingWifiAutoConnect_.exchange(false)) {
        DM_LOG(info) << "Auto-connecting wireless device " << deviceId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& d : devices_) {
                if (d.id == deviceId) {
                    d.status = "connected";
                    d.pendingConnection = nullptr;
                    break;
                }
            }
        }
        if (onDeviceReady) onDeviceReady(deviceId, std::move(connection));
        if (onDeviceListChanged) onDeviceListChanged();
        return;
    }

    // WiFi arrived without pending connect — store connection for later
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& d : devices_) {
            if (d.id == deviceId) {
                d.pendingConnection = std::move(connection);
                break;
            }
        }
    }
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

