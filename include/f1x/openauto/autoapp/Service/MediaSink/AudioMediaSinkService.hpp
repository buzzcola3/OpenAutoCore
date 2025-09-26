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

#include <vector>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

#include <f1x/openauto/autoapp/Service/IService.hpp>
#include <f1x/openauto/autoapp/Projection/IAudioOutput.hpp>
#include <Channel/MediaSink/Audio/IAudioMediaSinkService.hpp>
#include <aasdk/Common/Data.hpp>
#include <aasdk/Messenger/Timestamp.hpp>
#include <aasdk/Error/Error.hpp>

#include "open_auto_transport/wire.hpp"

// Forward-declare transport (global namespace to avoid shadowing).
namespace buzz { namespace autoapp { namespace Transport { class Transport; } } }

namespace f1x {
namespace openauto {
namespace autoapp {
namespace service {
namespace mediasink {

class AudioMediaSinkService
    : public IService
    , public aasdk::channel::mediasink::audio::IAudioMediaSinkServiceEventHandler
    , public std::enable_shared_from_this<AudioMediaSinkService> {
public:
  using IoContext = boost::asio::io_context;
  using Strand = boost::asio::strand<IoContext::executor_type>;

  AudioMediaSinkService(IoContext& ioContext,
                        aasdk::channel::mediasink::audio::IAudioMediaSinkService::Pointer channel,
                        projection::IAudioOutput::Pointer audioOutput);

  void start() override;
  void stop() override;
  void pause() override;
  void resume() override;

  // Control service hooks
  void fillFeatures(aap_protobuf::service::control::message::ServiceDiscoveryResponse& response) override;
  void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest& request) override;

  // Media channel hooks
  void onMediaChannelSetupRequest(const aap_protobuf::service::media::shared::message::Setup& request) override;
  void onMediaChannelStartIndication(const aap_protobuf::service::media::shared::message::Start& indication) override;
  void onMediaChannelStopIndication(const aap_protobuf::service::media::shared::message::Stop& indication) override;
  void onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType timestamp,
                                      const aasdk::common::DataConstBuffer& buffer) override;
  void onMediaIndication(const aasdk::common::DataConstBuffer& buffer) override;
  void onChannelError(const aasdk::error::Error& e) override;

  // Low-overhead transport tap
  void enableTransportTap(std::shared_ptr<::buzz::autoapp::Transport::Transport> transport,
                          ::buzz::wire::MsgType msgType);

protected:
  // Make strand_ accessible to derived services (Media/System/Guidance/Telephony).
  Strand strand_;

  // Also useful for derived classes.
  aasdk::channel::mediasink::audio::IAudioMediaSinkService::Pointer channel_;
  projection::IAudioOutput::Pointer audioOutput_;

private:
  int32_t session_;

  // Tap members
  std::shared_ptr<::buzz::autoapp::Transport::Transport> transportTap_{};
  ::buzz::wire::MsgType transportMsgType_{};
  std::vector<uint8_t> txBuf_;
};

} // namespace mediasink
} // namespace service
} // namespace autoapp
} // namespace openauto
} // namespace f1x