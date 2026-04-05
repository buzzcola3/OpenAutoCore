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

#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <ell/dbus.h>
#include <ell/dbus-service.h>
#include <DeviceManager/EllCompat.hpp>
#include <DeviceManager/DmLog.hpp>
#include <DeviceManager/DeviceManager.hpp>
#include <DeviceManager/USBDevice.hpp>
#include <DeviceManager/WifiDevice.hpp>

// ── AOAP protocol constants ──

static constexpr uint8_t kReqGetProtocol  = 51;
static constexpr uint8_t kReqSendString   = 52;
static constexpr uint8_t kReqStart        = 53;

struct AoapString {
    int index;
    const char* value;
};

static constexpr AoapString kAoapStrings[] = {
    {0, "Android"},               // Manufacturer
    {1, "Android Auto"},          // Model
    {2, "Android Auto"},          // Description
    {3, "2.0.1"},                 // Version
    {4, "https://f1xstudio.com"}, // URI
    {5, "HU-AAAAAA001"},          // Serial
};

// ── Bluetooth WiFi handshake protocol ──

enum BtWifiMsgId : uint16_t {
    BT_WIFI_START_REQUEST     = 1,
    BT_WIFI_INFO_REQUEST      = 2,
    BT_WIFI_INFO_RESPONSE     = 3,
    BT_WIFI_VERSION_REQUEST   = 4,
    BT_WIFI_VERSION_RESPONSE  = 5,
    BT_WIFI_CONNECTION_STATUS = 6,
    BT_WIFI_START_RESPONSE    = 7,
};

// WifiSecurityMode::WPA2_PERSONAL = 2, AccessPointType::STATIC = 1
static constexpr int32_t kWPA2Personal       = 2;
static constexpr int32_t kAccessPointStatic  = 1;

static const char* wifiStatusName(int32_t s) {
    switch (s) {
        case 0:   return "STATUS_SUCCESS";
        case -1:  return "STATUS_NO_COMPATIBLE_VERSION";
        case -2:  return "STATUS_WIFI_INACCESSIBLE_CHANNEL";
        case -3:  return "STATUS_WIFI_INCORRECT_CREDENTIALS";
        case -5:  return "STATUS_WIFI_DISABLED";
        case -10: return "STATUS_PHONE_WIFI_DISABLED";
        case -11: return "STATUS_WIFI_NETWORK_UNAVAILABLE";
        default:  return "UNKNOWN";
    }
}

// ── Minimal protobuf wire format helpers ──

static void pbWriteVarint(std::vector<uint8_t>& buf, uint64_t val) {
    do {
        uint8_t b = val & 0x7F;
        val >>= 7;
        if (val) b |= 0x80;
        buf.push_back(b);
    } while (val);
}

static void pbWriteString(std::vector<uint8_t>& buf, int field, const std::string& s) {
    pbWriteVarint(buf, (static_cast<uint64_t>(field) << 3) | 2);
    pbWriteVarint(buf, s.size());
    buf.insert(buf.end(), s.begin(), s.end());
}

static void pbWriteInt32(std::vector<uint8_t>& buf, int field, int32_t val) {
    pbWriteVarint(buf, (static_cast<uint64_t>(field) << 3) | 0);
    // protobuf encodes negative values as 10-byte varint; for small positive enums this is fine
    if (val >= 0) {
        pbWriteVarint(buf, static_cast<uint64_t>(val));
    } else {
        // 10-byte encoding for negative varint
        pbWriteVarint(buf, static_cast<uint64_t>(static_cast<uint32_t>(val)) |
                           (static_cast<uint64_t>(0xFFFFFFFF) << 32));
    }
}

static bool pbReadVarint(const uint8_t*& p, const uint8_t* end, uint64_t& out) {
    out = 0;
    int shift = 0;
    while (p < end) {
        uint8_t b = *p++;
        out |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

struct PbField {
    int fieldNum;
    int wireType;
    uint64_t varintVal;
    const uint8_t* bytesData;
    size_t bytesLen;
};

static bool pbReadField(const uint8_t*& p, const uint8_t* end, PbField& f) {
    if (p >= end) return false;
    uint64_t tag;
    if (!pbReadVarint(p, end, tag)) return false;
    f.fieldNum = static_cast<int>(tag >> 3);
    f.wireType = static_cast<int>(tag & 7);
    f.varintVal = 0;
    f.bytesData = nullptr;
    f.bytesLen = 0;
    switch (f.wireType) {
        case 0: return pbReadVarint(p, end, f.varintVal);
        case 2: {
            uint64_t len;
            if (!pbReadVarint(p, end, len)) return false;
            if (static_cast<size_t>(end - p) < len) return false;
            f.bytesData = p;
            f.bytesLen = static_cast<size_t>(len);
            p += len;
            return true;
        }
        default: return false;
    }
}

// ── D-Bus synchronous helpers (mirrors EllDbusUtils, standalone) ──

namespace {

struct DmDbusReadyWaiter {
    std::mutex mx;
    std::condition_variable cv;
    bool ready = false;
};

void dmDbusReadyHandler(void* ud) {
    auto* w = static_cast<DmDbusReadyWaiter*>(ud);
    std::lock_guard<std::mutex> lk(w->mx);
    w->ready = true;
    w->cv.notify_one();
}

bool dmDbusWaitReady(l_dbus* bus, int timeoutMs) {
    DmDbusReadyWaiter w;
    if (!l_dbus_set_ready_handler(bus, dmDbusReadyHandler, &w, nullptr))
        return false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lk(w.mx);
    return w.cv.wait_until(lk, deadline, [&] { return w.ready; });
}

struct DmDbusReplyWaiter {
    std::mutex mx;
    std::condition_variable cv;
    bool done = false;
    l_dbus_message* reply = nullptr;
};

void dmDbusReplyHandler(l_dbus_message* msg, void* ud) {
    auto* w = static_cast<DmDbusReplyWaiter*>(ud);
    std::lock_guard<std::mutex> lk(w->mx);
    w->reply = msg ? l_dbus_message_ref(msg) : nullptr;
    w->done = true;
    w->cv.notify_one();
}

l_dbus_message* dmDbusSendSync(l_dbus* bus, l_dbus_message* msg, int timeoutMs) {
    if (!bus || !msg) return nullptr;
    DmDbusReplyWaiter w;
    auto serial = l_dbus_send_with_reply(bus, msg, dmDbusReplyHandler, &w, nullptr);
    if (serial == 0) {
        l_dbus_message_unref(msg);
        return nullptr;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lk(w.mx);
    if (!w.cv.wait_until(lk, deadline, [&] { return w.done; }))
        return nullptr;
    return w.reply;
}

bool dmDbusNameHasOwner(l_dbus* bus, const char* name, int timeoutMs) {
    auto* msg = l_dbus_message_new_method_call(bus, "org.freedesktop.DBus",
                                               "/org/freedesktop/DBus",
                                               "org.freedesktop.DBus",
                                               "NameHasOwner");
    auto* builder = l_dbus_message_builder_new(msg);
    l_dbus_message_builder_append_basic(builder, 's', name);
    l_dbus_message_builder_finalize(builder);
    l_dbus_message_builder_destroy(builder);
    auto* reply = dmDbusSendSync(bus, msg, timeoutMs);
    if (!reply || l_dbus_message_is_error(reply)) {
        if (reply) l_dbus_message_unref(reply);
        return false;
    }
    bool has = false;
    l_dbus_message_get_arguments(reply, "b", &has);
    l_dbus_message_unref(reply);
    return has;
}

bool dmGetPropertyString(struct l_dbus_message_iter props, const char* key, std::string& out) {
    const char* name = nullptr;
    struct l_dbus_message_iter variant;
    while (l_dbus_message_iter_next_entry(&props, &name, &variant)) {
        if (!name || std::strcmp(name, key) != 0) continue;
        const char* value = nullptr;
        if (!l_dbus_message_iter_get_variant(&variant, "s", &value) || !value) return false;
        out = value;
        return true;
    }
    return false;
}

bool isWirelessInterface(const std::string& name) {
    if (name.empty()) return false;
    return ::access(("/sys/class/net/" + name + "/wireless").c_str(), F_OK) == 0;
}

uint16_t btFrameReadU16(const std::vector<uint8_t>& buf, size_t off) {
    return static_cast<uint16_t>((buf[off] << 8) | buf[off + 1]);
}

void btFrameWriteU16(std::vector<uint8_t>& buf, size_t off, uint16_t val) {
    buf[off] = static_cast<uint8_t>((val >> 8) & 0xFF);
    buf[off + 1] = static_cast<uint8_t>(val & 0xFF);
}

constexpr int kDbusTimeoutMs = 5000;
constexpr const char* kBluezSvc         = "org.bluez";
constexpr const char* kAdapterIface     = "org.bluez.Adapter1";
constexpr const char* kProfileIface     = "org.bluez.Profile1";
constexpr const char* kPropsIface       = "org.freedesktop.DBus.Properties";
constexpr const char* kObjMgrIface      = "org.freedesktop.DBus.ObjectManager";
constexpr const char* kProfileObjPath   = "/devicemanager/bt_profile";
constexpr const char* kProfileUuid      = "4de17a00-52cb-11e6-bdf4-0800200c9a66";
constexpr uint16_t    kBtChannel        = 12;

} // anonymous namespace

// ══════════════════════════════════════════════════════════════
// Construction / Destruction
// ══════════════════════════════════════════════════════════════

DeviceManager::DeviceManager(DeviceManagerConfig config)
    : config_(std::move(config)) {
    int rc = libusb_init(&usbContext_);
    if (rc != LIBUSB_SUCCESS) {
        DM_LOG(error) << "DeviceManager: libusb_init failed: "
                   << libusb_strerror(static_cast<libusb_error>(rc));
        usbContext_ = nullptr;
    }
}

DeviceManager::~DeviceManager() {
    stop();
    if (usbContext_) {
        libusb_exit(usbContext_);
        usbContext_ = nullptr;
    }
}

// ══════════════════════════════════════════════════════════════
// Scanning Lifecycle
// ══════════════════════════════════════════════════════════════

void DeviceManager::start() {
    if (running_) return;
    running_ = true;

    if (usbContext_) {
        // Register libusb fds into ELL for event-driven USB handling
        registerLibusbFds();

        // Set up eventfd-based cross-thread hotplug wakeup
        setupHotplugWakeup();

        // Register USB hotplug callback (also enumerates already-connected devices)
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
            DM_LOG(info) << "DeviceManager: USB hotplug registered";
        } else {
            DM_LOG(error) << "DeviceManager: hotplug registration failed: "
                       << libusb_strerror(static_cast<libusb_error>(rc));
        }
    }

    if (config_.wifiPort > 0) {
        setupTCPListener(config_.wifiPort);
    }

    if (config_.bluetoothEnabled) {
        setupBluetooth();
    }

    DM_LOG(info) << "DeviceManager: started (USB"
                 << (config_.wifiPort > 0 ? " + TCP:" + std::to_string(config_.wifiPort) : "")
                 << (config_.bluetoothEnabled ? " + BT" : "")
                 << ")";
}

void DeviceManager::stop() {
    if (!running_) return;
    running_ = false;

    disconnect();

    // Cancel in-flight AOAP setups
    for (auto& setup : aoapSetups_) {
        if (setup->transfer) libusb_cancel_transfer(setup->transfer);
        if (setup->handle) libusb_close(setup->handle);
    }
    aoapSetups_.clear();

    // Remove all devices
    for (auto& [id, device] : devices_) {
        device->stop();
        if (onDeviceLost) onDeviceLost(id);
    }
    devices_.clear();

    // USB cleanup
    if (hotplugRegistered_ && usbContext_) {
        libusb_hotplug_deregister_callback(usbContext_, hotplugHandle_);
        hotplugRegistered_ = false;
    }
    teardownHotplugWakeup();
    unregisterLibusbFds();

    teardownTCPListener();
    teardownBluetooth();
    DM_LOG(info) << "DeviceManager: stopped";
}

// ══════════════════════════════════════════════════════════════
// Device List & Connection
// ══════════════════════════════════════════════════════════════

std::vector<DeviceInfo> DeviceManager::getDevices() const {
    std::vector<DeviceInfo> result;
    result.reserve(devices_.size());
    for (auto& [id, device] : devices_) {
        result.push_back(device->info());
    }
    return result;
}

void DeviceManager::connect(const std::string& deviceId) {
    if (connectedDevice_) {
        DM_LOG(warning) << "DeviceManager: already connected, disconnect first";
        return;
    }

    auto it = devices_.find(deviceId);
    if (it == devices_.end()) {
        DM_LOG(warning) << "DeviceManager: unknown device id: " << deviceId;
        return;
    }

    connectedDevice_ = it->second.get();
    DM_LOG(info) << "DeviceManager: connected to " << deviceId;
    if (onConnected) onConnected();
}

void DeviceManager::disconnect() {
    if (!connectedDevice_) return;

    auto id = connectedDevice_->info().id;
    connectedDevice_->stop();
    connectedDevice_ = nullptr;
    DM_LOG(info) << "DeviceManager: disconnected from " << id;
    if (onDisconnected) onDisconnected("Disconnected");
}

// ══════════════════════════════════════════════════════════════
// Raw I/O (delegates to connected device)
// ══════════════════════════════════════════════════════════════

void DeviceManager::send(const uint8_t* data, size_t size,
                         IDevice::SendHandler onComplete, IDevice::ErrorHandler onError) {
    if (!connectedDevice_) {
        if (onError) onError("No device connected");
        return;
    }
    connectedDevice_->send(data, size, std::move(onComplete), std::move(onError));
}

void DeviceManager::receive(uint8_t* buffer, size_t maxSize,
                            IDevice::ReceiveHandler onData, IDevice::ErrorHandler onError) {
    if (!connectedDevice_) {
        if (onError) onError("No device connected");
        return;
    }
    connectedDevice_->receive(buffer, maxSize, std::move(onData), std::move(onError));
}

// ══════════════════════════════════════════════════════════════
// USB Hotplug
// ══════════════════════════════════════════════════════════════

int DeviceManager::onHotplugEvent(libusb_context* /*ctx*/, libusb_device* device,
                                  libusb_hotplug_event event, void* userData) {
    if (event != LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) return 0;

    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device, &desc) == 0) {
        DM_LOG(info) << "DeviceManager: hotplug arrived " << std::hex
                     << desc.idVendor << ":" << desc.idProduct << std::dec;
    }

    auto* self = static_cast<DeviceManager*>(userData);
    // Queue device and wake the ELL thread via eventfd (thread-safe).
    libusb_ref_device(device);
    {
        std::lock_guard<std::mutex> lock(self->hotplugMutex_);
        self->hotplugQueue_.push_back(device);
    }
    uint64_t val = 1;
    (void)::write(self->hotplugEventFd_, &val, sizeof(val));

    return 0;
}

void DeviceManager::setupHotplugWakeup() {
    hotplugEventFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (hotplugEventFd_ < 0) {
        DM_LOG(error) << "DeviceManager: eventfd() failed: " << std::strerror(errno);
        return;
    }
    hotplugWakeupIo_ = l_io_new(hotplugEventFd_);
    l_io_set_read_handler(hotplugWakeupIo_, onHotplugWakeup, this, nullptr);
}

void DeviceManager::teardownHotplugWakeup() {
    if (hotplugWakeupIo_) {
        l_io_destroy(hotplugWakeupIo_);
        hotplugWakeupIo_ = nullptr;
    }
    // Drain and unref any remaining queued devices
    std::lock_guard<std::mutex> lock(hotplugMutex_);
    for (auto* dev : hotplugQueue_) {
        libusb_unref_device(dev);
    }
    hotplugQueue_.clear();
    if (hotplugEventFd_ >= 0) {
        ::close(hotplugEventFd_);
        hotplugEventFd_ = -1;
    }
}

bool DeviceManager::onHotplugWakeup(struct l_io* /*io*/, void* userData) {
    auto* self = static_cast<DeviceManager*>(userData);
    // Consume the eventfd counter
    uint64_t val;
    (void)::read(self->hotplugEventFd_, &val, sizeof(val));
    self->drainHotplugQueue();
    return true;
}

void DeviceManager::drainHotplugQueue() {
    std::vector<libusb_device*> batch;
    {
        std::lock_guard<std::mutex> lock(hotplugMutex_);
        batch.swap(hotplugQueue_);
    }
    for (auto* dev : batch) {
        handleUSBDevice(dev);
        libusb_unref_device(dev);
    }
}

void DeviceManager::handleUSBDevice(libusb_device* device) {
    if (!running_) return;

    libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device, &desc) != 0) return;

    DM_LOG(info) << "DeviceManager: handleUSBDevice " << std::hex
                 << desc.idVendor << ":" << desc.idProduct << std::dec
                 << " class=" << (int)desc.bDeviceClass;

    if (shouldSkipDevice(device, desc)) return;

    uint8_t bus = libusb_get_bus_number(device);
    uint8_t port = libusb_get_port_number(device);
    std::string id = "usb:" + std::to_string(bus) + ":" + std::to_string(port);

    if (isAOAPDevice(desc)) {
        if (onUSBDeviceAvailable) {
            // Notify consumer with VID/PID so it can open on its own context
            DM_LOG(info) << "DeviceManager: AOAP device ready: " << id
                         << " (" << std::hex << desc.idVendor << ":"
                         << desc.idProduct << std::dec << "), handing to consumer";
            onUSBDeviceAvailable(desc.idVendor, desc.idProduct);
        } else {
            // Fallback: create USBDevice internally
            DeviceInfo info{id, "USB " + std::to_string(desc.idVendor) + ":"
                            + std::to_string(desc.idProduct), DeviceInfo::Transport::USB};
            auto usbDevice = USBDevice::create(std::move(info), device);
            if (usbDevice) {
                auto& ref = *usbDevice;
                devices_[id] = std::move(usbDevice);
                DM_LOG(info) << "DeviceManager: AOAP device ready: " << id;
                if (onDeviceFound) onDeviceFound(ref.info());
            }
        }
    } else {
        // Not AOAP yet — start accessory mode setup
        DM_LOG(info) << "DeviceManager: non-AOAP device (" << std::hex
                  << desc.idVendor << ":" << desc.idProduct << std::dec
                  << "), starting AOAP setup";
        startAoapSetup(device, id);
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

// Check if a vendor-specific (0xFF) interface looks like an Android phone.
// ADB uses class 0xFF, subclass 0x42, protocol 0x01.
static bool isAndroidInterface(const libusb_interface_descriptor& alt) {
    return alt.bInterfaceClass == 0xFF &&
           alt.bInterfaceSubClass == 0x42 &&
           alt.bInterfaceProtocol == 0x01;
}

bool DeviceManager::shouldSkipDevice(libusb_device* device,
                                     const libusb_device_descriptor& desc) const {
    // Linux Foundation root hubs
    if (desc.idVendor == 0x1d6b) return true;

    // AOAP devices are never skipped
    if (isAOAPDevice(desc)) return false;

    // Device-level class is conclusive
    if (desc.bDeviceClass != 0) return isSkippedClass(desc.bDeviceClass);

    // bDeviceClass == 0 means class is defined per-interface.
    // Check the active config descriptor (reads sysfs, no open needed).
    libusb_config_descriptor* config = nullptr;
    if (libusb_get_active_config_descriptor(device, &config) != 0) {
        // Can't read config — skip unless it's a known phone VID
        return true;
    }

    // Look for Android phone indicators:
    //  - ADB interface (0xFF/0x42/0x01)
    //  - MTP/PTP interface (class 0x06)
    //  - Any interface class not in the skip list AND not vendor-specific
    // If none found, skip the device.
    bool looksLikePhone = false;
    for (int i = 0; i < config->bNumInterfaces && !looksLikePhone; ++i) {
        const auto& iface = config->interface[i];
        for (int a = 0; a < iface.num_altsetting && !looksLikePhone; ++a) {
            const auto& alt = iface.altsetting[a];
            if (isAndroidInterface(alt)) { looksLikePhone = true; break; }
            if (alt.bInterfaceClass == 0x06) { looksLikePhone = true; break; } // PTP/MTP
            // Unknown class that isn't in skip list and isn't vendor-specific — uncertain
            if (alt.bInterfaceClass != 0xFF && !isSkippedClass(alt.bInterfaceClass)) {
                looksLikePhone = true; break;
            }
        }
    }
    libusb_free_config_descriptor(config);
    return !looksLikePhone;
}

bool DeviceManager::isAOAPDevice(const libusb_device_descriptor& desc) const {
    return desc.idVendor == kGoogleVendorId &&
           (desc.idProduct == kAOAPId || desc.idProduct == kAOAPWithAdbId);
}

// ══════════════════════════════════════════════════════════════
// AOAP Setup State Machine
// ══════════════════════════════════════════════════════════════
//
// States 0-7 map to the AOAP handshake:
//   0: GET_PROTOCOL (control IN)
//   1-6: SEND_STRING (control OUT) — manufacturer, model, etc.
//   7: START (control OUT) — phone re-enumerates as AOAP device

void DeviceManager::startAoapSetup(libusb_device* device, const std::string& id) {
    libusb_device_handle* handle = nullptr;
    int rc = libusb_open(device, &handle);
    if (rc != LIBUSB_SUCCESS) {
        DM_LOG(warning) << "DeviceManager: AOAP setup failed to open device "
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

void DeviceManager::advanceAoapSetup(AoapSetup* setup) {
    setup->buffer.clear();

    DM_LOG(info) << "DeviceManager: AOAP state " << setup->state
                  << " for " << setup->deviceId;

    if (setup->state == 0) {
        // GET_PROTOCOL: control IN, 2 bytes response
        setup->buffer.resize(LIBUSB_CONTROL_SETUP_SIZE + 2, 0);
        libusb_fill_control_setup(setup->buffer.data(),
                                  LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR,
                                  kReqGetProtocol, 0, 0, 2);
    } else if (setup->state >= 1 && setup->state <= 6) {
        // SEND_STRING: control OUT
        auto& str = kAoapStrings[setup->state - 1];
        size_t len = std::strlen(str.value) + 1; // include null terminator
        setup->buffer.resize(LIBUSB_CONTROL_SETUP_SIZE + len);
        libusb_fill_control_setup(setup->buffer.data(),
                                  LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
                                  kReqSendString, 0,
                                  static_cast<uint16_t>(str.index),
                                  static_cast<uint16_t>(len));
        std::memcpy(setup->buffer.data() + LIBUSB_CONTROL_SETUP_SIZE, str.value, len);
    } else if (setup->state == 7) {
        // START: control OUT, no data
        setup->buffer.resize(LIBUSB_CONTROL_SETUP_SIZE, 0);
        libusb_fill_control_setup(setup->buffer.data(),
                                  LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
                                  kReqStart, 0, 0, 0);
    } else {
        // Done — phone should re-enumerate as AOAP
        DM_LOG(info) << "DeviceManager: AOAP setup complete for " << setup->deviceId;
        cleanupAoapSetup(setup);
        return;
    }

    libusb_fill_control_transfer(setup->transfer, setup->handle,
                                 setup->buffer.data(),
                                 onAoapTransferDone, setup, 1000);

    int rc = libusb_submit_transfer(setup->transfer);
    if (rc != LIBUSB_SUCCESS) {
        DM_LOG(warning) << "DeviceManager: AOAP control transfer failed at state "
                     << setup->state << ": "
                     << libusb_strerror(static_cast<libusb_error>(rc));
        cleanupAoapSetup(setup);
    }
}

void DeviceManager::onAoapTransferDone(libusb_transfer* transfer) {
    auto* setup = static_cast<AoapSetup*>(transfer->user_data);

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        DM_LOG(warning) << "DeviceManager: AOAP transfer failed at state "
                     << setup->state << " status=" << transfer->status;
        setup->manager->cleanupAoapSetup(setup);
        return;
    }

    // If GET_PROTOCOL, verify version >= 1
    if (setup->state == 0) {
        auto* data = libusb_control_transfer_get_data(transfer);
        uint16_t version = static_cast<uint16_t>(data[0]) |
                          (static_cast<uint16_t>(data[1]) << 8);
        if (version < 1) {
            DM_LOG(warning) << "DeviceManager: device does not support AOAP (version="
                         << version << ")";
            setup->manager->cleanupAoapSetup(setup);
            return;
        }
    }

    setup->state++;
    setup->manager->advanceAoapSetup(setup);
}

void DeviceManager::cleanupAoapSetup(AoapSetup* setup) {
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
// libusb fd Integration with ELL
// ══════════════════════════════════════════════════════════════

void DeviceManager::registerLibusbFds() {
    // Register existing libusb fds
    const libusb_pollfd** pollfds = libusb_get_pollfds(usbContext_);
    if (pollfds) {
        for (int i = 0; pollfds[i]; i++) {
            auto* watch = l_io_new(pollfds[i]->fd);
            if (pollfds[i]->events & POLLIN)
                l_io_set_read_handler(watch, onLibusbFdReady, this, nullptr);
            if (pollfds[i]->events & POLLOUT)
                l_io_set_write_handler(watch, onLibusbFdReady, this, nullptr);
            libusbWatches_[pollfds[i]->fd] = watch;
        }
        libusb_free_pollfds(pollfds);
    }

    // Register for dynamic fd additions/removals
    libusb_set_pollfd_notifiers(usbContext_, onLibusbFdAdded, onLibusbFdRemoved, this);
}

void DeviceManager::unregisterLibusbFds() {
    if (usbContext_) {
        libusb_set_pollfd_notifiers(usbContext_, nullptr, nullptr, nullptr);
    }
    for (auto& [fd, watch] : libusbWatches_) {
        l_io_destroy(watch);
    }
    libusbWatches_.clear();
}

void DeviceManager::onLibusbFdAdded(int fd, short events, void* userData) {
    auto* self = static_cast<DeviceManager*>(userData);
    auto* watch = l_io_new(fd);
    if (events & POLLIN)
        l_io_set_read_handler(watch, onLibusbFdReady, self, nullptr);
    if (events & POLLOUT)
        l_io_set_write_handler(watch, onLibusbFdReady, self, nullptr);
    self->libusbWatches_[fd] = watch;
}

void DeviceManager::onLibusbFdRemoved(int fd, void* userData) {
    auto* self = static_cast<DeviceManager*>(userData);
    auto it = self->libusbWatches_.find(fd);
    if (it != self->libusbWatches_.end()) {
        l_io_destroy(it->second);
        self->libusbWatches_.erase(it);
    }
}

bool DeviceManager::onLibusbFdReady(struct l_io* /*io*/, void* userData) {
    auto* self = static_cast<DeviceManager*>(userData);
    self->handleLibusbEvents();
    return true;
}

void DeviceManager::handleLibusbEvents() {
    struct timeval tv = {0, 0};
    libusb_handle_events_timeout_completed(usbContext_, &tv, nullptr);
}

// ══════════════════════════════════════════════════════════════
// TCP Listener
// ══════════════════════════════════════════════════════════════

void DeviceManager::setupTCPListener(uint16_t port) {
    listenFd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) {
        DM_LOG(error) << "DeviceManager: socket() failed: " << std::strerror(errno);
        return;
    }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        DM_LOG(error) << "DeviceManager: bind() failed on port " << port << ": "
                   << std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return;
    }

    if (listen(listenFd_, 1) < 0) {
        DM_LOG(error) << "DeviceManager: listen() failed: " << std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return;
    }

    listenIo_ = l_io_new(listenFd_);
    l_io_set_read_handler(listenIo_, onTCPAcceptable, this, nullptr);
    DM_LOG(info) << "DeviceManager: TCP listener on port " << port;
}

void DeviceManager::teardownTCPListener() {
    if (listenIo_) {
        l_io_destroy(listenIo_);
        listenIo_ = nullptr;
    }
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
}

bool DeviceManager::onTCPAcceptable(struct l_io* /*io*/, void* userData) {
    auto* self = static_cast<DeviceManager*>(userData);

    sockaddr_in clientAddr{};
    socklen_t len = sizeof(clientAddr);
    int clientFd = accept4(self->listenFd_, reinterpret_cast<sockaddr*>(&clientAddr),
                           &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (clientFd < 0) return true;

    char addrStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
    std::string peerAddr(addrStr);
    std::string id = "wifi:" + peerAddr;

    DM_LOG(info) << "DeviceManager: WiFi client connected from " << peerAddr;

    if (self->onWifiClientConnected) {
        // Hand the raw fd to the consumer (e.g. App creates a Boost socket)
        self->onWifiClientConnected(clientFd, peerAddr);
    } else {
        // No consumer — register as a WifiDevice in the device list
        DeviceInfo info{id, peerAddr, DeviceInfo::Transport::Wifi};
        auto device = std::make_unique<WifiDevice>(std::move(info), clientFd);
        auto& ref = *device;
        self->devices_[id] = std::move(device);
        if (self->onDeviceFound) self->onDeviceFound(ref.info());
    }

    return true; // keep listening
}

// ══════════════════════════════════════════════════════════════
// Bluetooth WiFi Projection
// ══════════════════════════════════════════════════════════════

// ── D-Bus Profile callbacks (static) ──

void DeviceManager::setupProfileInterface(struct l_dbus_interface* iface) {
    l_dbus_interface_method(iface, "Release", 0, onProfileRelease, "", "");
    l_dbus_interface_method(iface, "NewConnection", 0, onProfileNewConnection, "", "oha{sv}");
    l_dbus_interface_method(iface, "RequestDisconnection", 0, onProfileDisconnection, "", "o");
}

l_dbus_message* DeviceManager::onProfileRelease(struct l_dbus*, struct l_dbus_message* msg, void*) {
    DM_LOG(info) << "DeviceManager: BT profile released";
    return l_dbus_message_new_method_return(msg);
}

l_dbus_message* DeviceManager::onProfileNewConnection(struct l_dbus*, struct l_dbus_message* msg, void* ud) {
    auto* self = static_cast<DeviceManager*>(ud);
    const char* devicePath = nullptr;
    int fd = -1;
    struct l_dbus_message_iter props;

    if (!l_dbus_message_get_arguments(msg, "oha{sv}", &devicePath, &fd, &props)) {
        return l_dbus_message_new_error(msg, "org.bluez.Error.InvalidArguments",
                                        "Invalid NewConnection args");
    }

    if (self && devicePath && fd >= 0) {
        int dupFd = ::dup(fd);
        if (dupFd >= 0) {
            self->onBtNewConnection(dupFd, devicePath);
        } else {
            DM_LOG(error) << "DeviceManager: failed to dup BT socket fd";
        }
    }

    return l_dbus_message_new_method_return(msg);
}

l_dbus_message* DeviceManager::onProfileDisconnection(struct l_dbus*, struct l_dbus_message* msg, void* ud) {
    auto* self = static_cast<DeviceManager*>(ud);
    const char* devicePath = nullptr;

    if (!l_dbus_message_get_arguments(msg, "o", &devicePath)) {
        return l_dbus_message_new_error(msg, "org.bluez.Error.InvalidArguments",
                                        "Invalid RequestDisconnection args");
    }

    if (self && devicePath) {
        self->onBtDisconnection(devicePath);
    }

    return l_dbus_message_new_method_return(msg);
}

// ── Bluetooth setup / teardown ──

bool DeviceManager::setupBluetooth() {
    bus_ = l_dbus_new_default(L_DBUS_SYSTEM_BUS);
    if (!bus_) {
        DM_LOG(error) << "DeviceManager: failed to connect to system D-Bus";
        return false;
    }

    if (!dmDbusWaitReady(bus_, kDbusTimeoutMs)) {
        DM_LOG(error) << "DeviceManager: D-Bus not ready (timeout)";
        l_dbus_destroy(bus_);
        bus_ = nullptr;
        return false;
    }
    DM_LOG(info) << "DeviceManager: D-Bus ready";

    if (!dmDbusNameHasOwner(bus_, kBluezSvc, kDbusTimeoutMs)) {
        DM_LOG(error) << "DeviceManager: org.bluez not available on D-Bus";
        l_dbus_destroy(bus_);
        bus_ = nullptr;
        return false;
    }

    adapterPath_ = resolveAdapterPath(config_.bluetoothAdapterAddress);
    if (adapterPath_.empty()) {
        DM_LOG(error) << "DeviceManager: no BlueZ adapter found";
        l_dbus_destroy(bus_);
        bus_ = nullptr;
        return false;
    }
    DM_LOG(info) << "DeviceManager: using BT adapter " << adapterPath_;

    // Configure adapter
    const bool on = true;
    const uint32_t zero = 0;
    setAdapterProperty(adapterPath_, "Powered", 'b', &on);
    setAdapterProperty(adapterPath_, "Discoverable", 'b', &on);
    setAdapterProperty(adapterPath_, "Pairable", 'b', &on);
    setAdapterProperty(adapterPath_, "DiscoverableTimeout", 'u', &zero);
    setAdapterProperty(adapterPath_, "PairableTimeout", 'u', &zero);

    // Register D-Bus profile interface
    l_dbus_register_interface(bus_, kProfileIface, setupProfileInterface, nullptr, false);
    if (!l_dbus_object_add_interface(bus_, kProfileObjPath, kProfileIface, this)) {
        DM_LOG(error) << "DeviceManager: failed to add profile interface at " << kProfileObjPath;
        l_dbus_destroy(bus_);
        bus_ = nullptr;
        return false;
    }

    // Register profile with BlueZ
    auto* regMsg = l_dbus_message_new_method_call(bus_, kBluezSvc, "/org/bluez",
                                                  "org.bluez.ProfileManager1", "RegisterProfile");
    auto* b = l_dbus_message_builder_new(regMsg);
    l_dbus_message_builder_append_basic(b, 'o', kProfileObjPath);
    l_dbus_message_builder_append_basic(b, 's', kProfileUuid);

    l_dbus_message_builder_enter_array(b, "{sv}");

    // Name
    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "Name");
    l_dbus_message_builder_enter_variant(b, "s");
    l_dbus_message_builder_append_basic(b, 's', "OpenAuto Bluetooth Service");
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    // Role
    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "Role");
    l_dbus_message_builder_enter_variant(b, "s");
    l_dbus_message_builder_append_basic(b, 's', "server");
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    // Channel
    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "Channel");
    l_dbus_message_builder_enter_variant(b, "q");
    l_dbus_message_builder_append_basic(b, 'q', &kBtChannel);
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    // Service UUID
    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "Service");
    l_dbus_message_builder_enter_variant(b, "s");
    l_dbus_message_builder_append_basic(b, 's', kProfileUuid);
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    // RequireAuthentication
    const bool no = false;
    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "RequireAuthentication");
    l_dbus_message_builder_enter_variant(b, "b");
    l_dbus_message_builder_append_basic(b, 'b', &no);
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    // RequireAuthorization
    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "RequireAuthorization");
    l_dbus_message_builder_enter_variant(b, "b");
    l_dbus_message_builder_append_basic(b, 'b', &no);
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    // AutoConnect
    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "AutoConnect");
    l_dbus_message_builder_enter_variant(b, "b");
    l_dbus_message_builder_append_basic(b, 'b', &on);
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    l_dbus_message_builder_leave_array(b);
    l_dbus_message_builder_finalize(b);
    l_dbus_message_builder_destroy(b);

    auto* reply = dmDbusSendSync(bus_, regMsg, kDbusTimeoutMs);
    if (!reply || l_dbus_message_is_error(reply)) {
        if (reply) {
            const char* eName = nullptr;
            const char* eText = nullptr;
            l_dbus_message_get_error(reply, &eName, &eText);
            DM_LOG(error) << "DeviceManager: RegisterProfile failed: "
                          << (eName ? eName : "unknown") << " " << (eText ? eText : "");
            l_dbus_message_unref(reply);
        }
        l_dbus_object_remove_interface(bus_, kProfileObjPath, kProfileIface);
        l_dbus_destroy(bus_);
        bus_ = nullptr;
        return false;
    }
    l_dbus_message_unref(reply);

    DM_LOG(info) << "DeviceManager: BT profile registered on channel " << kBtChannel;
    return true;
}

void DeviceManager::teardownBluetooth() {
    stopBtReadLoop(false);

    if (bus_) {
        l_dbus_object_remove_interface(bus_, kProfileObjPath, kProfileIface);
        l_dbus_destroy(bus_);
        bus_ = nullptr;
    }
}

std::string DeviceManager::resolveAdapterPath(const std::string& address) {
    if (!bus_) return "/org/bluez/hci0";

    auto* msg = l_dbus_message_new_method_call(bus_, kBluezSvc, "/",
                                               kObjMgrIface, "GetManagedObjects");
    l_dbus_message_set_arguments(msg, "");
    auto* reply = dmDbusSendSync(bus_, msg, kDbusTimeoutMs);
    if (!reply || l_dbus_message_is_error(reply)) {
        if (reply) l_dbus_message_unref(reply);
        return "/org/bluez/hci0";
    }

    struct l_dbus_message_iter objects;
    if (!l_dbus_message_get_arguments(reply, "a{oa{sa{sv}}}", &objects)) {
        l_dbus_message_unref(reply);
        return "/org/bluez/hci0";
    }

    const char* path = nullptr;
    struct l_dbus_message_iter object;
    while (l_dbus_message_iter_next_entry(&objects, &path, &object)) {
        const char* iface = nullptr;
        struct l_dbus_message_iter properties;
        while (l_dbus_message_iter_next_entry(&object, &iface, &properties)) {
            if (!iface || std::strcmp(iface, kAdapterIface) != 0) continue;

            if (address.empty() && path) {
                l_dbus_message_unref(reply);
                return path;
            }

            std::string adapterAddr;
            if (!dmGetPropertyString(properties, "Address", adapterAddr)) continue;

            std::string addrLower = adapterAddr;
            std::string targetLower = address;
            std::transform(addrLower.begin(), addrLower.end(), addrLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (addrLower == targetLower) {
                l_dbus_message_unref(reply);
                return path ? path : "/org/bluez/hci0";
            }
        }
    }

    l_dbus_message_unref(reply);
    return "/org/bluez/hci0";
}

bool DeviceManager::setAdapterProperty(const std::string& path, const std::string& name,
                                       char sig, const void* value) {
    if (!bus_) return false;

    auto* msg = l_dbus_message_new_method_call(bus_, kBluezSvc,
                                               path.c_str(), kPropsIface, "Set");
    auto* b = l_dbus_message_builder_new(msg);
    l_dbus_message_builder_append_basic(b, 's', kAdapterIface);
    l_dbus_message_builder_append_basic(b, 's', name.c_str());
    const char sigStr[2] = {sig, '\0'};
    l_dbus_message_builder_enter_variant(b, sigStr);
    l_dbus_message_builder_append_basic(b, sig, value);
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_finalize(b);
    l_dbus_message_builder_destroy(b);

    auto* reply = dmDbusSendSync(bus_, msg, kDbusTimeoutMs);
    if (!reply) return false;
    bool ok = !l_dbus_message_is_error(reply);
    if (!ok) {
        const char* eName = nullptr;
        const char* eText = nullptr;
        l_dbus_message_get_error(reply, &eName, &eText);
        DM_LOG(warning) << "DeviceManager: failed setting " << name
                        << ": " << (eName ? eName : "") << " " << (eText ? eText : "");
    }
    l_dbus_message_unref(reply);
    return ok;
}

// ── BT connection handling ──

void DeviceManager::onBtNewConnection(int fd, const std::string& devicePath) {
    DM_LOG(info) << "DeviceManager: BT connection from " << devicePath;

    stopBtReadLoop(false);
    btSocketFd_ = fd;
    startBtReadLoop();

    // Send version request (empty payload)
    sendBtFrame(BT_WIFI_VERSION_REQUEST, nullptr, 0);

    // Detect WiFi interface and send start request
    auto wifiInfo = getWifiInterfaceInfo();
    if (wifiInfo.ip.empty()) {
        DM_LOG(error) << "DeviceManager: no IPv4 interface found for WiFi projection";
        return;
    }
    wifiInterfaceName_ = wifiInfo.name;
    DM_LOG(info) << "DeviceManager: using WiFi interface " << wifiInterfaceName_
                 << " ip=" << wifiInfo.ip;

    // Encode WifiStartRequest: field 1 = ip (string), field 2 = port (int32)
    std::vector<uint8_t> payload;
    pbWriteString(payload, 1, wifiInfo.ip);
    pbWriteInt32(payload, 2, static_cast<int32_t>(config_.wifiPort));
    sendBtFrame(BT_WIFI_START_REQUEST, payload.data(), payload.size());
    DM_LOG(info) << "DeviceManager: sent WIFI_START_REQUEST ip=" << wifiInfo.ip
                 << " port=" << config_.wifiPort;
}

void DeviceManager::onBtDisconnection(const std::string& devicePath) {
    DM_LOG(info) << "DeviceManager: BT disconnected " << devicePath;
    stopBtReadLoop(false);
}

// ── BT read loop (dedicated thread) ──

void DeviceManager::startBtReadLoop() {
    if (btSocketFd_ < 0) return;
    btReading_.store(true);
    btReaderThread_ = std::thread(&DeviceManager::btReadLoop, this);
}

void DeviceManager::stopBtReadLoop(bool fromReader) {
    btReading_.store(false);
    if (btSocketFd_ >= 0) {
        ::shutdown(btSocketFd_, SHUT_RDWR);
        ::close(btSocketFd_);
        btSocketFd_ = -1;
    }
    if (!fromReader && btReaderThread_.joinable()) {
        btReaderThread_.join();
    }
    btBuffer_.clear();
}

void DeviceManager::btReadLoop() {
    while (btReading_.load()) {
        uint8_t temp[4096];
        auto bytes = ::read(btSocketFd_, temp, sizeof(temp));
        if (bytes < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            DM_LOG(warning) << "DeviceManager: BT socket read failed: " << strerror(errno);
            break;
        }
        if (bytes == 0) {
            DM_LOG(info) << "DeviceManager: BT socket closed by peer";
            break;
        }

        btBuffer_.insert(btBuffer_.end(), temp, temp + bytes);

        while (btBuffer_.size() >= 4) {
            uint16_t length = btFrameReadU16(btBuffer_, 0);
            if (btBuffer_.size() < static_cast<size_t>(length + 4)) break;

            uint16_t msgId = btFrameReadU16(btBuffer_, 2);
            const uint8_t* payload = btBuffer_.data() + 4;

            switch (msgId) {
                case BT_WIFI_INFO_REQUEST:
                    handleWifiInfoRequest();
                    break;
                case BT_WIFI_VERSION_RESPONSE:
                    handleWifiVersionResponse(payload, length);
                    break;
                case BT_WIFI_CONNECTION_STATUS:
                    handleWifiConnectionStatus(payload, length);
                    break;
                case BT_WIFI_START_RESPONSE:
                    handleWifiStartResponse(payload, length);
                    break;
                default:
                    DM_LOG(info) << "DeviceManager: BT unknown message id=" << msgId
                                << " len=" << length;
                    break;
            }

            btBuffer_.erase(btBuffer_.begin(), btBuffer_.begin() + length + 4);
        }
    }
    DM_LOG(info) << "DeviceManager: BT read loop exiting";
    stopBtReadLoop(true);
}

// ── BT message framing ──

void DeviceManager::sendBtFrame(uint16_t messageId, const uint8_t* payload, size_t len) {
    if (btSocketFd_ < 0) return;

    std::vector<uint8_t> frame(len + 4, 0);
    btFrameWriteU16(frame, 0, static_cast<uint16_t>(len));
    btFrameWriteU16(frame, 2, messageId);
    if (payload && len > 0) {
        std::memcpy(frame.data() + 4, payload, len);
    }

    auto written = ::write(btSocketFd_, frame.data(), frame.size());
    if (written < 0) {
        DM_LOG(warning) << "DeviceManager: BT socket write failed: " << strerror(errno);
    }
}

// ── WiFi handshake message handlers ──

void DeviceManager::handleWifiInfoRequest() {
    DM_LOG(info) << "DeviceManager: handling WIFI_INFO_REQUEST";

    std::string interfaceName = wifiInterfaceName_;
    if (interfaceName.empty()) {
        interfaceName = getWifiInterfaceInfo().name;
    }

    auto bssid = getMacAddress(interfaceName);
    if (bssid.empty()) bssid = "00:00:00:00:00:00";

    // Encode WifiInfoResponse
    std::vector<uint8_t> payload;
    pbWriteString(payload, 1, config_.wifiSSID);       // ssid
    pbWriteString(payload, 2, config_.wifiPassword);   // password
    pbWriteString(payload, 3, bssid);                  // bssid
    pbWriteInt32(payload, 4, kWPA2Personal);           // security_mode
    pbWriteInt32(payload, 5, kAccessPointStatic);      // access_point_type

    sendBtFrame(BT_WIFI_INFO_RESPONSE, payload.data(), payload.size());
    DM_LOG(info) << "DeviceManager: sent WIFI_INFO_RESPONSE ssid=" << config_.wifiSSID
                 << " bssid=" << bssid << " iface=" << interfaceName;
}

void DeviceManager::handleWifiVersionResponse(const uint8_t* payload, size_t len) {
    DM_LOG(info) << "DeviceManager: WIFI_VERSION_RESPONSE received (len=" << len << ")";
    // Parse for logging — 4 varint fields
    const uint8_t* p = payload;
    const uint8_t* end = payload + len;
    while (p < end) {
        PbField f;
        if (!pbReadField(p, end, f)) break;
        DM_LOG(info) << "DeviceManager:   field " << f.fieldNum << " = " << f.varintVal;
    }
}

void DeviceManager::handleWifiStartResponse(const uint8_t* payload, size_t len) {
    DM_LOG(info) << "DeviceManager: WIFI_START_RESPONSE received";
    std::string ip;
    int32_t port = 0;
    int32_t status = 0;
    const uint8_t* p = payload;
    const uint8_t* end = payload + len;
    while (p < end) {
        PbField f;
        if (!pbReadField(p, end, f)) break;
        if (f.fieldNum == 1 && f.wireType == 2)
            ip.assign(reinterpret_cast<const char*>(f.bytesData), f.bytesLen);
        else if (f.fieldNum == 2 && f.wireType == 0)
            port = static_cast<int32_t>(f.varintVal);
        else if (f.fieldNum == 3 && f.wireType == 0)
            status = static_cast<int32_t>(f.varintVal);
    }
    DM_LOG(info) << "DeviceManager:   ip=" << ip << " port=" << port
                 << " status=" << wifiStatusName(status);
}

void DeviceManager::handleWifiConnectionStatus(const uint8_t* payload, size_t len) {
    int32_t status = 0;
    const uint8_t* p = payload;
    const uint8_t* end = payload + len;
    while (p < end) {
        PbField f;
        if (!pbReadField(p, end, f)) break;
        if (f.fieldNum == 1 && f.wireType == 0)
            status = static_cast<int32_t>(f.varintVal);
    }
    DM_LOG(info) << "DeviceManager: WIFI_CONNECTION_STATUS: " << wifiStatusName(status);
}

// ── WiFi interface detection ──

DeviceManager::WifiInterfaceInfo DeviceManager::getWifiInterfaceInfo() const {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) return {};

    WifiInterfaceInfo fallback;
    for (auto* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) continue;

        char host[INET_ADDRSTRLEN];
        auto* addr = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
        if (!inet_ntop(AF_INET, &addr->sin_addr, host, sizeof(host))) continue;

        std::string name = ifa->ifa_name;
        WifiInterfaceInfo current{name, host};

        if (!config_.wifiInterface.empty() && name == config_.wifiInterface) {
            freeifaddrs(ifaddr);
            return current;
        }

        if (isWirelessInterface(name)) {
            freeifaddrs(ifaddr);
            return current;
        }

        if (fallback.name.empty()) fallback = current;
    }

    freeifaddrs(ifaddr);
    return fallback;
}

std::string DeviceManager::getMacAddress(const std::string& intf) const {
    if (intf.empty()) return {};
    std::ifstream file("/sys/class/net/" + intf + "/address");
    if (!file.is_open()) return {};
    std::string mac;
    std::getline(file, mac);
    return mac;
}
