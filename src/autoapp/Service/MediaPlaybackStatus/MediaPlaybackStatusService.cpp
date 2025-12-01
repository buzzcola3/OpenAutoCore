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

#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Service/MediaPlaybackStatus/MediaPlaybackStatusService.hpp>
#include <fstream>
#include <string>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {
        namespace mediaplaybackstatus {

          using IoContext = boost::asio::io_context;
          using Strand = boost::asio::strand<IoContext::executor_type>;

          MediaPlaybackStatusService::MediaPlaybackStatusService(IoContext& ioContext,
                                                       aasdk::messenger::IMessenger::Pointer messenger)
              : strand_(ioContext.get_executor()),
                timer_(ioContext),
                channel_(std::make_shared<aasdk::channel::mediaplaybackstatus::MediaPlaybackStatusService>(strand_, std::move(messenger))) {

          }

          void MediaPlaybackStatusService::start() {
            boost::asio::dispatch(strand_, [this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[MediaPlaybackStatusService] start()";
              channel_->receive(self);
            });
          }

          void MediaPlaybackStatusService::stop() {
            boost::asio::dispatch(strand_, [this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[MediaPlaybackStatusService] stop()";
            });
          }

          void MediaPlaybackStatusService::pause() {
            boost::asio::dispatch(strand_, [this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[MediaPlaybackStatusService] pause()";
            });
          }

          void MediaPlaybackStatusService::resume() {
            boost::asio::dispatch(strand_, [this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[MediaPlaybackStatusService] resume()";
            });
          }

          void MediaPlaybackStatusService::onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) {
            OPENAUTO_LOG(info) << "[MediaPlaybackStatusService] onChannelOpenRequest()";
            OPENAUTO_LOG(info) << "[MediaPlaybackStatusService] Channel Id: " << request.service_id() << ", Priority: " << request.priority();

            aap_protobuf::service::control::message::ChannelOpenResponse response;
            const aap_protobuf::shared::MessageStatus status = aap_protobuf::shared::MessageStatus::STATUS_SUCCESS;
            response.set_status(status);

            auto self = this->shared_from_this();
            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then([self]() {
                             self->channel_->receive(self);
                           },
                           std::bind(&MediaPlaybackStatusService::onChannelError, self,
                                     std::placeholders::_1));
            channel_->sendChannelOpenResponse(response, std::move(promise));
          }


          void MediaPlaybackStatusService::onChannelError(const aasdk::error::Error &e) {
            OPENAUTO_LOG(error) << "[MediaPlaybackStatusService] onChannelError(): " << e.what();
          }
        }
      }
    }
  }
}