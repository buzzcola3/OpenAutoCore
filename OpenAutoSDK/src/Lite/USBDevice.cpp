#include <Lite/USBDevice.hpp>
#include <Error/Error.hpp>
#include <libusb.h>

namespace aasdk::lite {

USBDevice::USBDevice(usb::DeviceHandle handle,
                     uint8_t inEndpointAddress,
                     uint8_t outEndpointAddress)
    : handle_(std::move(handle))
    , inAddr_(inEndpointAddress)
    , outAddr_(outEndpointAddress)
    , open_(true) {}

USBDevice USBDevice::create(usb::IUSBWrapper& usbWrapper, usb::DeviceHandle handle) {
    libusb_config_descriptor* rawDesc = nullptr;
    libusb_device* dev = libusb_get_device(handle.get());
    int rc = libusb_get_config_descriptor(dev, 0, &rawDesc);
    if (rc != 0 || rawDesc == nullptr) {
        throw error::Error(error::ErrorCode::USB_OBTAIN_CONFIG_DESCRIPTOR, rc);
    }
    // RAII for the descriptor
    auto descGuard = std::shared_ptr<libusb_config_descriptor>(rawDesc, libusb_free_config_descriptor);

    if (rawDesc->bNumInterfaces == 0) {
        throw error::Error(error::ErrorCode::USB_EMPTY_INTERFACES);
    }
    const libusb_interface& iface = rawDesc->interface[0];
    if (iface.num_altsetting == 0) {
        throw error::Error(error::ErrorCode::USB_EMPTY_INTERFACES);
    }
    const libusb_interface_descriptor& ifaceDesc = iface.altsetting[0];
    if (ifaceDesc.bNumEndpoints < 2) {
        throw error::Error(error::ErrorCode::USB_INVALID_DEVICE_ENDPOINTS);
    }

    int ifaceNum = ifaceDesc.bInterfaceNumber;
    rc = libusb_claim_interface(handle.get(), ifaceNum);
    if (rc != 0) {
        throw error::Error(error::ErrorCode::USB_CLAIM_INTERFACE, rc);
    }

    uint8_t inAddr = 0, outAddr = 0;
    for (int i = 0; i < ifaceDesc.bNumEndpoints; ++i) {
        uint8_t addr = ifaceDesc.endpoint[i].bEndpointAddress;
        if ((addr & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
            inAddr = addr;
        } else {
            outAddr = addr;
        }
    }

    USBDevice device(std::move(handle), inAddr, outAddr);
    device.interfaceNum_ = ifaceNum;
    return device;
}

bool USBDevice::read(uint8_t* buf, size_t size, unsigned int timeoutMs) {
    if (!open_) return false;

    size_t totalRead = 0;
    while (totalRead < size) {
        int actual = 0;
        int rc = libusb_bulk_transfer(
            handle_.get(), inAddr_,
            buf + totalRead,
            static_cast<int>(size - totalRead),
            &actual, timeoutMs);
        if (rc != 0 && rc != LIBUSB_ERROR_TIMEOUT) {
            return false;
        }
        if (actual > 0) {
            totalRead += static_cast<size_t>(actual);
        }
        if (rc == LIBUSB_ERROR_TIMEOUT && actual == 0) {
            // No data and timed out with infinite timeout shouldn't happen,
            // but with a real timeout it means nothing arrived.
            if (timeoutMs != 0) return false;
        }
    }
    return true;
}

bool USBDevice::write(const uint8_t* buf, size_t size, unsigned int timeoutMs) {
    if (!open_) return false;

    size_t totalWritten = 0;
    while (totalWritten < size) {
        int actual = 0;
        int rc = libusb_bulk_transfer(
            handle_.get(), outAddr_,
            const_cast<uint8_t*>(buf + totalWritten),
            static_cast<int>(size - totalWritten),
            &actual, timeoutMs);
        if (rc != 0 && rc != LIBUSB_ERROR_TIMEOUT) {
            return false;
        }
        if (actual > 0) {
            totalWritten += static_cast<size_t>(actual);
        }
        if (rc == LIBUSB_ERROR_TIMEOUT && actual == 0) {
            return false;
        }
    }
    return true;
}

void USBDevice::close() {
    if (!open_) return;
    open_ = false;
    if (interfaceNum_ >= 0 && handle_) {
        libusb_release_interface(handle_.get(), interfaceNum_);
    }
}

bool USBDevice::isOpen() const {
    return open_;
}

libusb_device_handle* USBDevice::rawHandle() const {
    return handle_.get();
}

} // namespace aasdk::lite
