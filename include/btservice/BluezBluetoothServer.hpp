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
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <sdbus-c++/sdbus-c++.h>

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
    std::string resolveAdapterPath(const std::string& address) const;
    bool setAdapterProperty(const std::string& adapterPath, const std::string& name, const sdbus::Variant& value) const;
    void startReadLoop();
    void stopReadLoop(bool fromReader);
    void readLoop();
    void handleWifiInfoRequest();
    void handleWifiVersionResponse(const uint8_t* payload, std::size_t length);
    void handleWifiConnectionStatus(const uint8_t* payload, std::size_t length);
    void handleWifiStartResponse(const uint8_t* payload, std::size_t length);
    void sendMessage(const google::protobuf::Message &message, uint16_t type);
    std::string getIP4_(const std::string& intf) const;
    std::string getMacAddress(const std::string& intf) const;
    void DecodeProtoMessage(const std::string& proto_data);

    autoapp::configuration::IConfiguration::Pointer configuration_;
    std::unique_ptr<sdbus::IConnection> bus_;
    std::unique_ptr<sdbus::IProxy> objectManager_;
    std::unique_ptr<sdbus::IProxy> profileManager_;
    std::string adapterPath_;
    std::string profilePath_;
    std::unique_ptr<BluezProfile> profile_;
    int socketFd_{-1};
    std::atomic_bool reading_{false};
    std::thread readerThread_;
    std::vector<uint8_t> buffer_;
    uint16_t channel_{12};
  };

}
