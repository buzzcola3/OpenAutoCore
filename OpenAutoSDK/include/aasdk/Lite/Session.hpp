#pragma once

#include <atomic>
#include <Lite/USBDevice.hpp>
#include <Lite/FrameIO.hpp>
#include <Lite/ChannelRouter.hpp>
#include <Messenger/ICryptor.hpp>

namespace aasdk::lite {

/// Owns USBDevice + FrameIO + ChannelRouter.
/// Provides a blocking single-threaded message pump.
class Session {
public:
    Session(USBDevice device, messenger::ICryptor::Pointer cryptor);

    /// Access the router to register/remove channel handlers.
    ChannelRouter& router();

    /// Access the FrameIO layer to send messages from within handlers.
    FrameIO& io();

    /// Blocking message pump: readMessage → dispatch, until error or stop().
    void run();

    /// Signal the pump to exit (thread-safe).
    void stop();

    /// Check if the session is running.
    bool isRunning() const;

private:
    USBDevice device_;
    FrameIO frameIO_;
    ChannelRouter router_;
    std::atomic<bool> running_{false};
};

} // namespace aasdk::lite
