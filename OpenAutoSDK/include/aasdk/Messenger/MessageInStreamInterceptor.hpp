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

namespace aasdk::lite {
class BluetoothHandler;
class MediaSourceHandler;
class InputSourceHandler;
class SensorHandler;
class PhoneStatusHandler;
class GenericNotificationHandler;
class NavigationStatusHandler;
class RadioHandler;
class MediaBrowserHandler;
class MediaPlaybackStatusHandler;
class VendorExtensionHandler;
class ControlHandler;
}

namespace aasdk::messenger::interceptor {

bool handleMessage(const ::aasdk::messenger::Message& message);
void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);
void setVideoTransport(const std::shared_ptr<buzz::autoapp::Transport::Transport>& transport);
::aasdk::lite::InputSourceHandler& getInputSourceHandler();
::aasdk::lite::SensorHandler& getSensorHandler();
::aasdk::lite::BluetoothHandler& getBluetoothHandler();
::aasdk::lite::MediaSourceHandler& getMediaSourceHandler();
::aasdk::lite::ControlHandler& getControlHandler();

}
