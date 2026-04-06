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

// USBDeviceManager — USB hotplug detection, AOAP setup, device listing.
//
// Owns the libusb context and all USB-related state. Hotplug events from
// the libusb worker thread are marshalled via eventfd and drained in
// pollDevices(). Non-AOAP Android phones are held in pendingPhones_ until
// the consumer requests AOAP setup via beginAoapSetup().

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
#include <DeviceManager/Common/DeviceConnection.hpp>
#include <DeviceManager/Wired/AoapConfig.hpp>

class USBDeviceManager {
public:
    explicit USBDeviceManager(AoapConfig aoapConfig = {});
    ~USBDeviceManager();

    void start();
    void stop();

    /// Poll for hotplug events, AOAP completions, and queued commands.
    /// Call this periodically from a scheduler (e.g. every 1ms).
    void pollDevices();

    // Open an AOAP-ready device and create a DeviceConnection for it.
    DeviceConnection::Pointer openDeviceConnection(uint16_t vid, uint16_t pid);

    // Reset a USB device out of AOAP mode.
    void resetDevice(uint16_t vid, uint16_t pid);

    // Thread-safe: queues command and wakes ELL thread.
    void beginAoapSetup(const std::string& deviceId);
    void rescanUSB();

    // ── Callbacks (set before start()) ──

    // AOAP-ready device appeared (after AOAP re-enumeration).
    std::function<void(uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid)> onUSBDeviceAvailable;

    // Non-AOAP Android phone detected (scan only, no AOAP started).
    std::function<void(uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid)> onUSBPhoneDetected;

private:
    // ── USB Hotplug ──
    static int onHotplugEvent(libusb_context* ctx, libusb_device* device,
                              libusb_hotplug_event event, void* userData);
    void handleUSBDevice(libusb_device* device);
    bool isAOAPDevice(const libusb_device_descriptor& desc) const;
    bool shouldSkipDevice(libusb_device* device, const libusb_device_descriptor& desc) const;
    void drainQueue();

    // ── AOAP Setup State Machine ──
    struct AoapSetup {
        USBDeviceManager* manager;
        libusb_device_handle* handle;
        libusb_transfer* transfer;
        std::vector<uint8_t> buffer;
        int state;
        std::string deviceId;
    };
    void startAoapSetup(libusb_device* device, const std::string& id);
    void advanceAoapSetup(AoapSetup* setup);
    static void onAoapTransferDone(libusb_transfer* transfer);
    void cleanupAoapSetup(AoapSetup* setup);

    // ── Command queue ──
    enum class Command { BeginAoap, RescanUSB };
    struct PendingCommand { Command type; std::string arg; };
    void doBeginAoapSetup(const std::string& deviceId);
    void doRescanUSB();
    void wakeEventFd();

    // ── Event thread (pumps libusb for async transfers) ──
    void startEventThread();
    void stopEventThread();

    // ── State ──
    libusb_context* usbContext_ = nullptr;
    libusb_hotplug_callback_handle hotplugHandle_ = 0;
    bool hotplugRegistered_ = false;
    std::atomic_bool running_{false};
    std::thread eventThread_;

    // Cross-thread wakeup: hotplug callback + commands queue from any thread,
    // then signal the ELL thread via eventfd.
    int eventFd_ = -1;
    std::mutex mutex_;
    std::vector<libusb_device*> hotplugQueue_;
    std::vector<AoapSetup*> completedAoapQueue_;
    std::vector<PendingCommand> commandQueue_;

    std::list<std::unique_ptr<AoapSetup>> aoapSetups_;
    std::map<std::string, libusb_device*> pendingPhones_;

    AoapConfig aoapConfig_;

    static constexpr uint16_t kGoogleVendorId = 0x18D1;
    static constexpr uint16_t kAOAPId         = 0x2D00;
    static constexpr uint16_t kAOAPWithAdbId  = 0x2D01;
};
