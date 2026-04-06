# DeviceConnection

Byte-stream abstraction over USB or TCP. Spawns a dedicated read thread that continuously pulls data and fires `readCallback_` into FrameRouter. Also exposes `send()` for writing bytes back. The rest of the stack doesn't care which implementation it got.

**Interface:** `include/DeviceManager/Common/DeviceConnection.hpp`

## Interface

```cpp
class DeviceConnection {
public:
    using Pointer = std::shared_ptr<DeviceConnection>;
    using ReadCallback = std::function<void(const uint8_t* data, size_t len)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    enum class Type { USB, TCP };

    virtual Type type() const = 0;
    virtual void start() = 0;       // Launch the read thread
    virtual void stop() = 0;        // Halt reading and join thread
    virtual void send(const uint8_t* data, size_t len) = 0;
    void setReadCallback(ReadCallback cb);
    void setErrorCallback(ErrorCallback cb);
};
```

## Implementations

### USBDeviceConnection

**Files:** `include/DeviceManager/Wired/USBDeviceConnection.hpp`, `src/DeviceManager/Wired/USBDeviceConnection.cpp`

Wraps a `libusb_device_handle`. Discovers bulk IN/OUT endpoints, claims the USB interface.

| Aspect | Detail |
|---|---|
| **Read** | `libusb_bulk_transfer()` in a loop, 1000ms timeout (timeout = non-fatal, retry) |
| **Write** | `libusb_bulk_transfer()` blocking, 10000ms timeout |
| **Buffer** | 16384 bytes |
| **Shutdown** | Atomic `reading_` flag → join thread → release USB interface |

### TCPDeviceConnection

**Files:** `include/DeviceManager/Wireless/TCPDeviceConnection.hpp`, `src/DeviceManager/Wireless/TCPDeviceConnection.cpp`

Wraps a raw socket file descriptor with optional ownership semantics (`releaseFd()` transfers ownership).

| Aspect | Detail |
|---|---|
| **Read** | `poll()` with 100ms timeout, then `read()`. Handles EINTR/EAGAIN. |
| **Write** | `write()` with retry loop on partial writes and EINTR |
| **Buffer** | 16384 bytes |
| **Shutdown** | `shutdown(fd_, SHUT_RDWR)` → atomic flag → join thread → close fd if owning |

## Thread Model

Both implementations spawn a `std::thread readThread_` in `start()`. The read thread loops until `reading_` is false, calling the registered `readCallback_` synchronously whenever data arrives. This means FrameRouter's `onData()` and all downstream handler dispatch runs on the connection's read thread.

`send()` is called from handler threads (via FrameRouter's `sendMutex_`) and is synchronous/blocking.
