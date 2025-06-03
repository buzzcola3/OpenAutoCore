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

#include <Channel/MediaSource/IMediaSourceService.hpp>
#include <Channel/MediaSource/MediaSourceService.hpp>
#include <Channel/MediaSource/IMediaSourceServiceEventHandler.hpp>
#include <f1x/openauto/autoapp/Service/IService.hpp>
#include <f1x/openauto/autoapp/Projection/IAudioInput.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace service
{
  namespace mediasource {

    using IoContext = boost::asio::io_context;
    using Strand = boost::asio::strand<IoContext::executor_type>;


    class MediaSourceService
        : public aasdk::channel::mediasource::IMediaSourceServiceEventHandler,
          public IService,
          public std::enable_shared_from_this<MediaSourceService> {
    public:
      typedef std::shared_ptr<MediaSourceService> Pointer;

      // General Constructor
      MediaSourceService(IoContext& ioContext,
      aasdk::channel::mediasource::IMediaSourceService::Pointer channel, projection::IAudioInput::Pointer audioInput);

      void start() override;

      void stop() override;

      void pause() override;

      void resume() override;

      void fillFeatures(
          aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) override;

      void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) override;

      void onMediaChannelSetupRequest(
          const aap_protobuf::service::media::shared::message::Setup &request) override;

      void onMediaSourceOpenRequest(
          const aap_protobuf::service::media::source::message::MicrophoneRequest &request) override;

      void onMediaChannelAckIndication(
          const aap_protobuf::service::media::source::message::Ack &indication) override;

      void onChannelError(const aasdk::error::Error &e) override;

    protected:
      using std::enable_shared_from_this<MediaSourceService>::shared_from_this;

      void onMediaSourceOpenSuccess();

      void onMediaSourceDataReady(aasdk::common::Data data);

      void readMediaSource();

      Strand strand_;
      aasdk::channel::mediasource::IMediaSourceService::Pointer channel_;
      projection::IAudioInput::Pointer audioInput_;
      int32_t session_;
    };
  }
}
}
}
}
