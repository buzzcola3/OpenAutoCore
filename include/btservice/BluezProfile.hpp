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

#pragma once

#include <map>
#include <memory>
#include <string>
#include <ell/dbus.h>

namespace f1x::openauto::btservice {

  class BluezBluetoothServer;

  class BluezProfile {
  public:
    BluezProfile(l_dbus* bus, std::string objectPath, BluezBluetoothServer* server);
    ~BluezProfile();

    const std::string& objectPath() const;

  private:
    static void setupInterface(struct l_dbus_interface* interface);
    static l_dbus_message* releaseCallback(struct l_dbus* dbus, struct l_dbus_message* message, void* userData);
    static l_dbus_message* newConnectionCallback(struct l_dbus* dbus, struct l_dbus_message* message, void* userData);
    static l_dbus_message* requestDisconnectionCallback(struct l_dbus* dbus, struct l_dbus_message* message, void* userData);

    void release();
    void newConnection(const std::string& devicePath, int fd);
    void requestDisconnection(const std::string& devicePath);

    std::string objectPath_;
    BluezBluetoothServer* server_;
    l_dbus* bus_{nullptr};
  };

}