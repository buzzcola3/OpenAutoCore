// Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
//
// This file is part of OpenAutoCore.
//
// OpenAutoCore is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// OpenAutoCore is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.

// Interceptor — handler dispatch and global Lite handler registry.

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
#include <Messenger/ChannelId.hpp>
#include <Common/Data.hpp>
#include <Common/Log.hpp>
#include <memory>
#include <mutex>
#include <utility>

namespace aasdk::messenger::interceptor {

namespace {

// Global SendFn — set by FrameRouter (or legacy MessageSender bridge).
// All handler lazy-sends resolve through this at call time.
aasdk::lite::SendFn gSendFn;

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
    if (gSendFn) gSendFn(ch, enc, mt, data, size);
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

} // namespace

void handleMessage(const ::aasdk::lite::InMessage& message) {
  using ChannelId = ::aasdk::messenger::ChannelId;

  switch (message.channelId) {
    case ChannelId::MEDIA_SINK_VIDEO:
      if (MEDIA_SINK_VIDEO_HANDLER) { (*MEDIA_SINK_VIDEO_HANDLER)(message); }
      break;
    case ChannelId::MEDIA_SINK_MEDIA_AUDIO:
      if (MEDIA_SINK_AUDIO_HANDLER) { (*MEDIA_SINK_AUDIO_HANDLER)(message); }
      break;
    case ChannelId::MEDIA_SINK_GUIDANCE_AUDIO:
      if (GUIDANCE_AUDIO_HANDLER) { (*GUIDANCE_AUDIO_HANDLER)(message); }
      break;
    case ChannelId::MEDIA_SINK_SYSTEM_AUDIO:
      if (SYSTEM_AUDIO_HANDLER) { (*SYSTEM_AUDIO_HANDLER)(message); }
      break;
    case ChannelId::MEDIA_SINK_TELEPHONY_AUDIO:
      if (TELEPHONY_AUDIO_HANDLER) { (*TELEPHONY_AUDIO_HANDLER)(message); }
      break;
    case ChannelId::INPUT_SOURCE:
      INPUT_SOURCE_HANDLER(message);
      break;
    case ChannelId::SENSOR:
      SENSOR_HANDLER(message);
      break;
    case ChannelId::BLUETOOTH:
      BLUETOOTH_HANDLER(message);
      break;
    case ChannelId::MEDIA_SOURCE_MICROPHONE:
      MEDIA_SOURCE_HANDLER(message);
      break;
    case ChannelId::PHONE_STATUS:
      PHONE_STATUS_HANDLER(message);
      break;
    case ChannelId::GENERIC_NOTIFICATION:
      GENERIC_NOTIFICATION_HANDLER(message);
      break;
    case ChannelId::NAVIGATION_STATUS:
      NAVIGATION_STATUS_HANDLER(message);
      break;
    case ChannelId::RADIO:
      RADIO_HANDLER(message);
      break;
    case ChannelId::MEDIA_BROWSER:
      MEDIA_BROWSER_HANDLER(message);
      break;
    case ChannelId::MEDIA_PLAYBACK_STATUS:
      MEDIA_PLAYBACK_STATUS_HANDLER(message);
      break;
    case ChannelId::VENDOR_EXTENSION:
      VENDOR_EXTENSION_HANDLER(message);
      break;
    case ChannelId::CONTROL:
      CONTROL_HANDLER(message);
      break;
    default:
      AASDK_LOG(warning) << "[Interceptor] Unhandled channel: "
                         << channelIdToString(message.channelId);
      break;
  }
}

void setSendFn(aasdk::lite::SendFn fn) {
  gSendFn = std::move(fn);
}

void setVideoTransport(const std::shared_ptr<buzz::autoapp::Transport::Transport>& transport) {
  MEDIA_SINK_VIDEO_HANDLER = std::make_unique<aasdk::lite::MediaSinkVideoHandler>(
      makeLazySendFn(), transport);
  MEDIA_SINK_AUDIO_HANDLER = std::make_unique<aasdk::lite::MediaSinkAudioHandler>(
      makeLazySendFn(), transport);
  GUIDANCE_AUDIO_HANDLER = std::make_unique<aasdk::lite::GuidanceAudioHandler>(
      makeLazySendFn(), transport);
  SYSTEM_AUDIO_HANDLER = std::make_unique<aasdk::lite::SystemAudioHandler>(
      makeLazySendFn(), transport);
  TELEPHONY_AUDIO_HANDLER = std::make_unique<aasdk::lite::TelephonyAudioHandler>(
      makeLazySendFn(), transport);
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
