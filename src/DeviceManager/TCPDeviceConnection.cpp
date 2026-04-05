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

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <DeviceManager/EllCompat.hpp>
#include <DeviceManager/TCPDeviceConnection.hpp>
#include <DeviceManager/DmLog.hpp>

TCPDeviceConnection::TCPDeviceConnection(int fd)
    : fd_(fd) {}

TCPDeviceConnection::~TCPDeviceConnection() {
    stop();
    if (owning_ && fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void TCPDeviceConnection::start() {
    if (fd_ < 0 || readIo_) return;
    readIo_ = l_io_new(fd_);
    l_io_set_read_handler(readIo_, onReadable, this, nullptr);
}

void TCPDeviceConnection::stop() {
    if (readIo_) {
        l_io_destroy(readIo_);
        readIo_ = nullptr;
    }
}

void TCPDeviceConnection::send(const uint8_t* data, size_t len) {
    if (fd_ < 0) return;
    size_t offset = 0;
    while (offset < len) {
        ssize_t n = ::write(fd_, data + offset, len - offset);
        if (n < 0) {
            if (errno == EINTR) continue;
            DM_LOG(error) << "TCPDeviceConnection: write error: " << strerror(errno);
            if (errorCallback_) errorCallback_(strerror(errno));
            return;
        }
        offset += static_cast<size_t>(n);
    }
}

int TCPDeviceConnection::releaseFd() {
    owning_ = false;
    return fd_;
}

bool TCPDeviceConnection::onReadable(struct l_io*, void* userData) {
    auto* self = static_cast<TCPDeviceConnection*>(userData);
    uint8_t buffer[16384];
    ssize_t n = ::read(self->fd_, buffer, sizeof(buffer));
    if (n <= 0) {
        if (n < 0) {
            DM_LOG(error) << "TCPDeviceConnection: read error: " << strerror(errno);
            if (self->errorCallback_) self->errorCallback_(strerror(errno));
        } else {
            DM_LOG(info) << "TCPDeviceConnection: peer closed";
            if (self->errorCallback_) self->errorCallback_("peer closed");
        }
        return false;
    }
    if (self->readCallback_) {
        self->readCallback_(buffer, static_cast<size_t>(n));
    }
    return true;
}
