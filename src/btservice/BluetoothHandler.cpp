//
// Created by Simon Dean on 26/11/2024.
//

#include <btservice/BluetoothHandler.hpp>
#include <Common/Log.hpp>

namespace f1x::openauto::btservice {
  BluetoothHandler::BluetoothHandler(btservice::IAndroidBluetoothServer::Pointer androidBluetoothServer,
                                     autoapp::configuration::IConfiguration::Pointer configuration)
  : configuration_(std::move(configuration)),
    androidBluetoothServer_(std::move(androidBluetoothServer)) {

    OPENAUTO_LOG(info) << "[BluetoothHandler::BluetoothHandler] Starting Up...";

    const auto adapterAddress = configuration_->getBluetoothAdapterAddress();

    uint16_t portNumber = androidBluetoothServer_->start(adapterAddress);

    if (portNumber == 0) {
      OPENAUTO_LOG(error) << "[BluetoothHandler::BluetoothHandler] Server start failed.";
      throw std::runtime_error("Unable to start bluetooth server");
    }

    OPENAUTO_LOG(info) << "[BluetoothHandler::BluetoothHandler] Listening for connections, address: " << adapterAddress
               << ", port: " << portNumber;

    // TODO: Connect to any previously paired devices
  }

  void BluetoothHandler::shutdownService() {
    OPENAUTO_LOG(info) << "[BluetoothHandler::shutdownService] Shutdown initiated";
  }
}