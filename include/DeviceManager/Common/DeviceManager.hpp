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

// DeviceManager — unified device discovery and connection for Android Auto.
//
// Owns the device registry, composes USBDeviceManager and WirelessDeviceManager,
// and handles all connect/disconnect logic. Consumers just call connectDevice(id)
// and receive a ready-to-use DeviceConnection via the onDeviceReady callback.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <DeviceManager/Wired/USBDeviceManager.hpp>
#include <DeviceManager/Wireless/WirelessDeviceManager.hpp>

struct DeviceManagerConfig {
    AoapConfig aoapConfig;
    bool bluetoothEnabled = true;
    std::string bluetoothAdapterAddress;
    std::string wifiInterface;
    std::string wifiSSID = "OpenAutoAP";
    std::string wifiPassword = "OpenAutoPass123";
    uint16_t wifiPort = 5000;
};

class DeviceManager {
public:
    explicit DeviceManager(DeviceManagerConfig config = {});
    ~DeviceManager();

    void start();
    void stop();

    /// Poll all sub-managers and execute pending timers.
    /// Call this periodically from a scheduler (e.g. every 1ms).
    void execute();

    // ── Device list (JSON for FE) ──
    std::string getDeviceListJson() const;

    // ── Connection control ──
    void connectDevice(const std::string& deviceId);
    void disconnectDevice(const std::string& deviceId);

    // ── Callbacks (set before start()) ──

    // Device is ready — connection is ready to use for Android Auto session.
    std::function<void(const std::string& deviceId,
                       DeviceConnection::Pointer connection)> onDeviceReady;

    // Device list has changed (status update for FE push during connect flow).
    std::function<void()> onDeviceListChanged;

private:
    struct DeviceEntry {
        std::string id;
        std::string displayName;
        std::string transport;   // "usb" | "wireless"
        std::string status = "available";
        // USB
        uint16_t vid = 0, pid = 0;
        uint8_t bus = 0, port = 0;
        bool aoapReady = false;
        // Wireless
        DeviceConnection::Pointer pendingConnection;
        std::string peerAddress;
    };

    void onUSBDeviceAvailable(uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid);
    void onUSBPhoneDetected(uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid);
    void onBtDeviceAvailable(const std::string& deviceId, const std::string& btAddress);
    void onWifiClientConnected(const std::string& deviceId,
                               DeviceConnection::Pointer connection);

    // Timer support (replaces ELL l_timeout)
    using TimerCallback = std::function<void()>;
    struct PendingTimer {
        std::chrono::steady_clock::time_point deadline;
        TimerCallback callback;
    };
    void addTimer(unsigned long ms, TimerCallback cb);
    void executeTimers();

    mutable std::mutex mutex_;
    std::vector<DeviceEntry> devices_;
    std::atomic_bool pendingUSBAutoConnect_{false};
    std::atomic_bool pendingWifiAutoConnect_{false};

    USBDeviceManager usb_;
    WirelessDeviceManager wireless_;
    std::vector<PendingTimer> timers_;
};
