#include <Messenger/handlers/MediaSinkVideoMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/ChannelId.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageType.hpp>
#include <Messenger/Timestamp.hpp>
#include <Common/Log.hpp>
#include <open_auto_transport/transport.hpp>
#include <open_auto_transport/wire.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <utility>

#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <aap_protobuf/service/media/shared/message/Config.pb.h>
#include <aap_protobuf/service/media/shared/message/Setup.pb.h>
#include <aap_protobuf/service/media/shared/message/MediaCodecType.pb.h>
#include <aap_protobuf/service/media/shared/message/Start.pb.h>
#include <aap_protobuf/service/media/shared/message/Stop.pb.h>
#include <aap_protobuf/service/media/sink/MediaMessageId.pb.h>
#include <aap_protobuf/service/media/video/message/VideoFocusNotification.pb.h>
#include <aap_protobuf/service/media/video/message/VideoFocusRequestNotification.pb.h>
#include <aap_protobuf/service/media/source/message/Ack.pb.h>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using Media = aap_protobuf::service::media::sink::MediaMessageId;

template<typename Proto>
void decodeAndLogPayload(const std::uint8_t* data, std::size_t size, const char* label) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] " << label
                     << " payload too large for ParseFromArray, bytes=" << size;
    return;
  }

  Proto message;
  if (!message.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] Failed to parse " << label
                     << " payload, bytes=" << size;
    return;
  }

  AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] " << label << ": "
                   << message.ShortDebugString();
}

}

namespace aasdk::messenger::interceptor {

bool MediaSinkVideoMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();
  //AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] media video message received, size="
  //                 << rawPayload.size() << ", total=" << messageCount_;

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] media video payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  bool handled = false;
  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      handled = handleChannelOpenRequest(message, payloadData, payloadSize);
      break;
    case Media::MEDIA_MESSAGE_SETUP:
      handled = handleChannelSetupRequest(message, payloadData, payloadSize);
      break;
    case Media::MEDIA_MESSAGE_START:
      {
        aap_protobuf::service::media::shared::message::Start start;
        if (start.ParseFromArray(payloadData, static_cast<int>(payloadSize))) {
          sessionId_ = start.session_id();
          AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] MediaStart: session=" << sessionId_;
        } else {
          AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] Failed to parse MediaStart payload";
        }
      }
      break;
    case Media::MEDIA_MESSAGE_STOP:
      decodeAndLogPayload<aap_protobuf::service::media::shared::message::Stop>(
          payloadData, payloadSize, "MediaStop");
      break;
    case Media::MEDIA_MESSAGE_VIDEO_FOCUS_REQUEST:
      decodeAndLogPayload<aap_protobuf::service::media::video::message::VideoFocusRequestNotification>(
          payloadData, payloadSize, "VideoFocusRequest");
      break;
    case Media::MEDIA_MESSAGE_CODEC_CONFIG:
      handled = handleCodecConfig(message, payloadData, payloadSize);
      break;
    case Media::MEDIA_MESSAGE_DATA:
      handled = handleMediaData(message, payloadData, payloadSize);
      break;
    default:
      AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] media video message id="
                       << messageId.getId() << " not explicitly decoded.";
      break;
  }

  return handled;
}

bool MediaSinkVideoMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                             const std::uint8_t* data,
                                                             std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!request.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] Failed to parse ChannelOpenRequest payload";
    return false;
  }

  AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] ChannelOpenRequest: "
                   << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] Constructed ChannelOpenResponse: "
                   << response.ShortDebugString();

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::CONTROL,
                          Control::MESSAGE_CHANNEL_OPEN_RESPONSE,
                          response);
    return true;
  } else {
    AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] MessageSender not configured; cannot send response.";
    return false;
  }
}

bool MediaSinkVideoMessageHandlers::handleMediaData(const ::aasdk::messenger::Message& message,
                                                    const std::uint8_t* data,
                                                    std::size_t size) const {
  //AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] media data frame size=" << size
  //                 << " bytes on channel " << channelIdToString(message.getChannelId());

  if (sender_ == nullptr) {
    AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] MessageSender not configured; cannot send media ACK.";
    return false;
  }

  if (sessionId_ < 0) {
    AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] Session id not set; cannot send media ACK.";
    return false;
  }

  const bool hasTimestamp = size >= sizeof(::aasdk::messenger::Timestamp::ValueType);
  constexpr auto timestampBytes = sizeof(::aasdk::messenger::Timestamp::ValueType);
  const std::uint8_t* frameData = data;
  std::size_t frameSize = size;

  uint64_t timestamp = 0;
  if (hasTimestamp) {
    ::aasdk::messenger::Timestamp ts(common::DataConstBuffer(data, timestampBytes));
    //AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] Detected timestamped media frame, ts=" << ts.getValue();
    timestamp = resolveTimestamp(true, ts.getValue());
    frameData += timestampBytes;
    frameSize -= timestampBytes;
  } else {
    AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] Media frame without timestamp.";
    timestamp = resolveTimestamp(false, 0);
  }

  if (ensureTransportStarted()) {
    transport_->send(buzz::wire::MsgType::VIDEO, timestamp, frameData, frameSize);
  }

  aap_protobuf::service::media::source::message::Ack ack;
  ack.set_session_id(sessionId_);
  ack.set_ack(1);

  sender_->sendProtobuf(message.getChannelId(),
                        message.getEncryptionType(),
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        aap_protobuf::service::media::sink::MediaMessageId::MEDIA_MESSAGE_ACK,
                        ack);

  return true; // to see the video data in the app layer, TODO set to true
}

bool MediaSinkVideoMessageHandlers::handleChannelSetupRequest(const ::aasdk::messenger::Message& message,
                                                              const std::uint8_t* data,
                                                              std::size_t size) const {
  aap_protobuf::service::media::shared::message::Setup setup;
  if (!setup.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] Failed to parse MediaSetup payload";
    return false;
  }

  AASDK_LOG(info) << "[MediaSinkVideoMessageHandlers] MediaSetup: channel="
                  << channelIdToString(message.getChannelId())
                  << ", codec=" << aap_protobuf::service::media::shared::message::MediaCodecType_Name(setup.type());

  aap_protobuf::service::media::shared::message::Config response;
  response.set_status(aap_protobuf::service::media::shared::message::Config::STATUS_READY);
  response.set_max_unacked(1);
  response.add_configuration_indices(0);

  if (sender_ != nullptr) {
    sender_->sendProtobuf(
        message.getChannelId(),
        message.getEncryptionType(),
        ::aasdk::messenger::MessageType::SPECIFIC,
        aap_protobuf::service::media::sink::MediaMessageId::MEDIA_MESSAGE_CONFIG,
        response);

    AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] MediaSetup response: "
                     << response.ShortDebugString();

    aap_protobuf::service::media::video::message::VideoFocusNotification focus;
    focus.set_focus(aap_protobuf::service::media::video::message::VideoFocusMode::VIDEO_FOCUS_PROJECTED);
    focus.set_unsolicited(false);

    sender_->sendProtobuf(
        message.getChannelId(),
        message.getEncryptionType(),
        ::aasdk::messenger::MessageType::SPECIFIC,
        aap_protobuf::service::media::sink::MediaMessageId::MEDIA_MESSAGE_VIDEO_FOCUS_NOTIFICATION,
        focus);

    AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] Sent VideoFocusNotification after setup.";
    return true;
  }

  AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] MessageSender not configured; cannot send setup response.";
  return false;
}

bool MediaSinkVideoMessageHandlers::handleCodecConfig(const ::aasdk::messenger::Message& message,
                                                      const std::uint8_t* data,
                                                      std::size_t size) const {
  AASDK_LOG(debug) << "[MediaSinkVideoMessageHandlers] codec configuration blob size=" << size
                   << " bytes on channel " << channelIdToString(message.getChannelId());

  return handleMediaData(message, data, size);;
}

void MediaSinkVideoMessageHandlers::setMessageSender(
    std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

void MediaSinkVideoMessageHandlers::setTransport(
    std::shared_ptr<buzz::autoapp::Transport::Transport> transport) {
  transport_ = std::move(transport);
}

bool MediaSinkVideoMessageHandlers::ensureTransportStarted() const {
  if (transport_ && transport_->isRunning()) {
    return true;
  }

  if (!transport_) {
    transport_ = std::make_shared<buzz::autoapp::Transport::Transport>();
  }

  // Use defaults: Side A creator, no cleanup to allow existing readers.
  if (!transport_->startAsA(std::chrono::microseconds{1000})) {
    AASDK_LOG(error) << "[MediaSinkVideoMessageHandlers] Failed to start OpenAutoTransport (side A).";
    return false;
  }

  return transport_->isRunning();
}

uint64_t MediaSinkVideoMessageHandlers::resolveTimestamp(bool hasTimestamp, uint64_t parsedTs) const {
  if (hasTimestamp) {
    return parsedTs;
  }

  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

}
