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
#include <unistd.h>

namespace f1x::openauto::btservice {

  BluezProfile::BluezProfile(sdbus::IConnection& connection, std::string objectPath, BluezBluetoothServer* server)
      : objectPath_(std::move(objectPath)),
        server_(server),
        object_(sdbus::createObject(connection, objectPath_)) {
    object_->registerMethod("Release")
        .onInterface("org.bluez.Profile1")
        .implementedAs([this]() { release(); });

    object_->registerMethod("NewConnection")
        .onInterface("org.bluez.Profile1")
        .implementedAs([this](const sdbus::ObjectPath& device,
                              const sdbus::UnixFd& fd,
                              const std::map<std::string, sdbus::Variant>& properties) {
          newConnection(device, fd, properties);
        });

    object_->registerMethod("RequestDisconnection")
        .onInterface("org.bluez.Profile1")
        .implementedAs([this](const sdbus::ObjectPath& device) { requestDisconnection(device); });

    object_->finishRegistration();
  }

  BluezProfile::~BluezProfile() = default;

  const std::string& BluezProfile::objectPath() const {
    return objectPath_;
  }

  void BluezProfile::release() {
    OPENAUTO_LOG(info) << "[BluezProfile] Release";
  }

  void BluezProfile::newConnection(const sdbus::ObjectPath& device,
                                   const sdbus::UnixFd& fd,
                                   const std::map<std::string, sdbus::Variant>&) {
    const std::string devicePath = device;
    OPENAUTO_LOG(info) << "[BluezProfile] NewConnection: " << devicePath;
    if (server_ != nullptr) {
      const int dupFd = ::dup(fd.get());
      if (dupFd < 0) {
        OPENAUTO_LOG(error) << "[BluezProfile] Failed to dup socket fd";
        return;
      }
      server_->onNewConnection(dupFd, devicePath);
    }
  }

  void BluezProfile::requestDisconnection(const sdbus::ObjectPath& device) {
    const std::string devicePath = device;
    OPENAUTO_LOG(info) << "[BluezProfile] RequestDisconnection: " << devicePath;
    if (server_ != nullptr) {
      server_->onDisconnection(devicePath);
    }
  }

}