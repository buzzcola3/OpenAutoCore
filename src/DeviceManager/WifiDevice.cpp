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

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <DeviceManager/EllCompat.hpp>
#include <DeviceManager/WifiDevice.hpp>


WifiDevice::WifiDevice(DeviceInfo info, int socketFd)
    : info_(std::move(info)), fd_(socketFd) {
    // Ensure non-blocking
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

    // TCP_NODELAY for low latency
    int opt = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    io_ = l_io_new(fd_);
    l_io_set_disconnect_handler(io_, onDisconnect, this, nullptr);
}

WifiDevice::~WifiDevice() {
    stop();
    if (io_) {
        l_io_destroy(io_);
        io_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void WifiDevice::send(const uint8_t* data, size_t size,
                      SendHandler onComplete, ErrorHandler onError) {
    sendBuffer_.assign(data, data + size);
    sendOffset_ = 0;
    pendingSendComplete_ = std::move(onComplete);
    pendingSendError_ = std::move(onError);
    trySend();
}

void WifiDevice::trySend() {
    while (sendOffset_ < sendBuffer_.size()) {
        ssize_t n = ::write(fd_, sendBuffer_.data() + sendOffset_,
                            sendBuffer_.size() - sendOffset_);
        if (n > 0) {
            sendOffset_ += static_cast<size_t>(n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            l_io_set_write_handler(io_, onWriteReady, this, nullptr);
            return;
        } else {
            auto handler = std::move(pendingSendError_);
            pendingSendComplete_ = nullptr;
            sendBuffer_.clear();
            if (handler) handler(std::strerror(errno));
            return;
        }
    }

    // All data sent
    sendBuffer_.clear();
    auto handler = std::move(pendingSendComplete_);
    pendingSendError_ = nullptr;
    if (handler) handler();
}

bool WifiDevice::onWriteReady(struct l_io* io, void* userData) {
    auto* self = static_cast<WifiDevice*>(userData);
    self->trySend();
    // If trySend finished or errored, pendingSendComplete_ is gone; stop watching.
    // If still sending, trySend re-registers the handler.
    return false;
}

void WifiDevice::receive(uint8_t* buffer, size_t maxSize,
                         ReceiveHandler onData, ErrorHandler onError) {
    recvBuffer_ = buffer;
    recvMaxSize_ = maxSize;
    pendingRecvData_ = std::move(onData);
    pendingRecvError_ = std::move(onError);

    l_io_set_read_handler(io_, onReadReady, this, nullptr);
}

bool WifiDevice::onReadReady(struct l_io* io, void* userData) {
    auto* self = static_cast<WifiDevice*>(userData);

    ssize_t n = ::read(self->fd_, self->recvBuffer_, self->recvMaxSize_);
    if (n > 0) {
        self->recvBuffer_ = nullptr;
        auto handler = std::move(self->pendingRecvData_);
        self->pendingRecvError_ = nullptr;
        handler(static_cast<size_t>(n));
    } else if (n == 0) {
        self->recvBuffer_ = nullptr;
        auto handler = std::move(self->pendingRecvError_);
        self->pendingRecvData_ = nullptr;
        if (handler) handler("Connection closed");
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return true; // keep watching
    } else {
        self->recvBuffer_ = nullptr;
        auto handler = std::move(self->pendingRecvError_);
        self->pendingRecvData_ = nullptr;
        if (handler) handler(std::strerror(errno));
    }

    return false; // one-shot
}

void WifiDevice::onDisconnect(struct l_io* io, void* userData) {
    auto* self = static_cast<WifiDevice*>(userData);
    self->stopped_ = true;

    // Fail any pending operations
    if (self->pendingSendError_) {
        auto handler = std::move(self->pendingSendError_);
        self->pendingSendComplete_ = nullptr;
        handler("Disconnected");
    }
    if (self->pendingRecvError_) {
        auto handler = std::move(self->pendingRecvError_);
        self->pendingRecvData_ = nullptr;
        handler("Disconnected");
    }
}

void WifiDevice::stop() {
    stopped_ = true;
    pendingSendComplete_ = nullptr;
    pendingSendError_ = nullptr;
    pendingRecvData_ = nullptr;
    pendingRecvError_ = nullptr;
    sendBuffer_.clear();
    recvBuffer_ = nullptr;

    if (io_) {
        l_io_set_read_handler(io_, nullptr, nullptr, nullptr);
        l_io_set_write_handler(io_, nullptr, nullptr, nullptr);
    }
}
