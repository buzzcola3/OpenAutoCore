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

#include <cstring>
#include <DeviceManager/USBDevice.hpp>
#include <DeviceManager/DmLog.hpp>

static constexpr unsigned int kSendTimeoutMs  = 10000;
static constexpr unsigned int kRecvTimeoutMs  = 0; // no timeout for receive


USBDevice::USBDevice(DeviceInfo info, libusb_device_handle* handle,
                     uint8_t inEndpoint, uint8_t outEndpoint, int interfaceNumber)
    : info_(std::move(info))
    , handle_(handle)
    , inEndpoint_(inEndpoint)
    , outEndpoint_(outEndpoint)
    , interfaceNumber_(interfaceNumber) {
}

USBDevice::~USBDevice() {
    stop();
    if (handle_) {
        libusb_release_interface(handle_, interfaceNumber_);
        libusb_close(handle_);
        handle_ = nullptr;
    }
}

std::unique_ptr<USBDevice> USBDevice::create(DeviceInfo info, libusb_device* device) {
    libusb_device_handle* handle = nullptr;
    int rc = libusb_open(device, &handle);
    if (rc != LIBUSB_SUCCESS) {
        DM_LOG(warning) << "USBDevice: failed to open device: " << libusb_strerror(static_cast<libusb_error>(rc));
        return nullptr;
    }

    libusb_config_descriptor* config = nullptr;
    rc = libusb_get_config_descriptor(device, 0, &config);
    if (rc != LIBUSB_SUCCESS) {
        libusb_close(handle);
        return nullptr;
    }

    // Find first interface with at least 2 endpoints
    const libusb_interface_descriptor* iface = nullptr;
    for (int i = 0; i < config->bNumInterfaces; i++) {
        auto& intf = config->interface[i];
        if (intf.num_altsetting > 0 && intf.altsetting[0].bNumEndpoints >= 2) {
            iface = &intf.altsetting[0];
            break;
        }
    }

    if (!iface) {
        DM_LOG(warning) << "USBDevice: no suitable interface found";
        libusb_free_config_descriptor(config);
        libusb_close(handle);
        return nullptr;
    }

    // Determine IN and OUT endpoint addresses
    uint8_t inEp, outEp;
    if ((iface->endpoint[0].bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
        inEp  = iface->endpoint[0].bEndpointAddress;
        outEp = iface->endpoint[1].bEndpointAddress;
    } else {
        inEp  = iface->endpoint[1].bEndpointAddress;
        outEp = iface->endpoint[0].bEndpointAddress;
    }

    int ifaceNum = iface->bInterfaceNumber;
    libusb_free_config_descriptor(config);

    rc = libusb_claim_interface(handle, ifaceNum);
    if (rc != LIBUSB_SUCCESS) {
        DM_LOG(warning) << "USBDevice: failed to claim interface: " << libusb_strerror(static_cast<libusb_error>(rc));
        libusb_close(handle);
        return nullptr;
    }

    DM_LOG(info) << "USBDevice: opened " << info.id
              << " inEp=0x" << std::hex << (int)inEp
              << " outEp=0x" << (int)outEp << std::dec;

    return std::make_unique<USBDevice>(std::move(info), handle, inEp, outEp, ifaceNum);
}

// ── Send ──

void USBDevice::send(const uint8_t* data, size_t size,
                     SendHandler onComplete, ErrorHandler onError) {
    sendBuffer_.assign(data, data + size);
    sendOffset_ = 0;
    pendingSendComplete_ = std::move(onComplete);
    pendingSendError_ = std::move(onError);
    submitSend();
}

void USBDevice::submitSend() {
    sendTransfer_ = libusb_alloc_transfer(0);
    if (!sendTransfer_) {
        auto handler = std::move(pendingSendError_);
        pendingSendComplete_ = nullptr;
        if (handler) handler("Failed to allocate USB transfer");
        return;
    }

    libusb_fill_bulk_transfer(sendTransfer_, handle_, outEndpoint_,
                              sendBuffer_.data() + sendOffset_,
                              static_cast<int>(sendBuffer_.size() - sendOffset_),
                              onSendComplete, this, kSendTimeoutMs);

    int rc = libusb_submit_transfer(sendTransfer_);
    if (rc != LIBUSB_SUCCESS) {
        libusb_free_transfer(sendTransfer_);
        sendTransfer_ = nullptr;
        auto handler = std::move(pendingSendError_);
        pendingSendComplete_ = nullptr;
        if (handler) handler("USB send submit failed: " +
                             std::string(libusb_strerror(static_cast<libusb_error>(rc))));
    }
}

void USBDevice::onSendComplete(libusb_transfer* transfer) {
    auto* self = static_cast<USBDevice*>(transfer->user_data);
    auto status = transfer->status;
    auto actual = static_cast<size_t>(transfer->actual_length);
    libusb_free_transfer(transfer);
    self->sendTransfer_ = nullptr;

    if (status == LIBUSB_TRANSFER_COMPLETED) {
        self->sendOffset_ += actual;
        if (self->sendOffset_ < self->sendBuffer_.size()) {
            self->submitSend(); // partial send, continue
        } else {
            self->sendBuffer_.clear();
            auto handler = std::move(self->pendingSendComplete_);
            self->pendingSendError_ = nullptr;
            if (handler) handler();
        }
    } else if (status == LIBUSB_TRANSFER_CANCELLED) {
        // Silently dropped (stop() was called)
    } else {
        self->sendBuffer_.clear();
        auto handler = std::move(self->pendingSendError_);
        self->pendingSendComplete_ = nullptr;
        if (handler) handler("USB send failed: status=" + std::to_string(status));
    }
}

// ── Receive ──

void USBDevice::receive(uint8_t* buffer, size_t maxSize,
                        ReceiveHandler onData, ErrorHandler onError) {
    pendingRecvData_ = std::move(onData);
    pendingRecvError_ = std::move(onError);

    recvTransfer_ = libusb_alloc_transfer(0);
    if (!recvTransfer_) {
        auto handler = std::move(pendingRecvError_);
        pendingRecvData_ = nullptr;
        if (handler) handler("Failed to allocate USB transfer");
        return;
    }

    libusb_fill_bulk_transfer(recvTransfer_, handle_, inEndpoint_,
                              buffer, static_cast<int>(maxSize),
                              onReceiveComplete, this, kRecvTimeoutMs);

    int rc = libusb_submit_transfer(recvTransfer_);
    if (rc != LIBUSB_SUCCESS) {
        libusb_free_transfer(recvTransfer_);
        recvTransfer_ = nullptr;
        auto handler = std::move(pendingRecvError_);
        pendingRecvData_ = nullptr;
        if (handler) handler("USB receive submit failed: " +
                             std::string(libusb_strerror(static_cast<libusb_error>(rc))));
    }
}

void USBDevice::onReceiveComplete(libusb_transfer* transfer) {
    auto* self = static_cast<USBDevice*>(transfer->user_data);
    auto status = transfer->status;
    auto actual = static_cast<size_t>(transfer->actual_length);
    libusb_free_transfer(transfer);
    self->recvTransfer_ = nullptr;

    if (status == LIBUSB_TRANSFER_COMPLETED) {
        auto handler = std::move(self->pendingRecvData_);
        self->pendingRecvError_ = nullptr;
        if (handler) handler(actual);
    } else if (status == LIBUSB_TRANSFER_CANCELLED) {
        // Silently dropped (stop() was called)
    } else {
        auto handler = std::move(self->pendingRecvError_);
        self->pendingRecvData_ = nullptr;
        if (handler) handler("USB receive failed: status=" + std::to_string(status));
    }
}

// ── Stop ──

void USBDevice::stop() {
    pendingSendComplete_ = nullptr;
    pendingSendError_ = nullptr;
    pendingRecvData_ = nullptr;
    pendingRecvError_ = nullptr;
    sendBuffer_.clear();

    if (sendTransfer_) libusb_cancel_transfer(sendTransfer_);
    if (recvTransfer_) libusb_cancel_transfer(recvTransfer_);
}
