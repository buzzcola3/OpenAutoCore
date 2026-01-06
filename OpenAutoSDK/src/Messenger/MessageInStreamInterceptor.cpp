// Default interceptor implementation; override to short-circuit message
// delivery for specific channel IDs when required.

#include <Messenger/MessageInStreamInterceptor.hpp>
#include <Messenger/MediaSinkVideoMessageHandlers.hpp>
#include <Messenger/InputSourceMessageHandlers.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageSenderLocator.hpp>
#include <Messenger/Message.hpp>
#include <Messenger/ChannelId.hpp>
#include <memory>
#include <mutex>
#include <utility>

namespace aasdk::messenger::interceptor {

namespace {

MediaSinkVideoMessageHandlers MEDIA_SINK_VIDEO_HANDLERS;
InputSourceMessageHandlers INPUT_SOURCE_HANDLERS;

}

bool handleMessage(const ::aasdk::messenger::Message& message) {
  switch (message.getChannelId()) {
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_VIDEO:
      return MEDIA_SINK_VIDEO_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::INPUT_SOURCE:
      return INPUT_SOURCE_HANDLERS.handle(message);
    default:
      return false;
  }
}

void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  MessageSenderLocator::set(sender);
  MEDIA_SINK_VIDEO_HANDLERS.setMessageSender(sender);
  INPUT_SOURCE_HANDLERS.setMessageSender(sender);
}

void setVideoTransport(std::shared_ptr<buzz::autoapp::Transport::Transport> transport) {
  MEDIA_SINK_VIDEO_HANDLERS.setTransport(std::move(transport));
}

}
