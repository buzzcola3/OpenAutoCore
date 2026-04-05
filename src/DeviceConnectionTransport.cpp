// Copyright (C) 2024 CubeOne (Simon Dean - simon.dean@cubeone.co.uk)
//
// This file is part of OpenAutoCore.

#include <cstring>
#include <Error/Error.hpp>
#include <DeviceConnectionTransport.hpp>

namespace f1x::openauto::autoapp {

DeviceConnectionTransport::DeviceConnectionTransport(
    boost::asio::io_service& ioService, DeviceConnection::Pointer connection)
    : Transport(ioService), connection_(std::move(connection))
{
    connection_->setReadCallback([this](const uint8_t* data, size_t len) {
        receiveStrand_.post([this, self = shared_from_this(),
                             d = std::vector<uint8_t>(data, data + len)]() {
            if (pendingReadBuffer_.data) {
                size_t n = std::min(d.size(), static_cast<size_t>(pendingReadBuffer_.size));
                std::memcpy(pendingReadBuffer_.data, d.data(), n);
                if (d.size() > n) {
                    incomingBuffer_.insert(incomingBuffer_.end(), d.begin() + n, d.end());
                }
                pendingReadBuffer_ = aasdk::common::DataBuffer();
                receiveHandler(n);
            } else {
                incomingBuffer_.insert(incomingBuffer_.end(), d.begin(), d.end());
            }
        });
    });

    connection_->setErrorCallback([this](const std::string&) {
        receiveStrand_.post([this, self = shared_from_this()]() {
            rejectReceivePromises(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_ABORTED));
        });
    });

    connection_->start();
}

DeviceConnectionTransport::~DeviceConnectionTransport() = default;

void DeviceConnectionTransport::enqueueReceive(aasdk::common::DataBuffer buffer) {
    if (!incomingBuffer_.empty()) {
        size_t n = std::min(incomingBuffer_.size(), static_cast<size_t>(buffer.size));
        std::memcpy(buffer.data, incomingBuffer_.data(), n);
        incomingBuffer_.erase(incomingBuffer_.begin(), incomingBuffer_.begin() + n);
        // Post to avoid recursion (distributeReceivedData → enqueueReceive → receiveHandler → distributeReceivedData)
        receiveStrand_.post([this, self = shared_from_this(), n]() {
            receiveHandler(n);
        });
    } else {
        pendingReadBuffer_ = buffer;
    }
}

void DeviceConnectionTransport::enqueueSend(SendQueue::iterator queueElement) {
    connection_->send(queueElement->first.data(), queueElement->first.size());
    queueElement->second->resolve();
    sendQueue_.erase(queueElement);
    if (!sendQueue_.empty()) {
        enqueueSend(sendQueue_.begin());
    }
}

void DeviceConnectionTransport::stop() {
    connection_->stop();
}

}
