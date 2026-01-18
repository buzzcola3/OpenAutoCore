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

#include <btservice/BluezBluetoothServer.hpp>
#include <btservice/BluezProfile.hpp>
#include <Common/Log.hpp>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/unknown_field_set.h>
#include <aap_protobuf/aaw/MessageId.pb.h>
#include <aap_protobuf/aaw/Status.pb.h>
#include <aap_protobuf/aaw/WifiConnectionStatus.pb.h>
#include <aap_protobuf/aaw/WifiInfoResponse.pb.h>
#include <aap_protobuf/aaw/WifiVersionRequest.pb.h>
#include <aap_protobuf/aaw/WifiVersionResponse.pb.h>
#include <aap_protobuf/aaw/WifiStartResponse.pb.h>
#include <aap_protobuf/aaw/WifiStartRequest.pb.h>
#include <QString>
#include <algorithm>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>
#include <errno.h>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <fstream>

using namespace google::protobuf;
using namespace google::protobuf::io;

namespace f1x::openauto::btservice {

  using VariantMap = std::map<std::string, sdbus::Variant>;
  using InterfaceMap = std::map<std::string, VariantMap>;
  using ManagedObjects = std::map<sdbus::ObjectPath, InterfaceMap>;

  namespace {
    uint16_t readUint16(const std::vector<uint8_t>& buffer, std::size_t offset) {
      return static_cast<uint16_t>((buffer[offset] << 8) | buffer[offset + 1]);
    }

    void writeUint16(std::vector<uint8_t>& buffer, std::size_t offset, uint16_t value) {
      buffer[offset] = static_cast<uint8_t>((value >> 8) & 0xFF);
      buffer[offset + 1] = static_cast<uint8_t>(value & 0xFF);
    }

    std::string toLower(const std::string& input) {
      std::string out = input;
      std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return out;
    }
  }

  BluezBluetoothServer::BluezBluetoothServer(autoapp::configuration::IConfiguration::Pointer configuration)
      : configuration_(std::move(configuration)),
        bus_(sdbus::createSystemBusConnection()),
        profilePath_("/f1x/openauto/bluez_profile") {
    OPENAUTO_LOG(info) << "[BluezBluetoothServer] Initialising";
  }

  BluezBluetoothServer::~BluezBluetoothServer() {
    stopReadLoop(false);
    if (bus_) {
      bus_->leaveEventLoop();
    }
  }

  uint16_t BluezBluetoothServer::start(const std::string& address) {
    OPENAUTO_LOG(info) << "[BluezBluetoothServer::start]";

    if (!bus_) {
      OPENAUTO_LOG(error) << "[BluezBluetoothServer] System bus not available.";
      return 0;
    }

    objectManager_ = sdbus::createProxy(*bus_, "org.bluez", "/");
    profileManager_ = sdbus::createProxy(*bus_, "org.bluez", "/org/bluez");

    adapterPath_ = resolveAdapterPath(address);
    if (adapterPath_.empty()) {
      OPENAUTO_LOG(error) << "[BluezBluetoothServer] No BlueZ adapter found.";
      return 0;
    }

    setAdapterProperty(adapterPath_, "Powered", sdbus::Variant(true));
    setAdapterProperty(adapterPath_, "Discoverable", sdbus::Variant(true));
    setAdapterProperty(adapterPath_, "Pairable", sdbus::Variant(true));
    setAdapterProperty(adapterPath_, "DiscoverableTimeout", sdbus::Variant(uint32_t{0}));
    setAdapterProperty(adapterPath_, "PairableTimeout", sdbus::Variant(uint32_t{0}));

    profile_ = std::make_unique<BluezProfile>(*bus_, profilePath_, this);

    const std::string serviceUuid = "4de17a00-52cb-11e6-bdf4-0800200c9a66";

    std::map<std::string, sdbus::Variant> options;
    options["Name"] = sdbus::Variant("OpenAuto Bluetooth Service");
    options["Role"] = sdbus::Variant("server");
    options["Channel"] = sdbus::Variant(channel_);
    options["Service"] = sdbus::Variant(serviceUuid);
    options["RequireAuthentication"] = sdbus::Variant(false);
    options["RequireAuthorization"] = sdbus::Variant(false);
    options["AutoConnect"] = sdbus::Variant(true);

    profileManager_->callMethod("RegisterProfile")
        .onInterface("org.bluez.ProfileManager1")
        .withArguments(sdbus::ObjectPath(profilePath_), serviceUuid, options);

    bus_->enterEventLoopAsync();

    OPENAUTO_LOG(info) << "[BluezBluetoothServer] Profile registered on channel " << channel_;
    return channel_;
  }

  void BluezBluetoothServer::onNewConnection(int fd, const std::string& devicePath) {
    OPENAUTO_LOG(info) << "[BluezBluetoothServer] New connection from " << devicePath;

    stopReadLoop(false);
    socketFd_ = fd;
    startReadLoop();

    aap_protobuf::aaw::WifiVersionRequest versionRequest;
    aap_protobuf::aaw::WifiStartRequest startRequest;

    const auto hostIp = getIP4_("wlan0");
    if (hostIp.empty()) {
      OPENAUTO_LOG(warning) << "[BluezBluetoothServer] No IPv4 found on wlan0, using first non-loopback interface.";
    }
    startRequest.set_ip_address(hostIp);
    startRequest.set_port(5000);

    sendMessage(versionRequest, aap_protobuf::aaw::MessageId::WIFI_VERSION_REQUEST);
    sendMessage(startRequest, aap_protobuf::aaw::MessageId::WIFI_START_REQUEST);
  }

  void BluezBluetoothServer::onDisconnection(const std::string& devicePath) {
    OPENAUTO_LOG(info) << "[BluezBluetoothServer] Disconnected " << devicePath;
    stopReadLoop(false);
  }

  std::string BluezBluetoothServer::resolveAdapterPath(const std::string& address) const {
    if (!objectManager_) {
      return "/org/bluez/hci0";
    }

    ManagedObjects managed;
    objectManager_->callMethod("GetManagedObjects")
        .onInterface("org.freedesktop.DBus.ObjectManager")
        .storeResultsTo(managed);

    for (const auto& entry : managed) {
      const auto& path = entry.first;
      const auto& interfaces = entry.second;
      auto adapterIt = interfaces.find("org.bluez.Adapter1");
      if (adapterIt == interfaces.end()) {
        continue;
      }

      if (address.empty()) {
        return path;
      }

      const auto& props = adapterIt->second;
      auto addrIt = props.find("Address");
      if (addrIt == props.end()) {
        continue;
      }

      const auto adapterAddr = addrIt->second.get<std::string>();
      if (toLower(adapterAddr) == toLower(address)) {
        return path;
      }
    }

    return "/org/bluez/hci0";
  }

  bool BluezBluetoothServer::setAdapterProperty(const std::string& adapterPath,
                                                const std::string& name,
                                                const sdbus::Variant& value) const {
    try {
      auto adapterProxy = sdbus::createProxy(*bus_, "org.bluez", adapterPath);
      adapterProxy->callMethod("Set")
          .onInterface("org.freedesktop.DBus.Properties")
          .withArguments("org.bluez.Adapter1", name, value);
      return true;
    } catch (const sdbus::Error& e) {
      OPENAUTO_LOG(warning) << "[BluezBluetoothServer] Failed setting " << name
                            << ": " << e.getMessage();
      return false;
    }
  }

  void BluezBluetoothServer::startReadLoop() {
    if (socketFd_ < 0) {
      return;
    }

    reading_.store(true);
    readerThread_ = std::thread(&BluezBluetoothServer::readLoop, this);
  }

  void BluezBluetoothServer::stopReadLoop(bool fromReader) {
    reading_.store(false);
    if (socketFd_ >= 0) {
      OPENAUTO_LOG(info) << "[BluezBluetoothServer] Closing RFCOMM socket fd " << socketFd_;
      ::shutdown(socketFd_, SHUT_RDWR);
      ::close(socketFd_);
      socketFd_ = -1;
    }
    if (!fromReader && readerThread_.joinable()) {
      readerThread_.join();
    }
    buffer_.clear();
  }

  void BluezBluetoothServer::readLoop() {
    while (reading_.load()) {
      uint8_t temp[4096];
      const auto bytes = ::read(socketFd_, temp, sizeof(temp));
      if (bytes < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        OPENAUTO_LOG(warning) << "[BluezBluetoothServer] Socket read failed: " << strerror(errno);
        break;
      }
      if (bytes == 0) {
        OPENAUTO_LOG(warning) << "[BluezBluetoothServer] Socket closed by peer.";
        break;
      }

      buffer_.insert(buffer_.end(), temp, temp + bytes);

      while (buffer_.size() >= 4) {
        const uint16_t length = readUint16(buffer_, 0);
        if (buffer_.size() < static_cast<std::size_t>(length + 4)) {
          break;
        }

        const uint16_t rawMessageId = readUint16(buffer_, 2);
        const auto messageId = static_cast<aap_protobuf::aaw::MessageId>(rawMessageId);
        const uint8_t* payload = buffer_.data() + 4;

        switch (messageId) {
          case aap_protobuf::aaw::MessageId::WIFI_INFO_REQUEST:
            handleWifiInfoRequest();
            break;
          case aap_protobuf::aaw::MessageId::WIFI_VERSION_RESPONSE:
            handleWifiVersionResponse(payload, length);
            break;
          case aap_protobuf::aaw::MessageId::WIFI_CONNECTION_STATUS:
            handleWifiConnectionStatus(payload, length);
            break;
          case aap_protobuf::aaw::MessageId::WIFI_START_RESPONSE:
            handleWifiStartResponse(payload, length);
            break;
          case aap_protobuf::aaw::MessageId::WIFI_START_REQUEST:
          case aap_protobuf::aaw::MessageId::WIFI_INFO_RESPONSE:
          case aap_protobuf::aaw::MessageId::WIFI_VERSION_REQUEST:
          default: {
            const auto dataLen = length >= 2 ? length - 2 : 0;
            std::string protoData(reinterpret_cast<const char*>(payload), dataLen);
            DecodeProtoMessage(protoData);

            std::stringstream ss;
            ss << std::hex << std::setfill('0');
            for (auto val : buffer_) {
              ss << std::setw(2) << static_cast<unsigned>(val);
            }
            OPENAUTO_LOG(debug) << "[BluezBluetoothServer::readLoop] Unknown message: " << messageId;
            OPENAUTO_LOG(debug) << "[BluezBluetoothServer::readLoop] Data " << ss.str();
            break;
          }
        }

        buffer_.erase(buffer_.begin(), buffer_.begin() + length + 4);
      }
    }
    OPENAUTO_LOG(info) << "[BluezBluetoothServer] Read loop exiting";
    stopReadLoop(true);
  }

  void BluezBluetoothServer::handleWifiInfoRequest() {
    OPENAUTO_LOG(info) << "[BluezBluetoothServer::handleWifiInfoRequest] Handling wifi info request";

    aap_protobuf::aaw::WifiInfoResponse response;

    auto ssid = configuration_->getParamFromFile("/etc/hostapd/hostapd.conf", "ssid");
    auto pass = configuration_->getParamFromFile("/etc/hostapd/hostapd.conf", "wpa_passphrase");

    if (ssid.isEmpty()) {
      ssid = configuration_->getParamFromFile("wifi_credentials.ini", "ssid");
    }
    if (pass.isEmpty()) {
      pass = configuration_->getParamFromFile("wifi_credentials.ini", "wpa_passphrase");
    }

    if (ssid.isEmpty()) {
      ssid = QString::fromUtf8("OpenAutoAP");
    }
    if (pass.isEmpty()) {
      pass = QString::fromUtf8("OpenAutoPass123");
    }

    response.set_ssid(ssid.toStdString());
    response.set_password(pass.toStdString());

    auto bssid = getMacAddress("wlp4s0");
    if (bssid.empty()) {
      bssid = "00:00:00:00:00:00";
    }
    response.set_bssid(bssid);
    response.set_security_mode(
        aap_protobuf::service::wifiprojection::message::WifiSecurityMode::WPA2_ENTERPRISE);
    response.set_access_point_type(aap_protobuf::service::wifiprojection::message::AccessPointType::STATIC);

    sendMessage(response, 3);
  }

  void BluezBluetoothServer::handleWifiVersionResponse(const uint8_t* payload, std::size_t length) {
    OPENAUTO_LOG(info) << "[BluezBluetoothServer::handleWifiVersionResponse] Handling wifi version response";

    aap_protobuf::aaw::WifiVersionResponse response;
    response.ParseFromArray(payload, static_cast<int>(length));
    OPENAUTO_LOG(debug) << "[BluezBluetoothServer::handleWifiVersionResponse] Unknown Param 1: "
                        << response.unknown_value_a() << " Unknown Param 2: " << response.unknown_value_b();
  }

  void BluezBluetoothServer::handleWifiStartResponse(const uint8_t* payload, std::size_t length) {
    OPENAUTO_LOG(info) << "[BluezBluetoothServer::handleWifiStartResponse] Handling wifi start response";

    aap_protobuf::aaw::WifiStartResponse response;
    response.ParseFromArray(payload, static_cast<int>(length));
    OPENAUTO_LOG(debug) << "[BluezBluetoothServer::handleWifiStartResponse] " << response.ip_address()
                        << " port " << response.port() << " status " << Status_Name(response.status());
  }

  void BluezBluetoothServer::handleWifiConnectionStatus(const uint8_t* payload, std::size_t length) {
    aap_protobuf::aaw::WifiConnectionStatus status;
    status.ParseFromArray(payload, static_cast<int>(length));
    OPENAUTO_LOG(info) << "[BluezBluetoothServer::handleWifiConnectionStatus] Handle wifi connection status, received: "
                       << Status_Name(status.status());
  }

  void BluezBluetoothServer::sendMessage(const google::protobuf::Message &message, uint16_t type) {
    if (socketFd_ < 0) {
      return;
    }

    const auto byteSize = static_cast<uint16_t>(message.ByteSizeLong());
    std::vector<uint8_t> out(byteSize + 4, 0);
    writeUint16(out, 0, byteSize);
    writeUint16(out, 2, type);
    message.SerializeToArray(reinterpret_cast<void*>(out.data() + 4), byteSize);

    auto written = ::write(socketFd_, out.data(), out.size());
    if (written > -1) {
      OPENAUTO_LOG(debug) << "[BluezBluetoothServer::sendMessage] Bytes written: " << written;
    } else {
      OPENAUTO_LOG(debug) << "[BluezBluetoothServer::sendMessage] Could not write data";
    }
  }

  std::string BluezBluetoothServer::getIP4_(const std::string& intf) const {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
      return "";
    }

    std::string fallback;
    for (auto ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
        continue;
      }

      if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) {
        continue;
      }

      char host[NI_MAXHOST];
      const auto* addr = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
      if (inet_ntop(AF_INET, &addr->sin_addr, host, sizeof(host)) == nullptr) {
        continue;
      }

      if (!intf.empty() && intf == ifa->ifa_name) {
        freeifaddrs(ifaddr);
        return host;
      }

      if (fallback.empty()) {
        fallback = host;
      }
    }

    freeifaddrs(ifaddr);
    return fallback;
  }

  std::string BluezBluetoothServer::getMacAddress(const std::string& intf) const {
    if (intf.empty()) {
      return "";
    }

    std::ifstream file("/sys/class/net/" + intf + "/address");
    if (!file.is_open()) {
      return "";
    }

    std::string mac;
    std::getline(file, mac);
    return mac;
  }

  void BluezBluetoothServer::DecodeProtoMessage(const std::string& proto_data) {
    UnknownFieldSet set;

    ArrayInputStream raw_input(proto_data.data(), proto_data.size());
    CodedInputStream input(&raw_input);

    if (!set.MergeFromCodedStream(&input)) {
      std::cerr << "Failed to decode the message." << std::endl;
      return;
    }

    for (int i = 0; i < set.field_count(); ++i) {
      const UnknownField& field = set.field(i);
      switch (field.type()) {
        case UnknownField::TYPE_VARINT:
          std::cout << "Field number " << field.number() << " is a varint: " << field.varint() << std::endl;
          break;
        case UnknownField::TYPE_FIXED32:
          std::cout << "Field number " << field.number() << " is a fixed32: " << field.fixed32() << std::endl;
          break;
        case UnknownField::TYPE_FIXED64:
          std::cout << "Field number " << field.number() << " is a fixed64: " << field.fixed64() << std::endl;
          break;
        case UnknownField::TYPE_LENGTH_DELIMITED:
          std::cout << "Field number " << field.number() << " is length-delimited: ";
          for (char ch : field.length_delimited()) {
            std::cout << std::hex << (int)(unsigned char)ch;
          }
          std::cout << std::dec << std::endl;
          break;
        case UnknownField::TYPE_GROUP:
          std::cout << "Field number " << field.number() << " is a group." << std::endl;
          break;
      }
    }
  }

}
