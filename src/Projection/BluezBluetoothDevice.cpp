/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*  Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
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
#include <ell/main.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace f1x::openauto::autoapp::projection {

using namespace std::chrono_literals;

namespace {
    std::string toLower(const std::string& input) {
        std::string out = input;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return out;
    }
}

namespace {
    constexpr int kDbusTimeoutMs = 5000;

    void dbusReadyHandler(void* ud) {
        *static_cast<bool*>(ud) = true;
    }

    bool dbusWaitReady(l_dbus* bus, int timeoutMs) {
        bool ready = false;
        if (!l_dbus_set_ready_handler(bus, dbusReadyHandler, &ready, nullptr))
            return false;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!ready) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            l_main_iterate(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    struct ReplyWaiter {
        bool done = false;
        l_dbus_message* reply = nullptr;
    };

    void dbusReplyHandler(l_dbus_message* msg, void* ud) {
        auto* w = static_cast<ReplyWaiter*>(ud);
        w->reply = msg ? l_dbus_message_ref(msg) : nullptr;
        w->done = true;
    }

    l_dbus_message* dbusSendSync(l_dbus* bus, l_dbus_message* msg, int timeoutMs) {
        if (!bus || !msg) return nullptr;
        ReplyWaiter w;
        auto serial = l_dbus_send_with_reply(bus, msg, dbusReplyHandler, &w, nullptr);
        if (serial == 0) {
            l_dbus_message_unref(msg);
            return nullptr;
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!w.done) {
            if (std::chrono::steady_clock::now() >= deadline) return nullptr;
            l_main_iterate(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return w.reply;
    }

    bool getPropertyString(struct l_dbus_message_iter props, const char* key, std::string& out) {
        const char* name = nullptr;
        struct l_dbus_message_iter variant;

        while (l_dbus_message_iter_next_entry(&props, &name, &variant)) {
            if (name == nullptr || std::strcmp(name, key) != 0) {
                continue;
            }

            const char* value = nullptr;
            if (!l_dbus_message_iter_get_variant(&variant, "s", &value) || value == nullptr) {
                return false;
            }

            out = value;
            return true;
        }

        return false;
    }

    bool getPropertyBool(struct l_dbus_message_iter props, const char* key, bool& out) {
        const char* name = nullptr;
        struct l_dbus_message_iter variant;

        while (l_dbus_message_iter_next_entry(&props, &name, &variant)) {
            if (name == nullptr || std::strcmp(name, key) != 0) {
                continue;
            }

            bool value = false;
            if (!l_dbus_message_iter_get_variant(&variant, "b", &value)) {
                return false;
            }

            out = value;
            return true;
        }

        return false;
    }
}

BluezBluetoothDevice::BluezBluetoothDevice(std::string adapterAddress)
    : adapterAddress_(std::move(adapterAddress)) {
    l_main_init();
    bus_.reset(l_dbus_new_default(L_DBUS_SYSTEM_BUS));
    if (bus_) {
        dbusWaitReady(bus_.get(), kDbusTimeoutMs);
    } else {
        OPENAUTO_LOG(error) << "[BluezBluetoothDevice] Failed to create system bus";
    }
}

void BluezBluetoothDevice::stop() {
    bus_.reset();
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
    if (!bus_) {
        return "/org/bluez/hci0";
    }

    auto* msg = l_dbus_message_new_method_call(bus_.get(), "org.bluez", "/",
                                               L_DBUS_INTERFACE_OBJECT_MANAGER,
                                               "GetManagedObjects");
    auto* reply = dbusSendSync(bus_.get(), msg, kDbusTimeoutMs);
    if (reply == nullptr || l_dbus_message_is_error(reply)) {
        if (reply != nullptr) {
            l_dbus_message_unref(reply);
        }
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
        const char* interface = nullptr;
        struct l_dbus_message_iter properties;

        while (l_dbus_message_iter_next_entry(&object, &interface, &properties)) {
            if (interface == nullptr || std::strcmp(interface, "org.bluez.Adapter1") != 0) {
                continue;
            }

            if (adapterAddress_.empty() && path != nullptr) {
                l_dbus_message_unref(reply);
                return path;
            }

            std::string adapterAddr;
            if (!getPropertyString(properties, "Address", adapterAddr)) {
                continue;
            }

            if (toLower(adapterAddr) == toLower(adapterAddress_)) {
                l_dbus_message_unref(reply);
                return path != nullptr ? path : "/org/bluez/hci0";
            }
        }
    }

    l_dbus_message_unref(reply);
    return "/org/bluez/hci0";
}

bool BluezBluetoothDevice::getDevicePaired(const std::string& deviceAddress) const {
    if (!bus_) {
        return false;
    }

    auto* msg = l_dbus_message_new_method_call(bus_.get(), "org.bluez", "/",
                                               L_DBUS_INTERFACE_OBJECT_MANAGER,
                                               "GetManagedObjects");
    auto* reply = dbusSendSync(bus_.get(), msg, kDbusTimeoutMs);
    if (reply == nullptr || l_dbus_message_is_error(reply)) {
        if (reply != nullptr) {
            l_dbus_message_unref(reply);
        }
        return false;
    }

    struct l_dbus_message_iter objects;
    if (!l_dbus_message_get_arguments(reply, "a{oa{sa{sv}}}", &objects)) {
        l_dbus_message_unref(reply);
        return false;
    }

    const auto target = toLower(deviceAddress);
    const char* path = nullptr;
    struct l_dbus_message_iter object;

    while (l_dbus_message_iter_next_entry(&objects, &path, &object)) {
        const char* interface = nullptr;
        struct l_dbus_message_iter properties;

        while (l_dbus_message_iter_next_entry(&object, &interface, &properties)) {
            if (interface == nullptr || std::strcmp(interface, "org.bluez.Device1") != 0) {
                continue;
            }

            std::string addr;
            if (!getPropertyString(properties, "Address", addr)) {
                continue;
            }

            if (toLower(addr) != target) {
                continue;
            }

            bool paired = false;
            const auto found = getPropertyBool(properties, "Paired", paired);
            l_dbus_message_unref(reply);
            return found && paired;
        }
    }

    l_dbus_message_unref(reply);
    return false;
}

}
