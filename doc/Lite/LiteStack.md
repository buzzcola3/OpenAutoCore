# Lite Messaging Stack

A simplified, single-threaded, callback-based replacement for the OpenAutoSDK messaging layers (`Transport` → `MessageInStream` → `Messenger` → `Channel`).

## Motivation

The existing stack uses 6 layers, 5+ strands, and ~6 heap-allocated promise objects per inbound message. The Lite stack replaces this with 3 layers, zero strands, and zero allocations per message in steady state.

| | Old stack | Lite stack |
|---|---|---|
| **Layers** | USBTransport → Transport → MessageInStream → Messenger → Channel → *ServiceChannel | USBDevice → FrameIO → ChannelRouter |
| **Threading** | 5+ strands, multi-threaded dispatch | Single thread, blocking pump |
| **Async model** | Custom `io::Promise<T>` + `PromiseLink` | Direct callbacks |
| **Per-message allocs** | ~6 shared_ptr<Promise> | 0 (steady state) |
| **Dependencies** | boost::asio, io_service | libusb only |

## Architecture

```
┌─────────────────────────────────┐
│         Session                 │  Owns everything, runs pump
├──────────┬──────────────────────┤
│ FrameIO  │   ChannelRouter     │  Frame I/O + dispatch table
├──────────┘                      │
│         USBDevice               │  Synchronous USB bulk I/O
└─────────────────────────────────┘
```

## Classes

### USBDevice

Thin synchronous wrapper around libusb bulk transfers on an AOAP device.

- `read(buf, size)` — blocking exact-size read from IN endpoint
- `write(buf, size)` — blocking write to OUT endpoint (handles partial writes)
- `close()` — release the device

Calls `libusb_bulk_transfer()` directly, bypassing the async USBEndpoint/Transport layers entirely.

### FrameIO

Reads and writes Android Auto framed messages. Handles:

- **Frame header** parsing (2 bytes: channelId, frameType, encryptionType, messageType)
- **Frame size** parsing (2 bytes SHORT, 6 bytes EXTENDED for FIRST frames)
- **Multi-frame reassembly** with per-channel partial buffers (FIRST → MIDDLE… → LAST)
- **Encryption/decryption** via the existing `ICryptor` interface
- **Fragmentation** on send for payloads > 16 KB

Wire format per frame:
```
[FrameHeader 2B][FrameSize 2B or 6B][Payload NB]
```

### ChannelRouter

Callback dispatch table: `std::array<Handler, 256>` keyed by `ChannelId`.

- `on(channelId, handler)` — register a callback
- `dispatch(channelId, data, size)` — invoke the callback

Handlers receive the complete decrypted payload including the 2-byte MessageId prefix. No queues, no promises — called inline during the message pump.

### Session

Owns `USBDevice`, `FrameIO`, `ChannelRouter`, and `ICryptor`. Provides:

- `router()` — register channel handlers
- `io()` — send messages from within handlers
- `run()` — blocking message pump: `while(running) { readMessage → dispatch }`
- `stop()` — signal the pump to exit (thread-safe via `atomic<bool>`)

The SSL handshake is performed during the initial CONTROL channel messages before entering steady-state dispatch.

## Wire Protocol Reference

### Frame Header (2 bytes)

```
Byte 0: [channelId:8]
Byte 1: [frameType:2][encryptionType:1][messageType:1][unused:4]
```

| Field | Bits | Values |
|-------|------|--------|
| channelId | byte 0, all | 0=CONTROL, 1=SENSOR, … 255=NONE |
| frameType | byte 1, bits 0-1 | 0=MIDDLE, 1=FIRST, 2=LAST, 3=BULK |
| encryptionType | byte 1, bit 3 | 0=PLAIN, 8=ENCRYPTED |
| messageType | byte 1, bit 2 | 0=SPECIFIC, 4=CONTROL |

### Frame Size

- **SHORT** (MIDDLE, LAST, BULK frames): 2 bytes, big-endian uint16 = this frame's payload size
- **EXTENDED** (FIRST frames only): 2 bytes frame size + 4 bytes big-endian uint32 total message size

### Message Payload

After decryption, the payload starts with a 2-byte big-endian MessageId, followed by the protobuf-serialized message body.

```
[MessageId 2B][Protobuf payload NB]
```

### Max Frame Payload

16 KB (0x4000). Messages larger than this are fragmented into FIRST → MIDDLE… → LAST frames.

## Encryption

Uses the existing `ICryptor` (OpenSSL BIO pair). Encrypt/decrypt are synchronous transforms:

- **Encrypt**: plaintext → `sslWrite()` → encrypted bytes from write-BIO
- **Decrypt**: ciphertext → write to read-BIO → `sslRead()` → plaintext. Overhead constant = 29 bytes.

The handshake is multi-step:
1. `cryptor->init()` — create SSL context
2. `cryptor->doHandshake()` — returns false if WANT_READ, true when done
3. `cryptor->readHandshakeBuffer()` — get outbound handshake data
4. `cryptor->writeHandshakeBuffer()` — feed inbound handshake data

## Usage

```cpp
auto cryptor = std::make_shared<Cryptor>(sslWrapper);
cryptor->init();

lite::Session session(std::move(aoapDevice), cryptor);

session.router().on(ChannelId::CONTROL, [&](const uint8_t* data, size_t size) {
    uint16_t msgId = (data[0] << 8) | data[1];
    switch (msgId) {
        case MESSAGE_SERVICE_DISCOVERY_REQUEST:
            // handle...
            break;
    }
});

session.router().on(ChannelId::INPUT_SOURCE, [&](const uint8_t* data, size_t size) {
    // handle input events...
});

session.run(); // blocks until disconnect or stop()
```
