// Copyright (C) 2024 CubeOne (Simon Dean - simon.dean@cubeone.co.uk)
//
// This file is part of OpenAutoCore.

#pragma once

#include <DeviceManager/DeviceConnection.hpp>

struct l_io;

// TCPDeviceConnection — byte-stream over a TCP socket fd.
//
// Owns the fd by default; start() installs an ELL read watch, stop() removes
// it. send() performs synchronous write(). releaseFd() transfers ownership to
// the caller (used by the AASDK bridge path to hand the fd to a boost socket).

class TCPDeviceConnection : public DeviceConnection {
public:
    explicit TCPDeviceConnection(int fd);
    ~TCPDeviceConnection() override;

    Type type() const override { return Type::TCP; }

    void start() override;
    void stop() override;
    void send(const uint8_t* data, size_t len) override;

    // Raw access for AASDK bridge (temporary)
    int fd() const { return fd_; }

    // Release fd ownership (prevents close on destruction).
    int releaseFd();

private:
    static bool onReadable(struct l_io* io, void* userData);

    int fd_;
    bool owning_ = true;
    struct l_io* readIo_ = nullptr;
};
