// Default interceptor implementation; override to short-circuit message
// delivery for specific channel IDs when required.

#include <Messenger/MessageInStreamInterceptor.hpp>
#include <Lite/BluetoothHandler.hpp>
#include <Lite/GenericNotificationHandler.hpp>
#include <Lite/GuidanceAudioHandler.hpp>
#include <Lite/InputSourceHandler.hpp>
#include <Lite/MediaBrowserHandler.hpp>
#include <Lite/MediaPlaybackStatusHandler.hpp>
#include <Lite/MediaSinkAudioHandler.hpp>
#include <Lite/MediaSinkVideoHandler.hpp>
#include <Lite/MediaSourceHandler.hpp>
#include <Lite/NavigationStatusHandler.hpp>
#include <Lite/PhoneStatusHandler.hpp>
#include <Lite/RadioHandler.hpp>
#include <Lite/SensorHandler.hpp>
#include <Lite/SystemAudioHandler.hpp>
#include <Lite/TelephonyAudioHandler.hpp>
#include <Lite/VendorExtensionHandler.hpp>
#include <Lite/ControlHandler.hpp>
#include <Lite/FrameIO.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageSenderLocator.hpp>
#include <Messenger/Message.hpp>
#include <Messenger/ChannelId.hpp>
#include <Common/Data.hpp>
#include <memory>
#include <mutex>
#include <utility>

namespace aasdk::messenger::interceptor {

namespace {

std::shared_ptr<::aasdk::messenger::MessageSender> MESSAGE_SENDER_STRONG_REF;

std::unique_ptr<aasdk::lite::MediaSinkVideoHandler> MEDIA_SINK_VIDEO_HANDLER;
std::unique_ptr<aasdk::lite::MediaSinkAudioHandler> MEDIA_SINK_AUDIO_HANDLER;
std::unique_ptr<aasdk::lite::GuidanceAudioHandler> GUIDANCE_AUDIO_HANDLER;
std::unique_ptr<aasdk::lite::SystemAudioHandler> SYSTEM_AUDIO_HANDLER;
std::unique_ptr<aasdk::lite::TelephonyAudioHandler> TELEPHONY_AUDIO_HANDLER;
aasdk::lite::SendFn makeLazySendFn() {
  return [](::aasdk::messenger::ChannelId ch,
            ::aasdk::messenger::EncryptionType enc,
            ::aasdk::messenger::MessageType mt,
            const uint8_t* data, size_t size) {
    auto s = MessageSenderLocator::get();
    if (!s || size < 2) return;
    uint16_t msgId = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    ::aasdk::common::DataConstBuffer buf(data + 2, size - 2);
    s->sendRaw(ch, enc, mt, msgId, buf);
  };
}

aasdk::lite::InputSourceHandler INPUT_SOURCE_HANDLER{makeLazySendFn()};
aasdk::lite::SensorHandler SENSOR_HANDLER{makeLazySendFn()};
aasdk::lite::BluetoothHandler BLUETOOTH_HANDLER{makeLazySendFn()};
aasdk::lite::MediaSourceHandler MEDIA_SOURCE_HANDLER{makeLazySendFn()};
aasdk::lite::PhoneStatusHandler PHONE_STATUS_HANDLER{makeLazySendFn()};
aasdk::lite::GenericNotificationHandler GENERIC_NOTIFICATION_HANDLER{makeLazySendFn()};
aasdk::lite::NavigationStatusHandler NAVIGATION_STATUS_HANDLER{makeLazySendFn()};
aasdk::lite::RadioHandler RADIO_HANDLER{makeLazySendFn()};
aasdk::lite::MediaBrowserHandler MEDIA_BROWSER_HANDLER{makeLazySendFn()};
aasdk::lite::MediaPlaybackStatusHandler MEDIA_PLAYBACK_STATUS_HANDLER{makeLazySendFn()};
aasdk::lite::VendorExtensionHandler VENDOR_EXTENSION_HANDLER{makeLazySendFn()};
aasdk::lite::ControlHandler CONTROL_HANDLER{makeLazySendFn()};

}

bool handleMessage(const ::aasdk::messenger::Message& message) {
  switch (message.getChannelId()) {
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_VIDEO:
      if (MEDIA_SINK_VIDEO_HANDLER) {
        // Adapt old Message to Lite InMessage and dispatch
        aasdk::lite::InMessage in;
        in.channelId = message.getChannelId();
        in.encryptionType = message.getEncryptionType();
        in.messageType = message.getType();
        in.payload.assign(message.getPayload().begin(), message.getPayload().end());
        (*MEDIA_SINK_VIDEO_HANDLER)(in);
        return true;
      }
      return false;
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_MEDIA_AUDIO:
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_GUIDANCE_AUDIO:
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_SYSTEM_AUDIO:
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_TELEPHONY_AUDIO: {
      auto* handler = [&]() -> void* {
        switch (message.getChannelId()) {
          case ::aasdk::messenger::ChannelId::MEDIA_SINK_MEDIA_AUDIO:     return MEDIA_SINK_AUDIO_HANDLER.get();
          case ::aasdk::messenger::ChannelId::MEDIA_SINK_GUIDANCE_AUDIO:  return GUIDANCE_AUDIO_HANDLER.get();
          case ::aasdk::messenger::ChannelId::MEDIA_SINK_SYSTEM_AUDIO:    return SYSTEM_AUDIO_HANDLER.get();
          case ::aasdk::messenger::ChannelId::MEDIA_SINK_TELEPHONY_AUDIO: return TELEPHONY_AUDIO_HANDLER.get();
          default: return nullptr;
        }
      }();
      if (!handler) return false;
      aasdk::lite::InMessage in;
      in.channelId = message.getChannelId();
      in.encryptionType = message.getEncryptionType();
      in.messageType = message.getType();
      in.payload.assign(message.getPayload().begin(), message.getPayload().end());
      switch (message.getChannelId()) {
        case ::aasdk::messenger::ChannelId::MEDIA_SINK_MEDIA_AUDIO:     (*MEDIA_SINK_AUDIO_HANDLER)(in); break;
        case ::aasdk::messenger::ChannelId::MEDIA_SINK_GUIDANCE_AUDIO:  (*GUIDANCE_AUDIO_HANDLER)(in); break;
        case ::aasdk::messenger::ChannelId::MEDIA_SINK_SYSTEM_AUDIO:    (*SYSTEM_AUDIO_HANDLER)(in); break;
        case ::aasdk::messenger::ChannelId::MEDIA_SINK_TELEPHONY_AUDIO: (*TELEPHONY_AUDIO_HANDLER)(in); break;
        default: break;
      }
      return true;
    }
    case ::aasdk::messenger::ChannelId::INPUT_SOURCE: {
        aasdk::lite::InMessage in;
        in.channelId = message.getChannelId();
        in.encryptionType = message.getEncryptionType();
        in.messageType = message.getType();
        in.payload.assign(message.getPayload().begin(), message.getPayload().end());
        INPUT_SOURCE_HANDLER(in);
        return true;
      }
    case ::aasdk::messenger::ChannelId::SENSOR: {
        aasdk::lite::InMessage in;
        in.channelId = message.getChannelId();
        in.encryptionType = message.getEncryptionType();
        in.messageType = message.getType();
        in.payload.assign(message.getPayload().begin(), message.getPayload().end());
        SENSOR_HANDLER(in);
        return true;
      }
    case ::aasdk::messenger::ChannelId::BLUETOOTH:
    case ::aasdk::messenger::ChannelId::MEDIA_SOURCE_MICROPHONE:
    case ::aasdk::messenger::ChannelId::PHONE_STATUS:
    case ::aasdk::messenger::ChannelId::GENERIC_NOTIFICATION:
    case ::aasdk::messenger::ChannelId::NAVIGATION_STATUS:
    case ::aasdk::messenger::ChannelId::RADIO:
    case ::aasdk::messenger::ChannelId::MEDIA_BROWSER:
    case ::aasdk::messenger::ChannelId::MEDIA_PLAYBACK_STATUS:
    case ::aasdk::messenger::ChannelId::VENDOR_EXTENSION: {
      aasdk::lite::InMessage in;
      in.channelId = message.getChannelId();
      in.encryptionType = message.getEncryptionType();
      in.messageType = message.getType();
      in.payload.assign(message.getPayload().begin(), message.getPayload().end());
      switch (message.getChannelId()) {
        case ::aasdk::messenger::ChannelId::BLUETOOTH:              BLUETOOTH_HANDLER(in); break;
        case ::aasdk::messenger::ChannelId::MEDIA_SOURCE_MICROPHONE: MEDIA_SOURCE_HANDLER(in); break;
        case ::aasdk::messenger::ChannelId::PHONE_STATUS:           PHONE_STATUS_HANDLER(in); break;
        case ::aasdk::messenger::ChannelId::GENERIC_NOTIFICATION:   GENERIC_NOTIFICATION_HANDLER(in); break;
        case ::aasdk::messenger::ChannelId::NAVIGATION_STATUS:      NAVIGATION_STATUS_HANDLER(in); break;
        case ::aasdk::messenger::ChannelId::RADIO:                  RADIO_HANDLER(in); break;
        case ::aasdk::messenger::ChannelId::MEDIA_BROWSER:          MEDIA_BROWSER_HANDLER(in); break;
        case ::aasdk::messenger::ChannelId::MEDIA_PLAYBACK_STATUS:  MEDIA_PLAYBACK_STATUS_HANDLER(in); break;
        case ::aasdk::messenger::ChannelId::VENDOR_EXTENSION:       VENDOR_EXTENSION_HANDLER(in); break;
        default: break;
      }
      return true;
    }
    case ::aasdk::messenger::ChannelId::CONTROL: {
      aasdk::lite::InMessage in;
      in.channelId = message.getChannelId();
      in.encryptionType = message.getEncryptionType();
      in.messageType = message.getType();
      in.payload.assign(message.getPayload().begin(), message.getPayload().end());
      CONTROL_HANDLER(in);
      return true;
    }
    default:
      return false;
  }
}

void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  MESSAGE_SENDER_STRONG_REF = sender;
  MessageSenderLocator::set(sender);
}

void setVideoTransport(const std::shared_ptr<buzz::autoapp::Transport::Transport>& transport) {
  // Create the Lite video handler with a SendFn that resolves MessageSender lazily,
  // since setVideoTransport may be called before setMessageSender.
  aasdk::lite::SendFn sendFn = [](::aasdk::messenger::ChannelId ch,
                                  ::aasdk::messenger::EncryptionType enc,
                                  ::aasdk::messenger::MessageType mt,
                                  const uint8_t* data, size_t size) {
    auto sender = MessageSenderLocator::get();
    if (!sender || size < 2) return;
    uint16_t msgId = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    ::aasdk::common::DataConstBuffer buf(data + 2, size - 2);
    sender->sendRaw(ch, enc, mt, msgId, buf);
  };
  MEDIA_SINK_VIDEO_HANDLER = std::make_unique<aasdk::lite::MediaSinkVideoHandler>(
      std::move(sendFn), transport);
  MEDIA_SINK_AUDIO_HANDLER = std::make_unique<aasdk::lite::MediaSinkAudioHandler>(makeLazySendFn(), transport);
  GUIDANCE_AUDIO_HANDLER = std::make_unique<aasdk::lite::GuidanceAudioHandler>(makeLazySendFn(), transport);
  SYSTEM_AUDIO_HANDLER = std::make_unique<aasdk::lite::SystemAudioHandler>(makeLazySendFn(), transport);
  TELEPHONY_AUDIO_HANDLER = std::make_unique<aasdk::lite::TelephonyAudioHandler>(makeLazySendFn(), transport);
}

aasdk::lite::InputSourceHandler& getInputSourceHandler() {
  return INPUT_SOURCE_HANDLER;
}

aasdk::lite::SensorHandler& getSensorHandler() {
  return SENSOR_HANDLER;
}

aasdk::lite::BluetoothHandler& getBluetoothHandler() {
  return BLUETOOTH_HANDLER;
}

aasdk::lite::MediaSourceHandler& getMediaSourceHandler() {
  return MEDIA_SOURCE_HANDLER;
}

aasdk::lite::ControlHandler& getControlHandler() {
  return CONTROL_HANDLER;
}

}
