#include <Messenger/MessageSenderLocator.hpp>
#include <Messenger/MessageSender.hpp>

#include <memory>
#include <mutex>

namespace aasdk::messenger {
namespace {
std::weak_ptr<MessageSender> gSender;
std::mutex gSenderMutex;
}

void MessageSenderLocator::set(std::shared_ptr<MessageSender> sender) {
  std::lock_guard<std::mutex> lock(gSenderMutex);
  gSender = std::move(sender);
}

std::shared_ptr<MessageSender> MessageSenderLocator::get() {
  std::lock_guard<std::mutex> lock(gSenderMutex);
  return gSender.lock();
}

}
