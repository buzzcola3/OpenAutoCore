# DeviceManager

Unified device discovery and connection manager for USB and wireless (Bluetooth/WiFi) Android Auto devices. Polled from the main loop every ~100ms.

**Files:** `include/DeviceManager/Common/DeviceManager.hpp`, `src/DeviceManager/Common/DeviceManager.cpp`

## Composition

DeviceManager wraps two sub-managers:

| Sub-manager | Scope |
|---|---|
| `USBDeviceManager` | libusb hotplug detection, AOAP handshake, USB bulk I/O |
| `WirelessDeviceManager` | BlueZ D-Bus profile, WiFi negotiation, TCP listener |

## Public API

| Method | Purpose |
|---|---|
| `start()` | Start USB + wireless sub-managers, wire internal callbacks |
| `stop()` | Stop both sub-managers |
| `pollDevices()` | Poll USB hotplug, wireless, and execute pending timers |
| `connectDevice(id)` | Initiate connection (triggers AOAP setup or WiFi projection) |
| `disconnectDevice(id)` | Tear down connection, remove from registry |
| `getDeviceListJson()` | JSON array of devices with status |

## Callbacks

```cpp
std::function<void(const std::string& deviceId,
                   DeviceConnection::Pointer connection)> onDeviceReady;

std::function<void()> onDeviceListChanged;
```

`onDeviceReady` fires when a device connection is ready for an Android Auto session. The `App` receives the `DeviceConnection` and creates a `FrameRouter` to begin the protocol.

`onDeviceListChanged` fires when devices appear/disappear/change status, so the frontend can refresh its device list.

## Device Connection Flow

**USB:**
1. Phone detected via libusb hotplug → added to device registry
2. `connectDevice()` → AOAP handshake (accessory ID strings → phone re-enumerates)
3. AOAP device opens bulk endpoints → `onDeviceReady` fires

**Wireless:**
1. Phone connects via BlueZ Bluetooth profile → added to device registry
2. `connectDevice()` → BT handshake negotiates WiFi AP details (SSID, IP, port)
3. Phone connects over TCP → `onDeviceReady` fires

## Main Loop Integration

```cpp
deviceManager.start();
while (running) {
    app->poll();
    deviceManager.pollDevices();   // ~100ms interval
    std::this_thread::sleep_for(100ms);
}
```

`pollDevices()` drains USB hotplug events, processes wireless state machines, and fires any expired internal timers.

## Device Registry

Internal `DeviceEntry` struct tracks:
- **ID** (e.g. `"usb:1:2"`, `"wireless:AA:BB:CC:DD:EE:FF"`)
- **Display name** (e.g. `"Android Auto (USB)"`)
- **Transport type** (`"usb"` or `"wireless"`)
- **Status** (`"available"`, `"connecting"`, `"connected"`)
- USB-specific: `vid`, `pid`, `bus`, `port`, `aoapReady`
- Wireless-specific: `pendingConnection`, `peerAddress`

Thread-safe via `std::mutex`.
