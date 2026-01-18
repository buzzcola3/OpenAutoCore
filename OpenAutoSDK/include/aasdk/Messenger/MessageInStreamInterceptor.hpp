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
class PhoneStatusMessageHandlers;

bool handleMessage(const ::aasdk::messenger::Message& message);
void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);
void setVideoTransport(const std::shared_ptr<buzz::autoapp::Transport::Transport>& transport);
::aasdk::messenger::interceptor::InputSourceMessageHandlers& getInputSourceHandlers();
::aasdk::messenger::interceptor::SensorMessageHandlers& getSensorHandlers();
::aasdk::messenger::interceptor::PhoneStatusMessageHandlers& getPhoneStatusHandlers();

}
