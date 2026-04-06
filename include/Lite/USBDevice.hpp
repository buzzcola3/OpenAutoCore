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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <libusb.h>

namespace aasdk::usb {
    using DeviceHandle = std::shared_ptr<libusb_device_handle>;
}

namespace aasdk::lite {

/// Synchronous USB bulk I/O on an AOAP device.
/// Calls libusb_bulk_transfer() directly — no async, no promises, no strands.
class USBDevice {
public:
    /// Construct from a claimed device handle and endpoint addresses.
    /// The caller must have already claimed the interface.
    USBDevice(aasdk::usb::DeviceHandle handle,
              uint8_t inEndpointAddress,
              uint8_t outEndpointAddress);

    /// Create from a raw device handle by probing its config descriptor for endpoints.
    /// Claims the interface. Throws aasdk::error::Error on failure.
    static USBDevice create(aasdk::usb::DeviceHandle handle);

    /// Blocking exact-size read. Returns false on USB error.
    bool read(uint8_t* buf, size_t size, unsigned int timeoutMs = 0);

    /// Blocking write. Handles partial writes internally.
    /// Returns false on USB error.
    bool write(const uint8_t* buf, size_t size, unsigned int timeoutMs = 10000);

    /// Release the device.
    void close();

    /// Check if the device is open.
    bool isOpen() const;

    /// Get the raw handle (for callers that need it, e.g. interface release).
    libusb_device_handle* rawHandle() const;

private:
    aasdk::usb::DeviceHandle handle_;
    uint8_t inAddr_;
    uint8_t outAddr_;
    int interfaceNum_{-1};
    bool open_{false};
};

} // namespace aasdk::lite
