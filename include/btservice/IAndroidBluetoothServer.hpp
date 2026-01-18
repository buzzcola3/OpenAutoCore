//
// Created by Simon Dean on 26/11/2024.
//

#ifndef OPENAUTO_IANDROIDBLUETOOTHSERVER_HPP
#define OPENAUTO_IANDROIDBLUETOOTHSERVER_HPP

#include <cstdint>
#include <memory>
#include <string>

namespace f1x::openauto::btservice {

  class IAndroidBluetoothServer {
  public:
    using Pointer = std::shared_ptr<IAndroidBluetoothServer>;

    virtual ~IAndroidBluetoothServer() = default;

    virtual uint16_t start(const std::string& address) = 0;
  };

}

#endif //OPENAUTO_IANDROIDBLUETOOTHSERVER_HPP


