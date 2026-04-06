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
#include <poll.h>
#include <sys/socket.h>
#include <DeviceManager/Wireless/TCPDeviceConnection.hpp>
#include <DeviceManager/Common/DmLog.hpp>

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
    if (fd_ < 0 || reading_) return;
    reading_ = true;
    readThread_ = std::thread([this] { readLoop(); });
}

void TCPDeviceConnection::stop() {
    reading_ = false;
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
    }
    if (readThread_.joinable()) readThread_.join();
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

void TCPDeviceConnection::readLoop() {
    uint8_t buffer[16384];
    while (reading_) {
        struct pollfd pfd = {fd_, POLLIN, 0};
        int rc = ::poll(&pfd, 1, 100);
        if (rc < 0) {
            if (errno == EINTR) continue;
            DM_LOG(error) << "TCPDeviceConnection: poll error: " << strerror(errno);
            if (errorCallback_) errorCallback_(strerror(errno));
            break;
        }
        if (rc == 0) continue;

        ssize_t n = ::read(fd_, buffer, sizeof(buffer));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            DM_LOG(error) << "TCPDeviceConnection: read error: " << strerror(errno);
            if (errorCallback_) errorCallback_(strerror(errno));
            break;
        }
        if (n == 0) {
            DM_LOG(info) << "TCPDeviceConnection: peer closed";
            if (errorCallback_) errorCallback_("peer closed");
            break;
        }
        if (readCallback_) {
            readCallback_(buffer, static_cast<size_t>(n));
        }
    }
}
