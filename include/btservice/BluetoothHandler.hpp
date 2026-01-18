//
// Created by Simon Dean on 26/11/2024.
//

#ifndef OPENAUTO_BLUETOOTHHANDLER_HPP
#define OPENAUTO_BLUETOOTHHANDLER_HPP

#include <btservice/IBluetoothHandler.hpp>
#include <btservice/IAndroidBluetoothServer.hpp>
#include <Configuration/IConfiguration.hpp>
#include <QObject>

namespace f1x::openauto::btservice {

  class BluetoothHandler : public QObject, public IBluetoothHandler {
    Q_OBJECT
  public:
    BluetoothHandler(btservice::IAndroidBluetoothServer::Pointer androidBluetoothServer,
                     autoapp::configuration::IConfiguration::Pointer configuration);

    void shutdownService() override;

  private:
    autoapp::configuration::IConfiguration::Pointer configuration_;
    btservice::IAndroidBluetoothServer::Pointer androidBluetoothServer_;
  };
}


#endif //OPENAUTO_BLUETOOTHHANDLER_HPP
