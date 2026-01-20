/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
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

#include <btservice/BluezProfile.hpp>
#include <btservice/BluezBluetoothServer.hpp>
#include <Common/Log.hpp>
#include <ell/dbus-service.h>
#include <unistd.h>

namespace f1x::openauto::btservice {

  namespace {
    constexpr const char* kProfileInterface = "org.bluez.Profile1";
  }

  BluezProfile::BluezProfile(l_dbus* bus, std::string objectPath, BluezBluetoothServer* server)
      : objectPath_(std::move(objectPath)),
        server_(server),
        bus_(bus) {
    if (bus_ == nullptr) {
      OPENAUTO_LOG(error) << "[BluezProfile] DBus not available";
      return;
    }

    if (!l_dbus_register_interface(bus_, kProfileInterface, setupInterface, nullptr, false)) {
      OPENAUTO_LOG(debug) << "[BluezProfile] Interface already registered or failed";
    }

    if (!l_dbus_object_add_interface(bus_, objectPath_.c_str(), kProfileInterface, this)) {
      OPENAUTO_LOG(error) << "[BluezProfile] Failed to add interface at " << objectPath_;
    }
  }

  BluezProfile::~BluezProfile() {
    if (bus_ != nullptr) {
      l_dbus_object_remove_interface(bus_, objectPath_.c_str(), kProfileInterface);
    }
  }

  const std::string& BluezProfile::objectPath() const {
    return objectPath_;
  }

  void BluezProfile::setupInterface(struct l_dbus_interface* interface) {
    l_dbus_interface_method(interface, "Release", 0, releaseCallback, "", "");
    l_dbus_interface_method(interface, "NewConnection", 0, newConnectionCallback, "", "oha{sv}");
    l_dbus_interface_method(interface, "RequestDisconnection", 0, requestDisconnectionCallback, "", "o");
  }

  l_dbus_message* BluezProfile::releaseCallback(struct l_dbus*, struct l_dbus_message* message, void* userData) {
    auto* profile = static_cast<BluezProfile*>(userData);
    if (profile != nullptr) {
      profile->release();
    }
    return l_dbus_message_new_method_return(message);
  }

  l_dbus_message* BluezProfile::newConnectionCallback(struct l_dbus*, struct l_dbus_message* message, void* userData) {
    auto* profile = static_cast<BluezProfile*>(userData);
    const char* devicePath = nullptr;
    int fd = -1;
    struct l_dbus_message_iter props;

    if (!l_dbus_message_get_arguments(message, "oha{sv}", &devicePath, &fd, &props)) {
      return l_dbus_message_new_error(message, "org.bluez.Error.InvalidArguments", "Invalid NewConnection args");
    }

    if (profile != nullptr && devicePath != nullptr && fd >= 0) {
      profile->newConnection(devicePath, fd);
    }

    return l_dbus_message_new_method_return(message);
  }

  l_dbus_message* BluezProfile::requestDisconnectionCallback(struct l_dbus*, struct l_dbus_message* message, void* userData) {
    auto* profile = static_cast<BluezProfile*>(userData);
    const char* devicePath = nullptr;

    if (!l_dbus_message_get_arguments(message, "o", &devicePath)) {
      return l_dbus_message_new_error(message, "org.bluez.Error.InvalidArguments", "Invalid RequestDisconnection args");
    }

    if (profile != nullptr && devicePath != nullptr) {
      profile->requestDisconnection(devicePath);
    }

    return l_dbus_message_new_method_return(message);
  }

  void BluezProfile::release() {
    OPENAUTO_LOG(info) << "[BluezProfile] Release";
  }

  void BluezProfile::newConnection(const std::string& devicePath, int fd) {
    OPENAUTO_LOG(info) << "[BluezProfile] NewConnection: " << devicePath;
    if (server_ != nullptr) {
      const int dupFd = ::dup(fd);
      if (dupFd < 0) {
        OPENAUTO_LOG(error) << "[BluezProfile] Failed to dup socket fd";
        return;
      }
      server_->onNewConnection(dupFd, devicePath);
    }
  }

  void BluezProfile::requestDisconnection(const std::string& devicePath) {
    OPENAUTO_LOG(info) << "[BluezProfile] RequestDisconnection: " << devicePath;
    if (server_ != nullptr) {
      server_->onDisconnection(devicePath);
    }
  }

}