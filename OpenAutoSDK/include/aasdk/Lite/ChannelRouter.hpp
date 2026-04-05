#pragma once

#include <array>
#include <functional>
#include <Messenger/ChannelId.hpp>
#include <Lite/FrameIO.hpp>

namespace aasdk::lite {

/// Dispatches complete messages to per-channel callbacks.
/// No threads, no queues — just a lookup table.
class ChannelRouter {
public:
    using Handler = std::function<void(const InMessage&)>;

    /// Register a handler for a channel. Replaces any previous handler.
    void setHandler(messenger::ChannelId ch, Handler handler);

    /// Remove the handler for a channel.
    void clearHandler(messenger::ChannelId ch);

    /// Dispatch a message to the registered handler.
    /// Returns false if no handler is registered for the channel.
    bool dispatch(const InMessage& msg);

private:
    std::array<Handler, 256> handlers_{};
};

} // namespace aasdk::lite
