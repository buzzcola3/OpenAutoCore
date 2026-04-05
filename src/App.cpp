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

#include <App.hpp>
#include <Common/Log.hpp>

namespace f1x::openauto::autoapp {

  App::App(boost::asio::io_service &ioService,
           service::IAndroidAutoEntityFactory &androidAutoEntityFactory)
      : ioService_(ioService), strand_(ioService_),
        androidAutoEntityFactory_(androidAutoEntityFactory), isStopped_(false) {
  }

  void App::start(DeviceConnection::Pointer connection) {
    strand_.dispatch([this, self = this->shared_from_this(), conn = std::move(connection)]() mutable {
      OPENAUTO_LOG(info) << "[App] Device connected.";

      if (androidAutoEntity_ != nullptr) {
        OPENAUTO_LOG(warning) << "[App] android auto entity is still running, stopping it first.";
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

      isStopped_ = false;

      try {
        if (!disableAutostartEntity) {
          OPENAUTO_LOG(info) << "[App] Start Android Auto allowed - let's go.";
          androidAutoEntity_ = androidAutoEntityFactory_.create(std::move(conn));
          androidAutoEntity_->start(*this);
        } else {
          OPENAUTO_LOG(info) << "[App] Start Android Auto not allowed - skip.";
        }
      }
      catch (const aasdk::error::Error &error) {
        OPENAUTO_LOG(error) << "[App] AndroidAutoEntity create error: " << error.what();
        androidAutoEntity_.reset();
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


