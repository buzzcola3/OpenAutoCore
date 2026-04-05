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

// DeviceManager — unified device discovery and raw I/O for Android Auto.
//
// Architecture:
//   DeviceManager owns USB hotplug detection, AOAP setup, and a TCP listener.
//   Discovered devices (USB or WiFi) appear in the device list. The consumer
//   calls connect(id) to activate a device, then uses send()/receive() for
//   raw byte I/O. The underlying transport (USB bulk or TCP socket) is hidden
//   behind the IDevice interface.
//
//   All I/O and callbacks run on the ELL main loop thread — no strands, no
//   mutexes, no Boost.Asio. libusb fds are polled via ELL l_io watches.
//
// Typical flow:
//   1. dm.onDeviceFound = [](auto& info) { /* update UI */ };
//   2. dm.onConnected   = []() { /* start SSL/AA session */ };
//   3. dm.start();
//   4. (hotplug fires) → AOAP setup → re-enumeration → onDeviceFound
//   5. dm.connect(id);   → onConnected
//   6. dm.send(...) / dm.receive(...)   ← raw AOAP/TCP byte stream
//   7. dm.disconnect();  → onDisconnected

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <libusb.h>
#include <DeviceManager/DeviceInfo.hpp>
#include <DeviceManager/IDevice.hpp>

struct l_dbus;
struct l_io;
struct l_timeout;

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

    // ── Scanning ──
    void start();   // Begin USB hotplug monitoring + TCP listener
    void stop();    // Tear down everything and disconnect

    // Expose the libusb context so consumers can create USB objects on it.
    libusb_context* usbContext() const { return usbContext_; }

    // ── Device list ──
    std::vector<DeviceInfo> getDevices() const;

    // ── Connection ──
    void connect(const std::string& deviceId);
    void disconnect();

    // ── Raw I/O (delegates to connected device) ──
    void send(const uint8_t* data, size_t size,
              IDevice::SendHandler onComplete, IDevice::ErrorHandler onError);
    void receive(uint8_t* buffer, size_t maxSize,
                 IDevice::ReceiveHandler onData, IDevice::ErrorHandler onError);

    // ── Events ──
    std::function<void(const DeviceInfo&)> onDeviceFound;
    std::function<void(const std::string& id)> onDeviceLost;
    std::function<void()> onConnected;
    std::function<void(const std::string& reason)> onDisconnected;
    // Called when a WiFi client connects (after BT handshake). Provides raw fd.
    // The consumer takes ownership of the fd (e.g. wraps in Boost socket).
    std::function<void(int fd, const std::string& peerAddress)> onWifiClientConnected;
    // Called when an AOAP-ready USB device appears. Provides VID/PID so the
    // consumer can open the device on its own libusb context.
    std::function<void(uint16_t vid, uint16_t pid)> onUSBDeviceAvailable;

private:
    // ── USB Hotplug ──
    static int onHotplugEvent(libusb_context* ctx, libusb_device* device,
                              libusb_hotplug_event event, void* userData);
    void handleUSBDevice(libusb_device* device);
    bool isAOAPDevice(const libusb_device_descriptor& desc) const;
    bool shouldSkipDevice(libusb_device* device, const libusb_device_descriptor& desc) const;
    void setupHotplugWakeup();
    void teardownHotplugWakeup();
    static bool onHotplugWakeup(struct l_io* io, void* userData);
    void drainHotplugQueue();

    // ── AOAP Setup State Machine ──
    // One per non-AOAP device being switched into accessory mode.
    struct AoapSetup {
        DeviceManager* manager;
        libusb_device_handle* handle;
        libusb_transfer* transfer;
        std::vector<uint8_t> buffer;
        int state; // 0=protocol, 1-6=strings, 7=start
        std::string deviceId;
    };
    void startAoapSetup(libusb_device* device, const std::string& id);
    void advanceAoapSetup(AoapSetup* setup);
    static void onAoapTransferDone(libusb_transfer* transfer);
    void cleanupAoapSetup(AoapSetup* setup);

    // ── libusb fd integration with ELL ──
    void registerLibusbFds();
    void unregisterLibusbFds();
    static void onLibusbFdAdded(int fd, short events, void* userData);
    static void onLibusbFdRemoved(int fd, void* userData);
    static bool onLibusbFdReady(struct l_io* io, void* userData);
    void handleLibusbEvents();

    // ── TCP Listener ──
    void setupTCPListener(uint16_t port);
    void teardownTCPListener();
    static bool onTCPAcceptable(struct l_io* io, void* userData);

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
    libusb_context* usbContext_ = nullptr;
    libusb_hotplug_callback_handle hotplugHandle_ = 0;
    bool hotplugRegistered_ = false;
    std::map<int, struct l_io*> libusbWatches_;

    // Cross-thread hotplug wakeup: hotplug callback queues devices from USB
    // worker threads, then signals the ELL thread via eventfd.
    int hotplugEventFd_ = -1;
    struct l_io* hotplugWakeupIo_ = nullptr;
    std::mutex hotplugMutex_;
    std::vector<libusb_device*> hotplugQueue_;

    int listenFd_ = -1;
    struct l_io* listenIo_ = nullptr;

    std::map<std::string, std::unique_ptr<IDevice>> devices_;
    std::list<std::unique_ptr<AoapSetup>> aoapSetups_;

    IDevice* connectedDevice_ = nullptr;
    bool running_ = false;

    // Bluetooth WiFi projection state
    DeviceManagerConfig config_;
    struct l_dbus* bus_ = nullptr;
    std::string adapterPath_;
    int btSocketFd_ = -1;
    std::atomic_bool btReading_{false};
    std::thread btReaderThread_;
    std::vector<uint8_t> btBuffer_;
    std::string wifiInterfaceName_;

    static constexpr uint16_t kGoogleVendorId = 0x18D1;
    static constexpr uint16_t kAOAPId         = 0x2D00;
    static constexpr uint16_t kAOAPWithAdbId  = 0x2D01;
};
