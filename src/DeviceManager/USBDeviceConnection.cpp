// Copyright (C) 2024 CubeOne (Simon Dean - simon.dean@cubeone.co.uk)
//
// This file is part of OpenAutoCore.

#include <cstring>
#include <DeviceManager/USBDeviceConnection.hpp>
#include <DeviceManager/DmLog.hpp>

USBDeviceConnection::USBDeviceConnection(std::shared_ptr<libusb_device_handle> handle,
                                         libusb_context* context)
    : handle_(std::move(handle)), context_(context) {}

USBDeviceConnection::~USBDeviceConnection() {
    stop();
    if (claimed_ && handle_) {
        libusb_release_interface(handle_.get(), interfaceNum_);
    }
}

bool USBDeviceConnection::discoverEndpoints() {
    auto* device = libusb_get_device(handle_.get());
    libusb_config_descriptor* config = nullptr;
    if (libusb_get_config_descriptor(device, 0, &config) != 0) {
        DM_LOG(error) << "USBDeviceConnection: failed to get config descriptor";
        return false;
    }

    bool found = false;
    for (int i = 0; i < config->bNumInterfaces && !found; ++i) {
        const auto& iface = config->interface[i];
        for (int j = 0; j < iface.num_altsetting && !found; ++j) {
            const auto& alt = iface.altsetting[j];
            if (alt.bNumEndpoints != 2) continue;

            uint8_t in = 0, out = 0;
            for (int e = 0; e < alt.bNumEndpoints; ++e) {
                const auto& ep = alt.endpoint[e];
                if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
                    continue;
                if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN)
                    in = ep.bEndpointAddress;
                else
                    out = ep.bEndpointAddress;
            }
            if (in && out) {
                inEndpoint_ = in;
                outEndpoint_ = out;
                interfaceNum_ = alt.bInterfaceNumber;
                found = true;
            }
        }
    }
    libusb_free_config_descriptor(config);

    if (!found) {
        DM_LOG(error) << "USBDeviceConnection: no suitable bulk interface found";
        return false;
    }

    int rc = libusb_claim_interface(handle_.get(), interfaceNum_);
    if (rc != 0) {
        DM_LOG(error) << "USBDeviceConnection: claim_interface failed: "
                      << libusb_strerror(static_cast<libusb_error>(rc));
        return false;
    }
    claimed_ = true;
    return true;
}

void USBDeviceConnection::start() {
    if (reading_) return;
    if (!claimed_ && !discoverEndpoints()) return;

    reading_ = true;
    readThread_ = std::thread([this] { readLoop(); });
}

void USBDeviceConnection::stop() {
    reading_ = false;
    if (readThread_.joinable()) readThread_.join();
}

void USBDeviceConnection::send(const uint8_t* data, size_t len) {
    if (!handle_ || !outEndpoint_) return;

    int transferred = 0;
    int rc = libusb_bulk_transfer(handle_.get(), outEndpoint_,
                                  const_cast<uint8_t*>(data), static_cast<int>(len),
                                  &transferred, 10000);
    if (rc != 0 && rc != LIBUSB_ERROR_TIMEOUT) {
        DM_LOG(error) << "USBDeviceConnection: send failed: "
                      << libusb_strerror(static_cast<libusb_error>(rc));
        if (errorCallback_) errorCallback_(libusb_strerror(static_cast<libusb_error>(rc)));
    }
}

void USBDeviceConnection::readLoop() {
    uint8_t buffer[16384];
    while (reading_) {
        int transferred = 0;
        int rc = libusb_bulk_transfer(handle_.get(), inEndpoint_,
                                      buffer, sizeof(buffer),
                                      &transferred, 1000);
        if (!reading_) break;
        if (rc == LIBUSB_ERROR_TIMEOUT) continue;
        if (rc != 0) {
            DM_LOG(error) << "USBDeviceConnection: read error: "
                          << libusb_strerror(static_cast<libusb_error>(rc));
            if (errorCallback_) errorCallback_(libusb_strerror(static_cast<libusb_error>(rc)));
            break;
        }
        if (transferred > 0 && readCallback_) {
            readCallback_(buffer, static_cast<size_t>(transferred));
        }
    }
}
