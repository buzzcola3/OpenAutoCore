# FrameRouter

Protocol engine between `DeviceConnection` and the 17 channel handlers. Parses the Android Auto frame protocol, handles encryption, multi-frame reassembly, and dispatches messages by `ChannelId`.

**Files:** `include/FrameRouter/FrameRouter.hpp`, `src/FrameRouter/FrameRouter.cpp`

## Construction

```cpp
FrameRouter(DeviceConnection::Pointer connection,
            messenger::ICryptor::Pointer cryptor,
            std::shared_ptr<Transport> transport);
```

All 17 handlers are created internally — no external registration. Each handler receives a `SendFn` that calls back into `FrameRouter::send()`. Media sink handlers additionally receive the `Transport` pointer.

## Inbound Data Flow

```
DeviceConnection readCallback_(data, len)
  → FrameRouter::onData()       — append to inBuffer_
    → processBuffer()           — consume complete frames
      → decode FrameHeader (2B) — extract channelId, frameType, encryptionType, messageType
      → decode FrameSize (2B or 6B)
      → multi-frame reassembly  — FIRST starts a PartialMessage, MIDDLE appends, LAST completes
      → decrypt payload         — via ICryptor if encryptionType == ENCRYPTED
      → dispatchMessage()       — switch on channelId → handler(InMessage)
```

## Outbound Data Flow

```
handler calls send_(channelId, enc, msgType, payload, size)
  → FrameRouter::send()
    → fragment if payload > 16KB (FIRST/MIDDLE/LAST frames)
    → encrypt payload if needed
    → encode FrameHeader + FrameSize
    → connection_->send()
```

Thread safety: `sendMutex_` serializes all outbound writes.

## Wire Format

```
[FrameHeader 2B] [FrameSize 2–6B] [Payload ≤16KB]
```

### Frame Header (2 bytes)

| Field | Location | Values |
|---|---|---|
| channelId | byte 0 | 0=CONTROL, 1=SENSOR, … 255=NONE |
| frameType | byte 1, bits 0–1 | 0=MIDDLE, 1=FIRST, 2=LAST, 3=BULK |
| encryptionType | byte 1, bit 3 | 0=PLAIN, 8=ENCRYPTED |
| messageType | byte 1, bit 2 | 0=SPECIFIC, 4=CONTROL |

### Frame Size

- **SHORT** (MIDDLE, LAST, BULK): 2 bytes big-endian uint16 = this frame's payload size
- **EXTENDED** (FIRST only): 2 bytes frame size + 4 bytes big-endian uint32 total message size

### Message Payload

After decryption, starts with a 2-byte big-endian MessageId followed by the protobuf body:

```
[MessageId 2B] [Protobuf payload NB]
```

### Max Frame Payload

16 KB (0x4000). Messages larger than this are fragmented into FIRST → MIDDLE… → LAST frames.

## Encryption

Uses `ICryptor` (OpenSSL BIO pair). Encrypt/decrypt are synchronous transforms:

- **Encrypt**: plaintext → `sslWrite()` → encrypted bytes from write-BIO
- **Decrypt**: ciphertext → write to read-BIO → `sslRead()` → plaintext

The TLS handshake is multi-step, handled during initial CONTROL channel messages:
1. `cryptor->init()` — create SSL context
2. `cryptor->doHandshake()` — returns false if WANT_READ, true when done
3. `cryptor->readHandshakeBuffer()` / `writeHandshakeBuffer()` — exchange handshake data

## Handler Ownership

FrameRouter owns all 17 handlers as member variables. Dispatch is a `switch` on `ChannelId` — no virtual dispatch, no registration. Public accessors expose a few handlers that need external wiring:

```cpp
lite::ControlHandler& controlHandler();
lite::InputSourceHandler& inputSourceHandler();
lite::SensorHandler& sensorHandler();
lite::BluetoothHandler& bluetoothHandler();
lite::MediaSourceHandler& mediaSourceHandler();
```
