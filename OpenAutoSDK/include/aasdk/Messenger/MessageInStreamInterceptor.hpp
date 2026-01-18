// This file is part of aasdk library project.
//
// Interceptor entry point for custom message handling during message stream
// processing. Returning true from the interceptor consumes the message and
// prevents the default channel handlers from seeing it.

#pragma once

#include <memory>

namespace buzz { namespace autoapp { namespace Transport { class Transport; } } }

namespace aasdk::messenger {
	class Message;
	class MessageSender;
}

namespace aasdk::messenger::interceptor {

class InputSourceMessageHandlers;
class SensorMessageHandlers;
class BluetoothMessageHandlers;
class MediaSourceMessageHandlers;
class PhoneStatusMessageHandlers;
class GenericNotificationMessageHandlers;
class NavigationStatusMessageHandlers;
class RadioMessageHandlers;
class MediaBrowserMessageHandlers;
class MediaPlaybackStatusMessageHandlers;
class VendorExtensionMessageHandlers;

bool handleMessage(const ::aasdk::messenger::Message& message);
void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);
void setVideoTransport(const std::shared_ptr<buzz::autoapp::Transport::Transport>& transport);
::aasdk::messenger::interceptor::InputSourceMessageHandlers& getInputSourceHandlers();
::aasdk::messenger::interceptor::SensorMessageHandlers& getSensorHandlers();
::aasdk::messenger::interceptor::BluetoothMessageHandlers& getBluetoothHandlers();
::aasdk::messenger::interceptor::MediaSourceMessageHandlers& getMediaSourceHandlers();
::aasdk::messenger::interceptor::PhoneStatusMessageHandlers& getPhoneStatusHandlers();
::aasdk::messenger::interceptor::GenericNotificationMessageHandlers& getGenericNotificationHandlers();
::aasdk::messenger::interceptor::NavigationStatusMessageHandlers& getNavigationStatusHandlers();
::aasdk::messenger::interceptor::RadioMessageHandlers& getRadioHandlers();
::aasdk::messenger::interceptor::MediaBrowserMessageHandlers& getMediaBrowserHandlers();
::aasdk::messenger::interceptor::MediaPlaybackStatusMessageHandlers& getMediaPlaybackStatusHandlers();
::aasdk::messenger::interceptor::VendorExtensionMessageHandlers& getVendorExtensionHandlers();

}
