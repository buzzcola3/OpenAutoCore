#include <Lite/FrameIO.hpp>
#include <Common/Data.hpp>
#include <boost/endian/conversion.hpp>
#include <cstring>

namespace aasdk::lite {

using messenger::ChannelId;
using messenger::EncryptionType;
using messenger::FrameType;
using messenger::MessageType;

FrameIO::FrameIO(USBDevice& device, messenger::ICryptor::Pointer cryptor)
    : device_(device), cryptor_(std::move(cryptor)) {}

// ─── Static helpers ────────────────────────────────────────────────

void FrameIO::encodeFrameHeader(uint8_t* out,
                                ChannelId ch, FrameType ft,
                                EncryptionType enc, MessageType mt) {
    out[0] = static_cast<uint8_t>(ch);
    out[1] = static_cast<uint8_t>(ft)
           | static_cast<uint8_t>(enc)
           | static_cast<uint8_t>(mt);
}

void FrameIO::decodeFrameHeader(const uint8_t* in,
                                ChannelId& ch, FrameType& ft,
                                EncryptionType& enc, MessageType& mt) {
    ch  = static_cast<ChannelId>(in[0]);
    ft  = static_cast<FrameType>(in[1] & static_cast<uint8_t>(FrameType::BULK));
    enc = static_cast<EncryptionType>(in[1] & static_cast<uint8_t>(EncryptionType::ENCRYPTED));
    mt  = static_cast<MessageType>(in[1] & static_cast<uint8_t>(MessageType::CONTROL));
}

size_t FrameIO::encodeFrameSize(uint8_t* out, FrameType ft,
                                uint16_t framePayloadSize,
                                uint32_t totalMessageSize) {
    uint16_t beFps = boost::endian::native_to_big(framePayloadSize);
    std::memcpy(out, &beFps, 2);

    if (ft == FrameType::FIRST) {
        uint32_t beTms = boost::endian::native_to_big(totalMessageSize);
        std::memcpy(out + 2, &beTms, 4);
        return 6;
    }
    return 2;
}

uint16_t FrameIO::decodeFrameSize(const uint8_t* in, FrameType ft,
                                  uint32_t& totalSize) {
    uint16_t frameSize;
    std::memcpy(&frameSize, in, 2);
    frameSize = boost::endian::big_to_native(frameSize);

    if (ft == FrameType::FIRST) {
        uint32_t ts;
        std::memcpy(&ts, in + 2, 4);
        totalSize = boost::endian::big_to_native(ts);
    } else {
        totalSize = frameSize;
    }
    return frameSize;
}

// ─── Read ──────────────────────────────────────────────────────────

bool FrameIO::readFrame(ChannelId& ch, FrameType& ft,
                        EncryptionType& enc, MessageType& mt,
                        std::vector<uint8_t>& payload, uint32_t& totalSize) {
    // 1. Read frame header (2 bytes)
    uint8_t hdr[2];
    if (!device_.read(hdr, 2)) return false;
    decodeFrameHeader(hdr, ch, ft, enc, mt);

    // 2. Read frame size (2 or 6 bytes)
    size_t sizeFieldLen = (ft == FrameType::FIRST) ? 6 : 2;
    uint8_t sizeBuf[6];
    if (!device_.read(sizeBuf, sizeFieldLen)) return false;
    uint16_t frameSize = decodeFrameSize(sizeBuf, ft, totalSize);

    // 3. Read frame payload
    payload.resize(frameSize);
    if (frameSize > 0) {
        if (!device_.read(payload.data(), frameSize)) return false;
    }
    return true;
}

bool FrameIO::readMessage(InMessage& out) {
    while (true) {
        ChannelId ch;
        FrameType ft;
        EncryptionType enc;
        MessageType mt;
        std::vector<uint8_t> framePayload;
        uint32_t totalSize;

        if (!readFrame(ch, ft, enc, mt, framePayload, totalSize))
            return false;

        int chKey = static_cast<int>(ch);

        if (ft == FrameType::BULK) {
            // Single-frame message — complete immediately
            partials_.erase(chKey);
            out.channelId = ch;
            out.encryptionType = enc;
            out.messageType = mt;

            if (enc == EncryptionType::ENCRYPTED && cryptor_) {
                common::Data decrypted;
                common::DataConstBuffer inBuf(framePayload.data(), framePayload.size());
                cryptor_->decrypt(decrypted, inBuf, static_cast<int>(framePayload.size()));
                out.payload = std::move(decrypted);
            } else {
                out.payload = std::move(framePayload);
            }
            return true;
        }

        if (ft == FrameType::FIRST) {
            // Start a new partial message
            auto& partial = partials_[chKey];
            partial.encryptionType = enc;
            partial.messageType = mt;
            partial.payload.clear();
            partial.payload.reserve(totalSize);

            if (enc == EncryptionType::ENCRYPTED && cryptor_) {
                common::Data decrypted;
                common::DataConstBuffer inBuf(framePayload.data(), framePayload.size());
                cryptor_->decrypt(decrypted, inBuf, static_cast<int>(framePayload.size()));
                partial.payload.insert(partial.payload.end(), decrypted.begin(), decrypted.end());
            } else {
                partial.payload.insert(partial.payload.end(), framePayload.begin(), framePayload.end());
            }
            // Keep reading for MIDDLE/LAST
            continue;
        }

        if (ft == FrameType::MIDDLE || ft == FrameType::LAST) {
            auto it = partials_.find(chKey);
            if (it == partials_.end()) {
                // Orphaned continuation — skip
                continue;
            }
            auto& partial = it->second;

            if (enc == EncryptionType::ENCRYPTED && cryptor_) {
                common::Data decrypted;
                common::DataConstBuffer inBuf(framePayload.data(), framePayload.size());
                cryptor_->decrypt(decrypted, inBuf, static_cast<int>(framePayload.size()));
                partial.payload.insert(partial.payload.end(), decrypted.begin(), decrypted.end());
            } else {
                partial.payload.insert(partial.payload.end(), framePayload.begin(), framePayload.end());
            }

            if (ft == FrameType::LAST) {
                out.channelId = ch;
                out.encryptionType = partial.encryptionType;
                out.messageType = partial.messageType;
                out.payload = std::move(partial.payload);
                partials_.erase(it);
                return true;
            }
            // MIDDLE — keep reading
            continue;
        }
    }
}

// ─── Write ─────────────────────────────────────────────────────────

bool FrameIO::writeFrame(ChannelId ch, FrameType ft,
                         EncryptionType enc, MessageType mt,
                         const uint8_t* payload, size_t size,
                         size_t totalMessageSize) {
    // Build the frame: header(2) + size(2 or 6) + payload
    size_t sizeFieldLen = (ft == FrameType::FIRST) ? 6 : 2;

    std::vector<uint8_t> frame;

    // If encrypted, we need to encrypt the payload first
    const uint8_t* sendPayload = payload;
    size_t sendSize = size;
    common::Data encrypted;

    if (enc == EncryptionType::ENCRYPTED && cryptor_) {
        common::DataConstBuffer inBuf(payload, size);
        cryptor_->encrypt(encrypted, inBuf);
        sendPayload = encrypted.data();
        sendSize = encrypted.size();
    }

    frame.resize(2 + sizeFieldLen + sendSize);

    encodeFrameHeader(frame.data(), ch, ft, enc, mt);
    encodeFrameSize(frame.data() + 2, ft,
                    static_cast<uint16_t>(sendSize),
                    static_cast<uint32_t>(totalMessageSize));
    if (sendSize > 0) {
        std::memcpy(frame.data() + 2 + sizeFieldLen, sendPayload, sendSize);
    }

    return device_.write(frame.data(), frame.size());
}

bool FrameIO::sendMessage(ChannelId channelId, EncryptionType enc,
                          MessageType msgType,
                          const uint8_t* payload, size_t size) {
    if (size <= kMaxFramePayload) {
        // Single BULK frame
        return writeFrame(channelId, FrameType::BULK, enc, msgType,
                          payload, size, size);
    }

    // Fragment into FIRST / MIDDLE / LAST
    size_t offset = 0;
    size_t totalSize = size;

    while (offset < size) {
        size_t remaining = size - offset;
        size_t chunkSize = std::min(remaining, kMaxFramePayload);

        FrameType ft;
        if (offset == 0) {
            ft = FrameType::FIRST;
        } else if (offset + chunkSize >= size) {
            ft = FrameType::LAST;
        } else {
            ft = FrameType::MIDDLE;
        }

        if (!writeFrame(channelId, ft, enc, msgType,
                        payload + offset, chunkSize, totalSize)) {
            return false;
        }
        offset += chunkSize;
    }
    return true;
}

} // namespace aasdk::lite
