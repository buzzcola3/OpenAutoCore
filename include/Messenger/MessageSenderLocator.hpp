#pragma once

#include <memory>

namespace aasdk::messenger {

class MessageSender;

class MessageSenderLocator {
public:
  static void set(std::shared_ptr<MessageSender> sender);
  static std::shared_ptr<MessageSender> get();
};

}
