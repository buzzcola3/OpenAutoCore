#include <Messenger/handlers/MediaBrowserMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageType.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/mediabrowser/MediaBrowserMessageId.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaBrowserInput.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaGetNode.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaListNode.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaRootNode.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaSongNode.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaSourceNode.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <limits>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using MediaBrowser = aap_protobuf::service::mediabrowser::MediaBrowserMessageId;
constexpr const char* kLogPrefix = "[MediaBrowserMessageHandlers]";

template<typename Proto>
bool parsePayload(const std::uint8_t* data, std::size_t size, Proto& message, const char* label) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    AASDK_LOG(error) << kLogPrefix << " " << label << " payload too large, bytes=" << size;
    return false;
  }

  if (!message.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << kLogPrefix << " Failed to parse " << label << " payload, bytes=" << size;
    return false;
  }

  return true;
}

}

namespace aasdk::messenger::interceptor {

bool MediaBrowserMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << kLogPrefix << " media browser payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      return handleChannelOpenRequest(message, payloadData, payloadSize);
    case MediaBrowser::MEDIA_ROOT_NODE:
      return handleMediaRootNode(payloadData, payloadSize);
    case MediaBrowser::MEDIA_SOURCE_NODE:
      return handleMediaSourceNode(payloadData, payloadSize);
    case MediaBrowser::MEDIA_LIST_NODE:
      return handleMediaListNode(payloadData, payloadSize);
    case MediaBrowser::MEDIA_SONG_NODE:
      return handleMediaSongNode(payloadData, payloadSize);
    case MediaBrowser::MEDIA_GET_NODE:
      return handleMediaGetNode(payloadData, payloadSize);
    case MediaBrowser::MEDIA_BROWSE_INPUT:
      return handleMediaBrowseInput(payloadData, payloadSize);
    default:
      AASDK_LOG(debug) << kLogPrefix << " message id=" << messageId.getId()
                       << " not explicitly handled.";
      return false;
  }
}

void MediaBrowserMessageHandlers::setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

bool MediaBrowserMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                           const std::uint8_t* data,
                                                           std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!parsePayload(data, size, request, "ChannelOpenRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ChannelOpenRequest: " << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  mediaBrowserChannelId_ = message.getChannelId();
  mediaBrowserEncryptionType_ = message.getEncryptionType();

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::CONTROL,
                          Control::MESSAGE_CHANNEL_OPEN_RESPONSE,
                          response);
    return true;
  }

  AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send channel open response.";
  return false;
}

bool MediaBrowserMessageHandlers::handleMediaRootNode(const std::uint8_t* data,
                                                      std::size_t size) const {
  aap_protobuf::service::mediabrowser::message::MediaRootNode root;
  if (!parsePayload(data, size, root, "MediaRootNode")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MediaRootNode: " << root.ShortDebugString();
  return true;
}

bool MediaBrowserMessageHandlers::handleMediaSourceNode(const std::uint8_t* data,
                                                        std::size_t size) const {
  aap_protobuf::service::mediabrowser::message::MediaSourceNode source;
  if (!parsePayload(data, size, source, "MediaSourceNode")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MediaSourceNode: " << source.ShortDebugString();
  return true;
}

bool MediaBrowserMessageHandlers::handleMediaListNode(const std::uint8_t* data,
                                                      std::size_t size) const {
  aap_protobuf::service::mediabrowser::message::MediaListNode list;
  if (!parsePayload(data, size, list, "MediaListNode")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MediaListNode: " << list.ShortDebugString();
  return true;
}

bool MediaBrowserMessageHandlers::handleMediaSongNode(const std::uint8_t* data,
                                                      std::size_t size) const {
  aap_protobuf::service::mediabrowser::message::MediaSongNode song;
  if (!parsePayload(data, size, song, "MediaSongNode")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MediaSongNode: " << song.ShortDebugString();
  return true;
}

bool MediaBrowserMessageHandlers::handleMediaGetNode(const std::uint8_t* data,
                                                     std::size_t size) const {
  aap_protobuf::service::mediabrowser::message::MediaGetNode getNode;
  if (!parsePayload(data, size, getNode, "MediaGetNode")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MediaGetNode: " << getNode.ShortDebugString();
  return true;
}

bool MediaBrowserMessageHandlers::handleMediaBrowseInput(const std::uint8_t* data,
                                                         std::size_t size) const {
  aap_protobuf::service::mediabrowser::message::MediaBrowserInput input;
  if (!parsePayload(data, size, input, "MediaBrowserInput")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MediaBrowserInput: " << input.ShortDebugString();
  return true;
}

}
