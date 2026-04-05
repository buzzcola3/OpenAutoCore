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
