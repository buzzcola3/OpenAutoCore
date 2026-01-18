// Default interceptor implementation; override to short-circuit message
// delivery for specific channel IDs when required.

#include <Messenger/MessageInStreamInterceptor.hpp>
#include <Messenger/handlers/MediaSinkAudioMessageHandlers.hpp>
#include <Messenger/handlers/GuidanceAudioMessageHandlers.hpp>
#include <Messenger/handlers/SystemAudioMessageHandlers.hpp>
#include <Messenger/handlers/TelephonyAudioMessageHandlers.hpp>
#include <Messenger/handlers/MediaSinkVideoMessageHandlers.hpp>
#include <Messenger/handlers/InputSourceMessageHandlers.hpp>
#include <Messenger/handlers/SensorMessageHandlers.hpp>
#include <Messenger/handlers/BluetoothMessageHandlers.hpp>
#include <Messenger/handlers/MediaSourceMessageHandlers.hpp>
#include <Messenger/handlers/PhoneStatusMessageHandlers.hpp>
#include <Messenger/handlers/GenericNotificationMessageHandlers.hpp>
#include <Messenger/handlers/NavigationStatusMessageHandlers.hpp>
#include <Messenger/handlers/RadioMessageHandlers.hpp>
#include <Messenger/handlers/MediaBrowserMessageHandlers.hpp>
#include <Messenger/handlers/MediaPlaybackStatusMessageHandlers.hpp>
#include <Messenger/handlers/VendorExtensionMessageHandlers.hpp>
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
MediaSinkAudioMessageHandlers MEDIA_SINK_AUDIO_HANDLERS;
GuidanceAudioMessageHandlers MEDIA_SINK_GUIDANCE_AUDIO_HANDLERS;
SystemAudioMessageHandlers MEDIA_SINK_SYSTEM_AUDIO_HANDLERS;
TelephonyAudioMessageHandlers MEDIA_SINK_TELEPHONY_AUDIO_HANDLERS;
InputSourceMessageHandlers INPUT_SOURCE_HANDLERS;
SensorMessageHandlers SENSOR_HANDLERS;
BluetoothMessageHandlers BLUETOOTH_HANDLERS;
MediaSourceMessageHandlers MEDIA_SOURCE_HANDLERS;
PhoneStatusMessageHandlers PHONE_STATUS_HANDLERS;
GenericNotificationMessageHandlers GENERIC_NOTIFICATION_HANDLERS;
NavigationStatusMessageHandlers NAVIGATION_STATUS_HANDLERS;
RadioMessageHandlers RADIO_HANDLERS;
MediaBrowserMessageHandlers MEDIA_BROWSER_HANDLERS;
MediaPlaybackStatusMessageHandlers MEDIA_PLAYBACK_STATUS_HANDLERS;
VendorExtensionMessageHandlers VENDOR_EXTENSION_HANDLERS;

}

bool handleMessage(const ::aasdk::messenger::Message& message) {
  switch (message.getChannelId()) {
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_VIDEO:
      return MEDIA_SINK_VIDEO_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_MEDIA_AUDIO:
      return MEDIA_SINK_AUDIO_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_GUIDANCE_AUDIO:
      return MEDIA_SINK_GUIDANCE_AUDIO_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_SYSTEM_AUDIO:
      return MEDIA_SINK_SYSTEM_AUDIO_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::MEDIA_SINK_TELEPHONY_AUDIO:
      return MEDIA_SINK_TELEPHONY_AUDIO_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::INPUT_SOURCE:
      return INPUT_SOURCE_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::SENSOR:
      return SENSOR_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::BLUETOOTH:
      return BLUETOOTH_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::MEDIA_SOURCE_MICROPHONE:
      return MEDIA_SOURCE_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::PHONE_STATUS:
      return PHONE_STATUS_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::GENERIC_NOTIFICATION:
      return GENERIC_NOTIFICATION_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::NAVIGATION_STATUS:
      return NAVIGATION_STATUS_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::RADIO:
      return RADIO_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::MEDIA_BROWSER:
      return MEDIA_BROWSER_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::MEDIA_PLAYBACK_STATUS:
      return MEDIA_PLAYBACK_STATUS_HANDLERS.handle(message);
    case ::aasdk::messenger::ChannelId::VENDOR_EXTENSION:
      return VENDOR_EXTENSION_HANDLERS.handle(message);
    default:
      return false;
  }
}

void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  MessageSenderLocator::set(sender);
  MEDIA_SINK_VIDEO_HANDLERS.setMessageSender(sender);
  MEDIA_SINK_AUDIO_HANDLERS.setMessageSender(sender);
  MEDIA_SINK_GUIDANCE_AUDIO_HANDLERS.setMessageSender(sender);
  MEDIA_SINK_SYSTEM_AUDIO_HANDLERS.setMessageSender(sender);
  MEDIA_SINK_TELEPHONY_AUDIO_HANDLERS.setMessageSender(sender);
  INPUT_SOURCE_HANDLERS.setMessageSender(sender);
  SENSOR_HANDLERS.setMessageSender(sender);
  BLUETOOTH_HANDLERS.setMessageSender(sender);
  MEDIA_SOURCE_HANDLERS.setMessageSender(sender);
  PHONE_STATUS_HANDLERS.setMessageSender(sender);
  GENERIC_NOTIFICATION_HANDLERS.setMessageSender(sender);
  NAVIGATION_STATUS_HANDLERS.setMessageSender(sender);
  RADIO_HANDLERS.setMessageSender(sender);
  MEDIA_BROWSER_HANDLERS.setMessageSender(sender);
  MEDIA_PLAYBACK_STATUS_HANDLERS.setMessageSender(sender);
  VENDOR_EXTENSION_HANDLERS.setMessageSender(sender);
}

void setVideoTransport(const std::shared_ptr<buzz::autoapp::Transport::Transport>& transport) {
  MEDIA_SINK_VIDEO_HANDLERS.setTransport(transport);
  MEDIA_SINK_AUDIO_HANDLERS.setTransport(transport);
  MEDIA_SINK_GUIDANCE_AUDIO_HANDLERS.setTransport(transport);
  MEDIA_SINK_SYSTEM_AUDIO_HANDLERS.setTransport(transport);
  MEDIA_SINK_TELEPHONY_AUDIO_HANDLERS.setTransport(transport);
}

InputSourceMessageHandlers& getInputSourceHandlers() {
  return INPUT_SOURCE_HANDLERS;
}

SensorMessageHandlers& getSensorHandlers() {
  return SENSOR_HANDLERS;
}

BluetoothMessageHandlers& getBluetoothHandlers() {
  return BLUETOOTH_HANDLERS;
}

MediaSourceMessageHandlers& getMediaSourceHandlers() {
  return MEDIA_SOURCE_HANDLERS;
}

PhoneStatusMessageHandlers& getPhoneStatusHandlers() {
  return PHONE_STATUS_HANDLERS;
}

GenericNotificationMessageHandlers& getGenericNotificationHandlers() {
  return GENERIC_NOTIFICATION_HANDLERS;
}

NavigationStatusMessageHandlers& getNavigationStatusHandlers() {
  return NAVIGATION_STATUS_HANDLERS;
}

RadioMessageHandlers& getRadioHandlers() {
  return RADIO_HANDLERS;
}

MediaBrowserMessageHandlers& getMediaBrowserHandlers() {
  return MEDIA_BROWSER_HANDLERS;
}

MediaPlaybackStatusMessageHandlers& getMediaPlaybackStatusHandlers() {
  return MEDIA_PLAYBACK_STATUS_HANDLERS;
}

VendorExtensionMessageHandlers& getVendorExtensionHandlers() {
  return VENDOR_EXTENSION_HANDLERS;
}

}
