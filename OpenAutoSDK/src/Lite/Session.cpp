#include <Lite/Session.hpp>

namespace aasdk::lite {

Session::Session(USBDevice device, messenger::ICryptor::Pointer cryptor)
    : device_(std::move(device)),
      frameIO_(device_, std::move(cryptor)) {}

ChannelRouter& Session::router() { return router_; }

FrameIO& Session::io() { return frameIO_; }

void Session::run() {
    running_.store(true);
    InMessage msg;

    while (running_.load()) {
        if (!frameIO_.readMessage(msg)) {
            break;
        }
        router_.dispatch(msg);
    }
    running_.store(false);
}

void Session::stop() {
    running_.store(false);
}

bool Session::isRunning() const {
    return running_.load();
}

} // namespace aasdk::lite
