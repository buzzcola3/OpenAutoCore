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
