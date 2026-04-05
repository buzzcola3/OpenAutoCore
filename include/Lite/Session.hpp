// Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
//
// This file is part of OpenAutoCore.
//
// OpenAutoCore is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// OpenAutoCore is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.

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
