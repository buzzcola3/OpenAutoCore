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
#include <DeviceManager/WirelessDeviceManager.hpp>
#include <DeviceManager/WifiDevice.hpp>

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
    if (val >= 0) {
        pbWriteVarint(buf, static_cast<uint64_t>(val));
    } else {
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

// ── D-Bus synchronous helpers ──

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

WirelessDeviceManager::WirelessDeviceManager(WirelessDeviceManagerConfig config)
    : config_(std::move(config)) {}

WirelessDeviceManager::~WirelessDeviceManager() {
    stop();
}

// ══════════════════════════════════════════════════════════════
// Lifecycle
// ══════════════════════════════════════════════════════════════

void WirelessDeviceManager::start() {
    if (running_) return;
    running_ = true;

    setupWakeup();

    if (config_.wifiPort > 0) {
        setupTCPListener(config_.wifiPort);
    }

    if (config_.bluetoothEnabled) {
        setupBluetooth();
    }

    DM_LOG(info) << "WirelessDeviceManager: started"
                 << (config_.wifiPort > 0 ? " TCP:" + std::to_string(config_.wifiPort) : "")
                 << (config_.bluetoothEnabled ? " + BT" : "");
}

void WirelessDeviceManager::stop() {
    if (!running_) return;
    running_ = false;

    teardownTCPListener();
    teardownBluetooth();
    teardownWakeup();

    DM_LOG(info) << "WirelessDeviceManager: stopped";
}

// ══════════════════════════════════════════════════════════════
// Command Queue (thread-safe → ELL thread)
// ══════════════════════════════════════════════════════════════

void WirelessDeviceManager::setupWakeup() {
    eventFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (eventFd_ < 0) {
        DM_LOG(error) << "WirelessDeviceManager: eventfd() failed: " << std::strerror(errno);
        return;
    }
    wakeupIo_ = l_io_new(eventFd_);
    l_io_set_read_handler(wakeupIo_, onWakeup, this, nullptr);
}

void WirelessDeviceManager::teardownWakeup() {
    if (wakeupIo_) {
        l_io_destroy(wakeupIo_);
        wakeupIo_ = nullptr;
    }
    if (eventFd_ >= 0) {
        ::close(eventFd_);
        eventFd_ = -1;
    }
}

bool WirelessDeviceManager::onWakeup(struct l_io*, void* userData) {
    auto* self = static_cast<WirelessDeviceManager*>(userData);
    uint64_t val;
    (void)::read(self->eventFd_, &val, sizeof(val));
    self->drainCommands();
    return true;
}

void WirelessDeviceManager::wakeEllThread() {
    uint64_t val = 1;
    (void)::write(eventFd_, &val, sizeof(val));
}

void WirelessDeviceManager::drainCommands() {
    std::vector<PendingCommand> commands;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commands.swap(commandQueue_);
    }
    for (auto& cmd : commands) {
        switch (cmd.type) {
            case Command::BeginWifi:   doBeginWifiProjection(); break;
            case Command::ReconnectBt: doReconnectBluetooth(); break;
        }
    }
}

void WirelessDeviceManager::beginWifiProjection() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commandQueue_.push_back({Command::BeginWifi});
    }
    wakeEllThread();
}

void WirelessDeviceManager::reconnectBluetooth() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commandQueue_.push_back({Command::ReconnectBt});
    }
    wakeEllThread();
}

void WirelessDeviceManager::doBeginWifiProjection() {
    if (btSocketFd_ < 0) {
        DM_LOG(warning) << "WirelessDeviceManager: beginWifiProjection: no BT socket";
        return;
    }

    startBtReadLoop();

    sendBtFrame(BT_WIFI_VERSION_REQUEST, nullptr, 0);

    auto wifiInfo = getWifiInterfaceInfo();
    if (wifiInfo.ip.empty()) {
        DM_LOG(error) << "WirelessDeviceManager: no IPv4 interface found for WiFi projection";
        return;
    }
    wifiInterfaceName_ = wifiInfo.name;
    DM_LOG(info) << "WirelessDeviceManager: using WiFi interface " << wifiInterfaceName_
                 << " ip=" << wifiInfo.ip;

    std::vector<uint8_t> payload;
    pbWriteString(payload, 1, wifiInfo.ip);
    pbWriteInt32(payload, 2, static_cast<int32_t>(config_.wifiPort));
    sendBtFrame(BT_WIFI_START_REQUEST, payload.data(), payload.size());
    DM_LOG(info) << "WirelessDeviceManager: sent WIFI_START_REQUEST ip=" << wifiInfo.ip
                 << " port=" << config_.wifiPort;
}

void WirelessDeviceManager::doReconnectBluetooth() {
    if (!bus_ || btDevicePath_.empty()) {
        DM_LOG(warning) << "WirelessDeviceManager: reconnectBluetooth: no BT device path";
        return;
    }

    stopBtReadLoop(false);

    DM_LOG(info) << "WirelessDeviceManager: cycling BT connection for " << btDevicePath_;

    auto* disconnMsg = l_dbus_message_new_method_call(bus_, kBluezSvc,
        btDevicePath_.c_str(), "org.bluez.Device1", "Disconnect");
    l_dbus_message_set_arguments(disconnMsg, "");

    l_dbus_send_with_reply(bus_, disconnMsg,
        [](l_dbus_message* reply, void* userData) {
            auto* self = static_cast<WirelessDeviceManager*>(userData);
            if (reply && l_dbus_message_is_error(reply)) {
                const char* eName = nullptr;
                const char* eText = nullptr;
                l_dbus_message_get_error(reply, &eName, &eText);
                DM_LOG(warning) << "WirelessDeviceManager: BT Disconnect: "
                               << (eName ? eName : "") << " " << (eText ? eText : "");
            }
            DM_LOG(info) << "WirelessDeviceManager: BT disconnected, sending Connect...";

            auto* connMsg = l_dbus_message_new_method_call(self->bus_, kBluezSvc,
                self->btDevicePath_.c_str(), "org.bluez.Device1", "Connect");
            l_dbus_message_set_arguments(connMsg, "");
            l_dbus_send_with_reply(self->bus_, connMsg,
                [](l_dbus_message* reply2, void*) {
                    if (reply2 && l_dbus_message_is_error(reply2)) {
                        const char* eName = nullptr;
                        const char* eText = nullptr;
                        l_dbus_message_get_error(reply2, &eName, &eText);
                        DM_LOG(warning) << "WirelessDeviceManager: BT Connect: "
                                       << (eName ? eName : "") << " " << (eText ? eText : "");
                    } else {
                        DM_LOG(info) << "WirelessDeviceManager: BT reconnect cycle complete";
                    }
                }, nullptr, nullptr);
        }, this, nullptr);
}

// ══════════════════════════════════════════════════════════════
// TCP Listener
// ══════════════════════════════════════════════════════════════

void WirelessDeviceManager::setupTCPListener(uint16_t port) {
    listenFd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listenFd_ < 0) {
        DM_LOG(error) << "WirelessDeviceManager: socket() failed: " << std::strerror(errno);
        return;
    }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        DM_LOG(error) << "WirelessDeviceManager: bind() failed on port " << port << ": "
                      << std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return;
    }

    if (listen(listenFd_, 1) < 0) {
        DM_LOG(error) << "WirelessDeviceManager: listen() failed: " << std::strerror(errno);
        ::close(listenFd_);
        listenFd_ = -1;
        return;
    }

    listenIo_ = l_io_new(listenFd_);
    l_io_set_read_handler(listenIo_, onTCPAcceptable, this, nullptr);
    DM_LOG(info) << "WirelessDeviceManager: TCP listener on port " << port;
}

void WirelessDeviceManager::teardownTCPListener() {
    if (listenIo_) {
        l_io_destroy(listenIo_);
        listenIo_ = nullptr;
    }
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
}

bool WirelessDeviceManager::onTCPAcceptable(struct l_io*, void* userData) {
    auto* self = static_cast<WirelessDeviceManager*>(userData);

    sockaddr_in clientAddr{};
    socklen_t len = sizeof(clientAddr);
    int clientFd = accept4(self->listenFd_, reinterpret_cast<sockaddr*>(&clientAddr),
                           &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (clientFd < 0) return true;

    char addrStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, sizeof(addrStr));
    std::string peerAddr(addrStr);

    DM_LOG(info) << "WirelessDeviceManager: WiFi client connected from " << peerAddr;

    if (self->onWifiClientConnected) {
        self->onWifiClientConnected(self->deviceId_, clientFd, peerAddr);
    } else {
        ::close(clientFd);
    }

    return true;
}

// ══════════════════════════════════════════════════════════════
// Bluetooth WiFi Projection
// ══════════════════════════════════════════════════════════════

// ── D-Bus Profile callbacks (static) ──

void WirelessDeviceManager::setupProfileInterface(struct l_dbus_interface* iface) {
    l_dbus_interface_method(iface, "Release", 0, onProfileRelease, "", "");
    l_dbus_interface_method(iface, "NewConnection", 0, onProfileNewConnection, "", "oha{sv}");
    l_dbus_interface_method(iface, "RequestDisconnection", 0, onProfileDisconnection, "", "o");
}

l_dbus_message* WirelessDeviceManager::onProfileRelease(struct l_dbus*, struct l_dbus_message* msg, void*) {
    DM_LOG(info) << "WirelessDeviceManager: BT profile released";
    return l_dbus_message_new_method_return(msg);
}

l_dbus_message* WirelessDeviceManager::onProfileNewConnection(struct l_dbus*, struct l_dbus_message* msg, void* ud) {
    auto* self = static_cast<WirelessDeviceManager*>(ud);
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
            DM_LOG(error) << "WirelessDeviceManager: failed to dup BT socket fd";
        }
    }

    return l_dbus_message_new_method_return(msg);
}

l_dbus_message* WirelessDeviceManager::onProfileDisconnection(struct l_dbus*, struct l_dbus_message* msg, void* ud) {
    auto* self = static_cast<WirelessDeviceManager*>(ud);
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

bool WirelessDeviceManager::setupBluetooth() {
    bus_ = l_dbus_new_default(L_DBUS_SYSTEM_BUS);
    if (!bus_) {
        DM_LOG(error) << "WirelessDeviceManager: failed to connect to system D-Bus";
        return false;
    }

    if (!dmDbusWaitReady(bus_, kDbusTimeoutMs)) {
        DM_LOG(error) << "WirelessDeviceManager: D-Bus not ready (timeout)";
        l_dbus_destroy(bus_);
        bus_ = nullptr;
        return false;
    }
    DM_LOG(info) << "WirelessDeviceManager: D-Bus ready";

    if (!dmDbusNameHasOwner(bus_, kBluezSvc, kDbusTimeoutMs)) {
        DM_LOG(error) << "WirelessDeviceManager: org.bluez not available on D-Bus";
        l_dbus_destroy(bus_);
        bus_ = nullptr;
        return false;
    }

    adapterPath_ = resolveAdapterPath(config_.bluetoothAdapterAddress);
    if (adapterPath_.empty()) {
        DM_LOG(error) << "WirelessDeviceManager: no BlueZ adapter found";
        l_dbus_destroy(bus_);
        bus_ = nullptr;
        return false;
    }
    DM_LOG(info) << "WirelessDeviceManager: using BT adapter " << adapterPath_;

    const bool on = true;
    const uint32_t zero = 0;
    setAdapterProperty(adapterPath_, "Powered", 'b', &on);
    setAdapterProperty(adapterPath_, "Discoverable", 'b', &on);
    setAdapterProperty(adapterPath_, "Pairable", 'b', &on);
    setAdapterProperty(adapterPath_, "DiscoverableTimeout", 'u', &zero);
    setAdapterProperty(adapterPath_, "PairableTimeout", 'u', &zero);

    l_dbus_register_interface(bus_, kProfileIface, setupProfileInterface, nullptr, false);
    if (!l_dbus_object_add_interface(bus_, kProfileObjPath, kProfileIface, this)) {
        DM_LOG(error) << "WirelessDeviceManager: failed to add profile interface at " << kProfileObjPath;
        l_dbus_destroy(bus_);
        bus_ = nullptr;
        return false;
    }

    auto* regMsg = l_dbus_message_new_method_call(bus_, kBluezSvc, "/org/bluez",
                                                  "org.bluez.ProfileManager1", "RegisterProfile");
    auto* b = l_dbus_message_builder_new(regMsg);
    l_dbus_message_builder_append_basic(b, 'o', kProfileObjPath);
    l_dbus_message_builder_append_basic(b, 's', kProfileUuid);

    l_dbus_message_builder_enter_array(b, "{sv}");

    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "Name");
    l_dbus_message_builder_enter_variant(b, "s");
    l_dbus_message_builder_append_basic(b, 's', "OpenAuto Bluetooth Service");
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "Role");
    l_dbus_message_builder_enter_variant(b, "s");
    l_dbus_message_builder_append_basic(b, 's', "server");
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "Channel");
    l_dbus_message_builder_enter_variant(b, "q");
    l_dbus_message_builder_append_basic(b, 'q', &kBtChannel);
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "Service");
    l_dbus_message_builder_enter_variant(b, "s");
    l_dbus_message_builder_append_basic(b, 's', kProfileUuid);
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    const bool no = false;
    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "RequireAuthentication");
    l_dbus_message_builder_enter_variant(b, "b");
    l_dbus_message_builder_append_basic(b, 'b', &no);
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

    l_dbus_message_builder_enter_dict(b, "sv");
    l_dbus_message_builder_append_basic(b, 's', "RequireAuthorization");
    l_dbus_message_builder_enter_variant(b, "b");
    l_dbus_message_builder_append_basic(b, 'b', &no);
    l_dbus_message_builder_leave_variant(b);
    l_dbus_message_builder_leave_dict(b);

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
            DM_LOG(error) << "WirelessDeviceManager: RegisterProfile failed: "
                          << (eName ? eName : "unknown") << " " << (eText ? eText : "");
            l_dbus_message_unref(reply);
        }
        l_dbus_object_remove_interface(bus_, kProfileObjPath, kProfileIface);
        l_dbus_destroy(bus_);
        bus_ = nullptr;
        return false;
    }
    l_dbus_message_unref(reply);

    DM_LOG(info) << "WirelessDeviceManager: BT profile registered on channel " << kBtChannel;
    return true;
}

void WirelessDeviceManager::teardownBluetooth() {
    stopBtReadLoop(false);

    if (bus_) {
        l_dbus_object_remove_interface(bus_, kProfileObjPath, kProfileIface);
        l_dbus_destroy(bus_);
        bus_ = nullptr;
    }
}

std::string WirelessDeviceManager::resolveAdapterPath(const std::string& address) {
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

bool WirelessDeviceManager::setAdapterProperty(const std::string& path, const std::string& name,
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
        DM_LOG(warning) << "WirelessDeviceManager: failed setting " << name
                        << ": " << (eName ? eName : "") << " " << (eText ? eText : "");
    }
    l_dbus_message_unref(reply);
    return ok;
}

// ── BT connection handling ──

void WirelessDeviceManager::onBtNewConnection(int fd, const std::string& devicePath) {
    DM_LOG(info) << "WirelessDeviceManager: BT connection from " << devicePath;

    stopBtReadLoop(false);
    btSocketFd_ = fd;
    btDevicePath_ = devicePath;

    // Extract BT MAC from D-Bus path: /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF → AA:BB:CC:DD:EE:FF
    btAddress_.clear();
    auto pos = devicePath.rfind("/dev_");
    if (pos != std::string::npos) {
        btAddress_ = devicePath.substr(pos + 5);
        std::replace(btAddress_.begin(), btAddress_.end(), '_', ':');
    } else {
        btAddress_ = devicePath;
    }
    deviceId_ = "wireless:" + btAddress_;

    if (onBtDeviceAvailable) {
        onBtDeviceAvailable(deviceId_, btAddress_);
    } else {
        doBeginWifiProjection();
    }
}

void WirelessDeviceManager::onBtDisconnection(const std::string& devicePath) {
    DM_LOG(info) << "WirelessDeviceManager: BT disconnected " << devicePath;
    stopBtReadLoop(false);
}

// ── BT read loop (dedicated thread) ──

void WirelessDeviceManager::startBtReadLoop() {
    if (btSocketFd_ < 0) return;
    btReading_.store(true);
    btReaderThread_ = std::thread(&WirelessDeviceManager::btReadLoop, this);
}

void WirelessDeviceManager::stopBtReadLoop(bool fromReader) {
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

void WirelessDeviceManager::btReadLoop() {
    while (btReading_.load()) {
        uint8_t temp[4096];
        auto bytes = ::read(btSocketFd_, temp, sizeof(temp));
        if (bytes < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            DM_LOG(warning) << "WirelessDeviceManager: BT socket read failed: " << strerror(errno);
            break;
        }
        if (bytes == 0) {
            DM_LOG(info) << "WirelessDeviceManager: BT socket closed by peer";
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
                    DM_LOG(info) << "WirelessDeviceManager: BT unknown message id=" << msgId
                                << " len=" << length;
                    break;
            }

            btBuffer_.erase(btBuffer_.begin(), btBuffer_.begin() + length + 4);
        }
    }
    DM_LOG(info) << "WirelessDeviceManager: BT read loop exiting";
    stopBtReadLoop(true);
}

// ── BT message framing ──

void WirelessDeviceManager::sendBtFrame(uint16_t messageId, const uint8_t* payload, size_t len) {
    if (btSocketFd_ < 0) return;

    std::vector<uint8_t> frame(len + 4, 0);
    btFrameWriteU16(frame, 0, static_cast<uint16_t>(len));
    btFrameWriteU16(frame, 2, messageId);
    if (payload && len > 0) {
        std::memcpy(frame.data() + 4, payload, len);
    }

    auto written = ::write(btSocketFd_, frame.data(), frame.size());
    if (written < 0) {
        DM_LOG(warning) << "WirelessDeviceManager: BT socket write failed: " << strerror(errno);
    }
}

// ── WiFi handshake message handlers ──

void WirelessDeviceManager::handleWifiInfoRequest() {
    DM_LOG(info) << "WirelessDeviceManager: handling WIFI_INFO_REQUEST";

    std::string interfaceName = wifiInterfaceName_;
    if (interfaceName.empty()) {
        interfaceName = getWifiInterfaceInfo().name;
    }

    auto bssid = getMacAddress(interfaceName);
    if (bssid.empty()) bssid = "00:00:00:00:00:00";

    std::vector<uint8_t> payload;
    pbWriteString(payload, 1, config_.wifiSSID);
    pbWriteString(payload, 2, config_.wifiPassword);
    pbWriteString(payload, 3, bssid);
    pbWriteInt32(payload, 4, kWPA2Personal);
    pbWriteInt32(payload, 5, kAccessPointStatic);

    sendBtFrame(BT_WIFI_INFO_RESPONSE, payload.data(), payload.size());
    DM_LOG(info) << "WirelessDeviceManager: sent WIFI_INFO_RESPONSE ssid=" << config_.wifiSSID
                 << " bssid=" << bssid << " iface=" << interfaceName;
}

void WirelessDeviceManager::handleWifiVersionResponse(const uint8_t* payload, size_t len) {
    DM_LOG(info) << "WirelessDeviceManager: WIFI_VERSION_RESPONSE received (len=" << len << ")";
    const uint8_t* p = payload;
    const uint8_t* end = payload + len;
    while (p < end) {
        PbField f;
        if (!pbReadField(p, end, f)) break;
        DM_LOG(info) << "WirelessDeviceManager:   field " << f.fieldNum << " = " << f.varintVal;
    }
}

void WirelessDeviceManager::handleWifiStartResponse(const uint8_t* payload, size_t len) {
    DM_LOG(info) << "WirelessDeviceManager: WIFI_START_RESPONSE received";
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
    DM_LOG(info) << "WirelessDeviceManager:   ip=" << ip << " port=" << port
                 << " status=" << wifiStatusName(status);
}

void WirelessDeviceManager::handleWifiConnectionStatus(const uint8_t* payload, size_t len) {
    int32_t status = 0;
    const uint8_t* p = payload;
    const uint8_t* end = payload + len;
    while (p < end) {
        PbField f;
        if (!pbReadField(p, end, f)) break;
        if (f.fieldNum == 1 && f.wireType == 0)
            status = static_cast<int32_t>(f.varintVal);
    }
    DM_LOG(info) << "WirelessDeviceManager: WIFI_CONNECTION_STATUS: " << wifiStatusName(status);
}

// ── WiFi interface detection ──

WirelessDeviceManager::WifiInterfaceInfo WirelessDeviceManager::getWifiInterfaceInfo() const {
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

std::string WirelessDeviceManager::getMacAddress(const std::string& intf) const {
    if (intf.empty()) return {};
    std::ifstream file("/sys/class/net/" + intf + "/address");
    if (!file.is_open()) return {};
    std::string mac;
    std::getline(file, mac);
    return mac;
}
