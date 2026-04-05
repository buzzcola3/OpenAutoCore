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

#include <thread>
#include <USB/AOAPDevice.hpp>
#include <TCP/TCPEndpoint.hpp>
#include <App.hpp>
#include <Common/Log.hpp>

namespace f1x::openauto::autoapp {

  App::App(boost::asio::io_service &ioService, aasdk::usb::USBWrapper &usbWrapper, aasdk::tcp::ITCPWrapper &tcpWrapper,
           service::IAndroidAutoEntityFactory &androidAutoEntityFactory)
      : ioService_(ioService), usbWrapper_(usbWrapper), tcpWrapper_(tcpWrapper), strand_(ioService_),
        androidAutoEntityFactory_(androidAutoEntityFactory), isStopped_(false) {
  }

  void App::startUSBDevice(aasdk::usb::DeviceHandle deviceHandle) {
    strand_.dispatch([this, self = this->shared_from_this(), deviceHandle = std::move(deviceHandle)]() mutable {
      aoapDeviceHandler(std::move(deviceHandle));
    });
  }

  void App::start(aasdk::tcp::ITCPEndpoint::SocketPointer socket) {
    strand_.dispatch([this, self = this->shared_from_this(), socket = std::move(socket)]() mutable {
      OPENAUTO_LOG(info) << "Start from socket";
      if (androidAutoEntity_ != nullptr) {
        try {
          androidAutoEntity_->stop();
        } catch (...) {
          OPENAUTO_LOG(error) << "[App] start: exception caused by androidAutoEntity_->stop();";
        }
        try {
          androidAutoEntity_.reset();
        } catch (...) {
          OPENAUTO_LOG(error) << "[App] start: exception caused by androidAutoEntity_.reset();";
        }
      }

      try {
        auto tcpEndpoint(std::make_shared<aasdk::tcp::TCPEndpoint>(tcpWrapper_, std::move(socket)));
        androidAutoEntity_ = androidAutoEntityFactory_.create(std::move(tcpEndpoint));
        androidAutoEntity_->start(*this);
      }
      catch (const aasdk::error::Error &error) {
        OPENAUTO_LOG(error) << "[App] TCP AndroidAutoEntity create error: " << error.what();
      }
    });
  }

  void App::stop() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      isStopped_ = true;

      if (androidAutoEntity_ != nullptr) {
        try {
          androidAutoEntity_->stop();
        } catch (...) {
          OPENAUTO_LOG(error) << "[App] stop: exception caused by androidAutoEntity_->stop();";
        }
        try {
          androidAutoEntity_.reset();
        } catch (...) {
          OPENAUTO_LOG(error) << "[App] stop: exception caused by androidAutoEntity_.reset();";
        }
      }
    });
  }

  void App::aoapDeviceHandler(aasdk::usb::DeviceHandle deviceHandle) {
    OPENAUTO_LOG(info) << "[App] Device connected.";

    if (androidAutoEntity_ != nullptr) {
      OPENAUTO_LOG(warning) << "[App] android auto entity is still running.";
      return;
    }

    try {
      if (!disableAutostartEntity) {
        OPENAUTO_LOG(info) << "[App] Start Android Auto allowed - let's go.";

        auto aoapDevice(aasdk::usb::AOAPDevice::create(usbWrapper_, ioService_, deviceHandle));
        androidAutoEntity_ = androidAutoEntityFactory_.create(std::move(aoapDevice));
        androidAutoEntity_->start(*this);
      } else {
        OPENAUTO_LOG(info) << "[App] Start Android Auto not allowed - skip.";
      }
    }
    catch (const aasdk::error::Error &error) {
      OPENAUTO_LOG(error) << "[App] USB AndroidAutoEntity create error: " << error.what();
      androidAutoEntity_.reset();
    }
  }

  void App::pause() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[App] pause...";
      androidAutoEntity_->pause();
    });
  }

  void App::resume() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      if (androidAutoEntity_ != nullptr) {
        OPENAUTO_LOG(info) << "[App] resume...";
        androidAutoEntity_->resume();
      } else {
        OPENAUTO_LOG(info) << "[App] Ignore resume -> no androidAutoEntity_ ...";
      }
    });
  }

  void App::onAndroidAutoQuit() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[App] onAndroidAutoQuit()";

      if (androidAutoEntity_ != nullptr) {
        try {
          androidAutoEntity_->stop();
        } catch (...) {
          OPENAUTO_LOG(error) << "[App] onAndroidAutoQuit: exception caused by androidAutoEntity_->stop();";
        }
        try {
          androidAutoEntity_.reset();
        } catch (...) {
          OPENAUTO_LOG(error) << "[App] onAndroidAutoQuit: exception caused by androidAutoEntity_.reset();";
        }
      }
    });
  }

}


