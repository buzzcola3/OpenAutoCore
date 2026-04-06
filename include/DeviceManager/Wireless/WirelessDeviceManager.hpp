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

// WirelessDeviceManager — Bluetooth WiFi projection and TCP listener.
//
// Owns the BlueZ D-Bus profile, BT read loop, WiFi handshake protocol,
// and TCP listener for incoming WiFi connections. Commands from other
// threads are dispatched via eventfd and drained in pollDevices().

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <DeviceManager/Common/DeviceConnection.hpp>

struct l_dbus;

struct WirelessDeviceManagerConfig {
    bool bluetoothEnabled = true;
    std::string bluetoothAdapterAddress;
    std::string wifiInterface;
    std::string wifiSSID = "OpenAutoAP";
    std::string wifiPassword = "OpenAutoPass123";
    uint16_t wifiPort = 5000;
};

class WirelessDeviceManager {
public:
    explicit WirelessDeviceManager(WirelessDeviceManagerConfig config = {});
    ~WirelessDeviceManager();

    void start();
    void stop();

    /// Poll for queued commands and incoming TCP connections.
    /// Call this periodically from a scheduler (e.g. every 1ms).
    void pollDevices();

    // Thread-safe: queues command and wakes ELL thread.
    void beginWifiProjection();
    void reconnectBluetooth();

    // ── Callbacks (set before start()) ──

    // WiFi client connected — connection is ready to use.
    // deviceId is the same stable ID reported by onBtDeviceAvailable.
    std::function<void(const std::string& deviceId,
                       DeviceConnection::Pointer connection)> onWifiClientConnected;

    // Bluetooth device paired and connected (before WiFi handshake).
    // deviceId: stable reproducible ID ("wireless:<BT_MAC>").
    // btAddress: colon-separated BT MAC (e.g. "AA:BB:CC:DD:EE:FF").
    std::function<void(const std::string& deviceId, const std::string& btAddress)> onBtDeviceAvailable;

private:
    // ── Command queue ──
    enum class Command { BeginWifi, ReconnectBt };
    struct PendingCommand { Command type; };
    void drainCommands();
    void wakeEventFd();

    void doBeginWifiProjection();
    void doReconnectBluetooth();

    // ── TCP Listener ──
    void setupTCPListener(uint16_t port);
    void teardownTCPListener();
    void pollTCPListener();

    // ── Bluetooth WiFi Projection ──
    bool setupBluetooth();
    void teardownBluetooth();
    std::string resolveAdapterPath(const std::string& address);
    bool setAdapterProperty(const std::string& path, const std::string& name,
                            char sig, const void* value);
    void onBtNewConnection(int fd, const std::string& devicePath);
    void onBtDisconnection(const std::string& devicePath);
    void startBtReadLoop();
    void stopBtReadLoop(bool fromReader);
    void btReadLoop();
    void sendBtFrame(uint16_t messageId, const uint8_t* payload, size_t len);
    void handleWifiInfoRequest();
    void handleWifiVersionResponse(const uint8_t* payload, size_t len);
    void handleWifiStartResponse(const uint8_t* payload, size_t len);
    void handleWifiConnectionStatus(const uint8_t* payload, size_t len);

    struct WifiInterfaceInfo { std::string name; std::string ip; };
    WifiInterfaceInfo getWifiInterfaceInfo() const;
    std::string getMacAddress(const std::string& intf) const;

    // D-Bus profile callbacks
    static void setupProfileInterface(struct l_dbus_interface* iface);
    static struct l_dbus_message* onProfileRelease(struct l_dbus*, struct l_dbus_message*, void*);
    static struct l_dbus_message* onProfileNewConnection(struct l_dbus*, struct l_dbus_message*, void*);
    static struct l_dbus_message* onProfileDisconnection(struct l_dbus*, struct l_dbus_message*, void*);

    // ── State ──
    WirelessDeviceManagerConfig config_;
    bool running_ = false;

    // Command dispatch via eventfd
    int eventFd_ = -1;
    std::mutex mutex_;
    std::vector<PendingCommand> commandQueue_;

    // TCP listener
    int listenFd_ = -1;

    // Bluetooth state
    struct l_dbus* bus_ = nullptr;
    std::string adapterPath_;
    int btSocketFd_ = -1;
    std::string btDevicePath_;
    std::string btAddress_;   // colon-separated MAC of connected BT device
    std::string deviceId_;    // stable "wireless:<MAC>" for current BT device
    std::atomic_bool btReading_{false};
    std::thread btReaderThread_;
    std::vector<uint8_t> btBuffer_;
    std::string wifiInterfaceName_;
};
