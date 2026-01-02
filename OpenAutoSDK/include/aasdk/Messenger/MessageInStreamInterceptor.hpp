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

bool handleMessage(const ::aasdk::messenger::Message& message);
void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);
void setVideoTransport(std::shared_ptr<buzz::autoapp::Transport::Transport> transport);

}
