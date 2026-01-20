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

#include <btservice/IAndroidBluetoothServer.hpp>
#include <Configuration/IConfiguration.hpp>
#include <atomic>
#include <cstdint>
#include <ell/dbus.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <aap_protobuf/aaw/MessageId.pb.h>

namespace google::protobuf {
  class Message;
}

namespace f1x::openauto::btservice {

  class BluezProfile;

  class BluezBluetoothServer : public IAndroidBluetoothServer {
  public:
    explicit BluezBluetoothServer(autoapp::configuration::IConfiguration::Pointer configuration);
    ~BluezBluetoothServer() override;

    uint16_t start(const std::string& address) override;

    void onNewConnection(int fd, const std::string& devicePath);
    void onDisconnection(const std::string& devicePath);

  private:
    struct InterfaceInfo {
      std::string name;
      std::string ip;
    };

    std::string resolveAdapterPath(const std::string& address) const;
    bool setAdapterProperty(const std::string& adapterPath, const std::string& name,
                            char signature, const void* value) const;
    void startReadLoop();
    void stopReadLoop(bool fromReader);
    void readLoop();
    void sendWifiVersionRequest();
    void sendWifiStartRequest(const InterfaceInfo& wifiInfo);
    void handleWifiInfoRequest();
    void handleWifiVersionResponse(const uint8_t* payload, std::size_t length);
    void handleWifiConnectionStatus(const uint8_t* payload, std::size_t length);
    void handleWifiStartResponse(const uint8_t* payload, std::size_t length);
    void sendMessage(const google::protobuf::Message &message, aap_protobuf::aaw::MessageId type,
             const char* label = nullptr);
    InterfaceInfo getWifiInterfaceInfo() const;
    std::string getMacAddress(const std::string& intf) const;

    struct EllDbusDeleter {
      void operator()(l_dbus* bus) const {
        if (bus != nullptr) {
          l_dbus_destroy(bus);
        }
      }
    };

    autoapp::configuration::IConfiguration::Pointer configuration_;
    std::unique_ptr<l_dbus, EllDbusDeleter> bus_;
    std::string adapterPath_;
    std::string profilePath_;
    std::unique_ptr<BluezProfile> profile_;
    int socketFd_{-1};
    std::atomic_bool reading_{false};
    std::thread readerThread_;
    std::vector<uint8_t> buffer_;
    uint16_t channel_{12};
    std::string wifiInterface_;
  };

}
