#include <Lite/ChannelRouter.hpp>

namespace aasdk::lite {

void ChannelRouter::setHandler(messenger::ChannelId ch, Handler handler) {
    handlers_[static_cast<uint8_t>(ch)] = std::move(handler);
}

void ChannelRouter::clearHandler(messenger::ChannelId ch) {
    handlers_[static_cast<uint8_t>(ch)] = nullptr;
}

bool ChannelRouter::dispatch(const InMessage& msg) {
    auto& h = handlers_[static_cast<uint8_t>(msg.channelId)];
    if (!h) return false;
    h(msg);
    return true;
}

} // namespace aasdk::lite
