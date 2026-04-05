// Copyright (C) 2024 CubeOne (Simon Dean - simon.dean@cubeone.co.uk)
//
// This file is part of OpenAutoCore.

#pragma once

#include <vector>
#include <Transport/Transport.hpp>
#include <DeviceManager/DeviceConnection.hpp>

namespace f1x::openauto::autoapp {

// DeviceConnectionTransport — adapts DeviceConnection to AASDK ITransport.
//
// Bridges DeviceManager's universal byte-stream (libusb bulk / TCP socket)
// into the AASDK promise-based transport interface so the existing Messenger,
// Cryptor, and AndroidAutoEntity can work unchanged.
//
// Incoming data from DeviceConnection is buffered and fed to the Messenger's
// receive requests. Outgoing data is sent synchronously via DeviceConnection.

class DeviceConnectionTransport : public aasdk::transport::Transport {
public:
    DeviceConnectionTransport(boost::asio::io_service& ioService,
                              DeviceConnection::Pointer connection);
    ~DeviceConnectionTransport() override;

    void stop() override;

private:
    void enqueueReceive(aasdk::common::DataBuffer buffer) override;
    void enqueueSend(SendQueue::iterator queueElement) override;

    DeviceConnection::Pointer connection_;
    std::vector<uint8_t> incomingBuffer_;
    aasdk::common::DataBuffer pendingReadBuffer_;
};

}
