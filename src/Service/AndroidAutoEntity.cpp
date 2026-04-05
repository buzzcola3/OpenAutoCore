/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*  Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
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

#include <Lite/ControlHandler.hpp>
#include <Messenger/MessageInStreamInterceptor.hpp>
#include <Service/AndroidAutoEntity.hpp>
#include <Common/Log.hpp>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {

        AndroidAutoEntity::AndroidAutoEntity(boost::asio::io_service &ioService,
                                             aasdk::messenger::ICryptor::Pointer cryptor,
                                             aasdk::FrameRouter::Pointer router,
                                             configuration::IConfiguration::Pointer configuration,
                                             configuration::ServiceConfig& serviceConfig,
                                             ServiceList serviceList,
                                             IPinger::Pointer pinger)
            : strand_(ioService), cryptor_(std::move(cryptor)), router_(std::move(router)),
              controlHandler_(aasdk::messenger::interceptor::getControlHandler()),
              configuration_(std::move(configuration)), serviceConfig_(serviceConfig),
              serviceList_(std::move(serviceList)),
              pinger_(std::move(pinger)), eventHandler_(nullptr) {
        }

        AndroidAutoEntity::~AndroidAutoEntity() {
          OPENAUTO_LOG(debug) << "[AndroidAutoEntity] destroy.";
        }

        void AndroidAutoEntity::wireControlCallbacks() {
          auto self = this->shared_from_this();

          controlHandler_.onVersionResponse = [this, self](uint16_t majorCode, uint16_t minorCode,
                                                            aap_protobuf::shared::MessageStatus status) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onVersionResponse() " << majorCode << "." << minorCode
                               << " status=" << status;

            if (status == aap_protobuf::shared::MessageStatus::STATUS_NO_COMPATIBLE_VERSION) {
              OPENAUTO_LOG(error) << "[AndroidAutoEntity] Version mismatch.";
              this->triggerQuit();
              return;
            }

            OPENAUTO_LOG(debug) << "[AndroidAutoEntity] Version matches.";
            try {
              OPENAUTO_LOG(info) << "[AndroidAutoEntity] Beginning SSL handshake.";
              cryptor_->doHandshake();
              controlHandler_.sendHandshake(cryptor_->readHandshakeBuffer());
            } catch (const aasdk::error::Error &e) {
              OPENAUTO_LOG(error) << "[AndroidAutoEntity] Handshake Error.";
              this->triggerQuit();
            }
          };

          controlHandler_.onHandshake = [this, self](const aasdk::common::DataConstBuffer &payload) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onHandshake() size=" << payload.size;
            try {
              cryptor_->writeHandshakeBuffer(payload);
              if (!cryptor_->doHandshake()) {
                OPENAUTO_LOG(info) << "[AndroidAutoEntity] Re-attempting handshake.";
                controlHandler_.sendHandshake(cryptor_->readHandshakeBuffer());
              } else {
                OPENAUTO_LOG(info) << "[AndroidAutoEntity] Handshake completed.";
                aap_protobuf::service::control::message::AuthResponse auth;
                auth.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
                controlHandler_.sendAuthComplete(auth);
              }
            } catch (const aasdk::error::Error &e) {
              OPENAUTO_LOG(error) << "[AndroidAutoEntity] Error during handshake";
              this->triggerQuit();
            }
          };

          controlHandler_.onServiceDiscoveryRequest = [this, self](
              const aap_protobuf::service::control::message::ServiceDiscoveryRequest &request) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onServiceDiscoveryRequest()";
            OPENAUTO_LOG(debug) << "[AndroidAutoEntity] Type: " << request.label_text()
                                << ", Model: " << request.device_name();
            controlHandler_.sendServiceDiscoveryResponse(serviceConfig_.toProto());
          };

          controlHandler_.onAudioFocusRequest = [this, self](
              const aap_protobuf::service::control::message::AudioFocusRequest &request) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onAudioFocusRequest()";
            OPENAUTO_LOG(debug) << "[AndroidAutoEntity] AudioFocusRequestType: "
                               << AudioFocusRequestType_Name(request.audio_focus_type());

            aap_protobuf::service::control::message::AudioFocusStateType focusState =
                request.audio_focus_type() ==
                aap_protobuf::service::control::message::AudioFocusRequestType::AUDIO_FOCUS_RELEASE
                ? aap_protobuf::service::control::message::AudioFocusStateType::AUDIO_FOCUS_STATE_LOSS
                : aap_protobuf::service::control::message::AudioFocusStateType::AUDIO_FOCUS_STATE_GAIN;

            aap_protobuf::service::control::message::AudioFocusNotification response;
            response.set_focus_state(focusState);
            controlHandler_.sendAudioFocusResponse(response);
          };

          controlHandler_.onNavigationFocusRequest = [this, self](
              const aap_protobuf::service::control::message::NavFocusRequestNotification &request) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onNavigationFocusRequest()";
            OPENAUTO_LOG(debug) << "[AndroidAutoEntity] NavFocusType: " << NavFocusType_Name(request.focus_type());

            aap_protobuf::service::control::message::NavFocusNotification response;
            response.set_focus_type(
                aap_protobuf::service::control::message::NavFocusType::NAV_FOCUS_PROJECTED);
            controlHandler_.sendNavigationFocusResponse(response);
          };

          controlHandler_.onByeByeRequest = [this, self](
              const aap_protobuf::service::control::message::ByeByeRequest &request) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onByeByeRequest() reason=" << request.reason();
            aap_protobuf::service::control::message::ByeByeResponse response;
            controlHandler_.sendShutdownResponse(response);
            this->triggerQuit();
          };

          controlHandler_.onByeByeResponse = [this, self](
              const aap_protobuf::service::control::message::ByeByeResponse &) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onByeByeResponse()";
            this->triggerQuit();
          };

          controlHandler_.onBatteryStatusNotification = [](
              const aap_protobuf::service::control::message::BatteryStatusNotification &) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onBatteryStatusNotification()";
          };

          controlHandler_.onVoiceSessionRequest = [](
              const aap_protobuf::service::control::message::VoiceSessionNotification &) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onVoiceSessionRequest()";
          };

          controlHandler_.onPingRequest = [](
              const aap_protobuf::service::control::message::PingRequest &) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onPingRequest()";
          };

          controlHandler_.onPingResponse = [this, self](
              const aap_protobuf::service::control::message::PingResponse &response) {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] onPingResponse() ts=" << response.timestamp();
            pinger_->pong();
          };

          pinger_->onPingReady = [this, self]() { sendPing(); };
          pinger_->onPingTimeout = [this, self]() {
            OPENAUTO_LOG(error) << "[AndroidAutoEntity] Ping timer exceeded.";
            triggerQuit();
          };
        }

        void AndroidAutoEntity::start(IAndroidAutoEntityEventHandler &eventHandler) {
          strand_.dispatch([this, self = this->shared_from_this(), eventHandler = &eventHandler]() {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] start()";

            eventHandler_ = eventHandler;
            std::for_each(serviceList_.begin(), serviceList_.end(), std::bind(&IService::start, std::placeholders::_1));

            this->wireControlCallbacks();

            OPENAUTO_LOG(debug) << "[AndroidAutoEntity] Starting FrameRouter.";
            router_->start();

            OPENAUTO_LOG(debug) << "[AndroidAutoEntity] Send Version Request.";
            controlHandler_.sendVersionRequest();

            pinger_->start();
          });
        }

        void AndroidAutoEntity::stop() {
          strand_.dispatch([this, self = this->shared_from_this()]() {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] stop()";

            try {
              eventHandler_ = nullptr;

              // Clear all callbacks to break shared_ptr reference cycles
              controlHandler_.onVersionResponse = nullptr;
              controlHandler_.onHandshake = nullptr;
              controlHandler_.onServiceDiscoveryRequest = nullptr;
              controlHandler_.onAudioFocusRequest = nullptr;
              controlHandler_.onNavigationFocusRequest = nullptr;
              controlHandler_.onByeByeRequest = nullptr;
              controlHandler_.onByeByeResponse = nullptr;
              controlHandler_.onBatteryStatusNotification = nullptr;
              controlHandler_.onVoiceSessionRequest = nullptr;
              controlHandler_.onPingRequest = nullptr;
              controlHandler_.onPingResponse = nullptr;
              controlHandler_.onChannelOpenRequest = nullptr;

              pinger_->onPingReady = nullptr;
              pinger_->onPingTimeout = nullptr;
              pinger_->cancel();

              std::for_each(serviceList_.begin(), serviceList_.end(),
                            std::bind(&IService::stop, std::placeholders::_1));

              router_->stop();
              cryptor_->deinit();
            } catch (...) {
              OPENAUTO_LOG(error) << "[AndroidAutoEntity] stop() - exception when stopping.";
            }
          });
        }

        void AndroidAutoEntity::pause() {
          strand_.dispatch([this, self = this->shared_from_this()]() {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] pause()";

            try {
              std::for_each(serviceList_.begin(), serviceList_.end(),
                            std::bind(&IService::pause, std::placeholders::_1));
            } catch (...) {
              OPENAUTO_LOG(error) << "[AndroidAutoEntity] pause() - exception when pausing.";
            }
          });
        }

        void AndroidAutoEntity::resume() {
          strand_.dispatch([this, self = this->shared_from_this()]() {
            OPENAUTO_LOG(info) << "[AndroidAutoEntity] resume()";

            try {
              std::for_each(serviceList_.begin(), serviceList_.end(),
                            std::bind(&IService::resume, std::placeholders::_1));
            } catch (...) {
              OPENAUTO_LOG(error) << "[AndroidAutoEntity] resume() exception when resuming.";
            }
          });
        }

        void AndroidAutoEntity::triggerQuit() {
          OPENAUTO_LOG(info) << "[AndroidAutoEntity] triggerQuit()";
          if (eventHandler_ != nullptr) {
            eventHandler_->onAndroidAutoQuit();
          }
        }

        void AndroidAutoEntity::sendPing() {
          OPENAUTO_LOG(debug) << "[AndroidAutoEntity] sendPing()";

          aap_protobuf::service::control::message::PingRequest request;
          auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::high_resolution_clock::now().time_since_epoch());
          request.set_timestamp(timestamp.count());
          controlHandler_.sendPingRequest(request);
        }
      }
    }
  }
}
