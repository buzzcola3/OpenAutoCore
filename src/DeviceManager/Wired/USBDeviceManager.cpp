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

#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <algorithm>
#include <DeviceManager/Wired/USBDeviceConnection.hpp>
#include <DeviceManager/Common/DmLog.hpp>
#include <DeviceManager/Wired/USBDeviceManager.hpp>

// ── AOAP protocol constants ──

static constexpr uint8_t kReqGetProtocol  = 51;
static constexpr uint8_t kReqSendString   = 52;
static constexpr uint8_t kReqStart        = 53;

// ══════════════════════════════════════════════════════════════
// Construction / Destruction
// ══════════════════════════════════════════════════════════════

USBDeviceManager::USBDeviceManager(AoapConfig aoapConfig)
    : aoapConfig_(std::move(aoapConfig)) {
    int rc = libusb_init(&usbContext_);
    if (rc != LIBUSB_SUCCESS) {
        DM_LOG(error) << "USBDeviceManager: libusb_init failed: "
                      << libusb_strerror(static_cast<libusb_error>(rc));
        usbContext_ = nullptr;
    }
}

USBDeviceManager::~USBDeviceManager() {
    stop();
    if (usbContext_) {
        libusb_exit(usbContext_);
        usbContext_ = nullptr;
    }
}

DeviceConnection::Pointer USBDeviceManager::openDeviceConnection(uint16_t vid, uint16_t pid) {
    auto raw = libusb_open_device_with_vid_pid(usbContext_, vid, pid);
    if (!raw) {
        DM_LOG(error) << "USBDeviceManager: failed to open device "
                      << std::hex << vid << ":" << pid << std::dec;
        return nullptr;
    }
    auto handle = std::shared_ptr<libusb_device_handle>(raw, &libusb_close);
    return std::make_shared<USBDeviceConnection>(std::move(handle), usbContext_);
}

void USBDeviceManager::resetDevice(uint16_t vid, uint16_t pid) {
    auto raw = libusb_open_device_with_vid_pid(usbContext_, vid, pid);
    if (raw) {
        int rc = libusb_reset_device(raw);
        DM_LOG(info) << "USB reset to exit AOAP: "
                     << libusb_strerror(static_cast<libusb_error>(rc));
        libusb_close(raw);
    } else {
        DM_LOG(warning) << "Could not open AOAP device for reset";
    }
}

// ══════════════════════════════════════════════════════════════
// Scanning Lifecycle
// ══════════════════════════════════════════════════════════════

void USBDeviceManager::start() {
    if (running_) return;
    running_ = true;

    if (!usbContext_) return;

    // Create eventfd for cross-thread wakeup
    eventFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (eventFd_ < 0) {
        DM_LOG(error) << "USBDeviceManager: eventfd() failed: " << std::strerror(errno);
    }

    int rc = libusb_hotplug_register_callback(
        usbContext_,
        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED,
        LIBUSB_HOTPLUG_ENUMERATE,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY,
        onHotplugEvent,
        this,
        &hotplugHandle_);

    if (rc == LIBUSB_SUCCESS) {
        hotplugRegistered_ = true;
        DM_LOG(info) << "USBDeviceManager: USB hotplug registered";
    } else {
        DM_LOG(error) << "USBDeviceManager: hotplug registration failed: "
                      << libusb_strerror(static_cast<libusb_error>(rc));
    }

    startEventThread();
}

void USBDeviceManager::stop() {
    if (!running_) return;
    running_ = false;

    stopEventThread();

    // Cancel in-flight AOAP setups
    for (auto& setup : aoapSetups_) {
        if (setup->transfer) libusb_cancel_transfer(setup->transfer);
        if (setup->handle) libusb_close(setup->handle);
    }
    aoapSetups_.clear();

    // Release pending phone device references
    for (auto& [id, dev] : pendingPhones_) {
        libusb_unref_device(dev);
    }
    pendingPhones_.clear();

    if (hotplugRegistered_ && usbContext_) {
        libusb_hotplug_deregister_callback(usbContext_, hotplugHandle_);
        hotplugRegistered_ = false;
    }

    // Drain and close eventfd
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* dev : hotplugQueue_) {
            libusb_unref_device(dev);
        }
        hotplugQueue_.clear();
    }
    if (eventFd_ >= 0) {
        ::close(eventFd_);
        eventFd_ = -1;
    }

    DM_LOG(info) << "USBDeviceManager: stopped";
}

// ══════════════════════════════════════════════════════════════
// Event Thread
// ══════════════════════════════════════════════════════════════

void USBDeviceManager::startEventThread() {
    eventThread_ = std::thread([this]() {
        timeval timeout{1, 0};
        while (running_.load(std::memory_order_relaxed)) {
            libusb_handle_events_timeout_completed(usbContext_, &timeout, nullptr);
        }
    });
    DM_LOG(info) << "USBDeviceManager: event thread started";
}

void USBDeviceManager::stopEventThread() {
    if (usbContext_) {
        libusb_interrupt_event_handler(usbContext_);
    }
    if (eventThread_.joinable()) eventThread_.join();
}

// ══════════════════════════════════════════════════════════════
// USB Hotplug
// ══════════════════════════════════════════════════════════════

int USBDeviceManager::onHotplugEvent(libusb_context*, libusb_device* device,
                                     libusb_hotplug_event event, void* userData) {
    if (event != LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) return 0;

    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device, &desc) == 0) {
        DM_LOG(info) << "USBDeviceManager: hotplug arrived " << std::hex
                     << desc.idVendor << ":" << desc.idProduct << std::dec;
    }

    auto* self = static_cast<USBDeviceManager*>(userData);
    libusb_ref_device(device);
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->hotplugQueue_.push_back(device);
    }
    self->wakeEventFd();
    return 0;
}

void USBDeviceManager::wakeEventFd() {
    uint64_t val = 1;
    (void)::write(eventFd_, &val, sizeof(val));
}

void USBDeviceManager::execute() {
    if (eventFd_ < 0) return;
    struct pollfd pfd = {eventFd_, POLLIN, 0};
    if (::poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        uint64_t val;
        (void)::read(eventFd_, &val, sizeof(val));
        drainQueue();
    }
}

void USBDeviceManager::drainQueue() {
    std::vector<libusb_device*> batch;
    std::vector<AoapSetup*> completedSetups;
    std::vector<PendingCommand> commands;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(hotplugQueue_);
        completedSetups.swap(completedAoapQueue_);
        commands.swap(commandQueue_);
    }

    for (auto* setup : completedSetups) {
        cleanupAoapSetup(setup);
    }
    for (auto* dev : batch) {
        handleUSBDevice(dev);
        libusb_unref_device(dev);
    }
    for (auto& cmd : commands) {
        switch (cmd.type) {
            case Command::BeginAoap:  doBeginAoapSetup(cmd.arg); break;
            case Command::RescanUSB:  doRescanUSB(); break;
        }
    }
}

void USBDeviceManager::handleUSBDevice(libusb_device* device) {
    if (!running_) return;

    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device, &desc) != 0) return;

    DM_LOG(info) << "USBDeviceManager: handleUSBDevice " << std::hex
                 << desc.idVendor << ":" << desc.idProduct << std::dec
                 << " class=" << (int)desc.bDeviceClass;

    if (shouldSkipDevice(device, desc)) return;

    uint8_t bus = libusb_get_bus_number(device);
    uint8_t port = libusb_get_port_number(device);
    std::string id = "usb:" + std::to_string(bus) + ":" + std::to_string(port);

    if (isAOAPDevice(desc)) {
        DM_LOG(info) << "USBDeviceManager: AOAP device ready: " << id
                     << " (" << std::hex << desc.idVendor << ":"
                     << desc.idProduct << std::dec << ")";
        if (onUSBDeviceAvailable) {
            onUSBDeviceAvailable(bus, port, desc.idVendor, desc.idProduct);
        }
    } else {
        DM_LOG(info) << "USBDeviceManager: non-AOAP phone detected (" << std::hex
                     << desc.idVendor << ":" << desc.idProduct << std::dec
                     << ") " << id;
        if (onUSBPhoneDetected) {
            libusb_ref_device(device);
            pendingPhones_[id] = device;
            onUSBPhoneDetected(bus, port, desc.idVendor, desc.idProduct);
        }
    }
}

// Classes that indicate a device is not an Android phone.
static bool isSkippedClass(uint8_t cls) {
    switch (cls) {
        case 1:    // audio
        case 2:    // CDC/comm
        case 3:    // HID
        case 5:    // physical
        case 7:    // printer
        case 8:    // mass storage
        case 9:    // hub
        case 0x0B: // smart card
        case 0x0E: // video
        case 0xE0: // wireless controller (BT/WiFi adapters)
            return true;
        default:
            return false;
    }
}

// ADB uses class 0xFF, subclass 0x42, protocol 0x01.
static bool isAndroidInterface(const libusb_interface_descriptor& alt) {
    return alt.bInterfaceClass == 0xFF &&
           alt.bInterfaceSubClass == 0x42 &&
           alt.bInterfaceProtocol == 0x01;
}

bool USBDeviceManager::shouldSkipDevice(libusb_device* device,
                                        const libusb_device_descriptor& desc) const {
    if (desc.idVendor == 0x1d6b) return true;
    if (isAOAPDevice(desc)) return false;
    if (desc.bDeviceClass != 0) return isSkippedClass(desc.bDeviceClass);

    libusb_config_descriptor* config = nullptr;
    if (libusb_get_active_config_descriptor(device, &config) != 0) {
        return true;
    }

    bool looksLikePhone = false;
    for (int i = 0; i < config->bNumInterfaces && !looksLikePhone; ++i) {
        const auto& iface = config->interface[i];
        for (int a = 0; a < iface.num_altsetting && !looksLikePhone; ++a) {
            const auto& alt = iface.altsetting[a];
            if (isAndroidInterface(alt)) { looksLikePhone = true; break; }
            if (alt.bInterfaceClass == 0x06) { looksLikePhone = true; break; }
            if (alt.bInterfaceClass != 0xFF && !isSkippedClass(alt.bInterfaceClass)) {
                looksLikePhone = true; break;
            }
        }
    }
    libusb_free_config_descriptor(config);
    return !looksLikePhone;
}

bool USBDeviceManager::isAOAPDevice(const libusb_device_descriptor& desc) const {
    return desc.idVendor == kGoogleVendorId &&
           (desc.idProduct == kAOAPId || desc.idProduct == kAOAPWithAdbId);
}

// ══════════════════════════════════════════════════════════════
// AOAP Setup State Machine
// ══════════════════════════════════════════════════════════════

void USBDeviceManager::startAoapSetup(libusb_device* device, const std::string& id) {
    libusb_device_handle* handle = nullptr;
    int rc = libusb_open(device, &handle);
    if (rc != LIBUSB_SUCCESS) {
        DM_LOG(warning) << "USBDeviceManager: AOAP setup failed to open device "
                        << id << ": " << libusb_strerror(static_cast<libusb_error>(rc));
        return;
    }

    auto setup = std::make_unique<AoapSetup>();
    setup->manager = this;
    setup->handle = handle;
    setup->transfer = libusb_alloc_transfer(0);
    setup->state = 0;
    setup->deviceId = id;

    auto* raw = setup.get();
    aoapSetups_.push_back(std::move(setup));
    advanceAoapSetup(raw);
}

void USBDeviceManager::advanceAoapSetup(AoapSetup* setup) {
    setup->buffer.clear();

    DM_LOG(info) << "USBDeviceManager: AOAP state " << setup->state
                 << " for " << setup->deviceId;

    if (setup->state == 0) {
        setup->buffer.resize(LIBUSB_CONTROL_SETUP_SIZE + 2, 0);
        libusb_fill_control_setup(setup->buffer.data(),
                                  LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR,
                                  kReqGetProtocol, 0, 0, 2);
    } else if (setup->state >= 1 && setup->state <= 6) {
        const std::string* strings[] = {
            &setup->manager->aoapConfig_.manufacturer,
            &setup->manager->aoapConfig_.model,
            &setup->manager->aoapConfig_.description,
            &setup->manager->aoapConfig_.version,
            &setup->manager->aoapConfig_.uri,
            &setup->manager->aoapConfig_.serial,
        };
        const auto& str = *strings[setup->state - 1];
        uint16_t index = static_cast<uint16_t>(setup->state - 1);
        size_t len = str.size() + 1;
        setup->buffer.resize(LIBUSB_CONTROL_SETUP_SIZE + len);
        libusb_fill_control_setup(setup->buffer.data(),
                                  LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
                                  kReqSendString, 0,
                                  index,
                                  static_cast<uint16_t>(len));
        std::memcpy(setup->buffer.data() + LIBUSB_CONTROL_SETUP_SIZE, str.c_str(), len);
    } else if (setup->state == 7) {
        setup->buffer.resize(LIBUSB_CONTROL_SETUP_SIZE, 0);
        libusb_fill_control_setup(setup->buffer.data(),
                                  LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
                                  kReqStart, 0, 0, 0);
    } else {
        DM_LOG(info) << "USBDeviceManager: AOAP setup complete for " << setup->deviceId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completedAoapQueue_.push_back(setup);
        }
        wakeEventFd();
        return;
    }

    libusb_fill_control_transfer(setup->transfer, setup->handle,
                                 setup->buffer.data(),
                                 onAoapTransferDone, setup, 1000);

    int rc = libusb_submit_transfer(setup->transfer);
    if (rc != LIBUSB_SUCCESS) {
        DM_LOG(warning) << "USBDeviceManager: AOAP control transfer failed at state "
                        << setup->state << ": "
                        << libusb_strerror(static_cast<libusb_error>(rc));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completedAoapQueue_.push_back(setup);
        }
        wakeEventFd();
    }
}

void USBDeviceManager::onAoapTransferDone(libusb_transfer* transfer) {
    auto* setup = static_cast<AoapSetup*>(transfer->user_data);

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        DM_LOG(warning) << "USBDeviceManager: AOAP transfer failed at state "
                        << setup->state << " status=" << transfer->status;
        {
            std::lock_guard<std::mutex> lock(setup->manager->mutex_);
            setup->manager->completedAoapQueue_.push_back(setup);
        }
        setup->manager->wakeEventFd();
        return;
    }

    if (setup->state == 0) {
        auto* data = libusb_control_transfer_get_data(transfer);
        uint16_t version = static_cast<uint16_t>(data[0]) |
                          (static_cast<uint16_t>(data[1]) << 8);
        if (version < 1) {
            DM_LOG(warning) << "USBDeviceManager: device does not support AOAP (version="
                            << version << ")";
            {
                std::lock_guard<std::mutex> lock(setup->manager->mutex_);
                setup->manager->completedAoapQueue_.push_back(setup);
            }
            setup->manager->wakeEventFd();
            return;
        }
    }

    setup->state++;
    setup->manager->advanceAoapSetup(setup);
}

void USBDeviceManager::cleanupAoapSetup(AoapSetup* setup) {
    if (setup->handle) {
        libusb_close(setup->handle);
        setup->handle = nullptr;
    }
    if (setup->transfer) {
        libusb_free_transfer(setup->transfer);
        setup->transfer = nullptr;
    }
    aoapSetups_.remove_if([setup](auto& s) { return s.get() == setup; });
}

// ══════════════════════════════════════════════════════════════
// User-Initiated Device Operations (thread-safe → ELL thread)
// ══════════════════════════════════════════════════════════════

void USBDeviceManager::beginAoapSetup(const std::string& deviceId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commandQueue_.push_back({Command::BeginAoap, deviceId});
    }
    wakeEventFd();
}

void USBDeviceManager::rescanUSB() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commandQueue_.push_back({Command::RescanUSB, {}});
    }
    wakeEventFd();
}

void USBDeviceManager::doBeginAoapSetup(const std::string& deviceId) {
    auto it = pendingPhones_.find(deviceId);
    if (it == pendingPhones_.end()) {
        DM_LOG(warning) << "USBDeviceManager: beginAoapSetup: unknown device " << deviceId;
        return;
    }
    libusb_device* device = it->second;
    DM_LOG(info) << "USBDeviceManager: starting AOAP setup for " << deviceId;
    startAoapSetup(device, deviceId);
    libusb_unref_device(device);
    pendingPhones_.erase(it);
}

void USBDeviceManager::doRescanUSB() {
    if (!usbContext_) return;
    DM_LOG(info) << "USBDeviceManager: rescanning USB devices";

    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(usbContext_, &list);
    if (count < 0 || !list) return;

    for (ssize_t i = 0; i < count; ++i) {
        handleUSBDevice(list[i]);
    }
    libusb_free_device_list(list, 1);
}
