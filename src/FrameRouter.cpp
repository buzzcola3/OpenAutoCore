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

#include <FrameRouter.hpp>
#include <Messenger/MessageInStreamInterceptor.hpp>
#include <Common/Log.hpp>
#include <boost/endian/conversion.hpp>
#include <cstring>

namespace aasdk {

using messenger::ChannelId;
using messenger::EncryptionType;
using messenger::FrameType;
using messenger::MessageType;

FrameRouter::FrameRouter(DeviceConnection::Pointer connection,
                         messenger::ICryptor::Pointer cryptor)
    : connection_(std::move(connection))
    , cryptor_(std::move(cryptor)) {}

FrameRouter::~FrameRouter() {
    stop();
}

void FrameRouter::start() {
    connection_->setReadCallback([this](const uint8_t* data, size_t len) {
        onData(data, len);
    });

    connection_->setErrorCallback([this](const std::string& error) {
        AASDK_LOG(error) << "[FrameRouter] Connection error: " << error;
        stopped_ = true;
    });

    connection_->start();
}

void FrameRouter::stop() {
    stopped_ = true;
    connection_->stop();
    partials_.clear();
    inBuffer_.clear();
}

void FrameRouter::onData(const uint8_t* data, size_t len) {
    if (stopped_) return;
    inBuffer_.insert(inBuffer_.end(), data, data + len);
    processBuffer();
}

void FrameRouter::processBuffer() {
    while (!stopped_) {
        // Need at least 2 bytes for the frame header
        if (inBuffer_.size() < 2) return;

        // Peek at header to determine frame type → size field length
        ChannelId ch;
        FrameType ft;
        EncryptionType enc;
        MessageType mt;
        lite::FrameIO::decodeFrameHeader(inBuffer_.data(), ch, ft, enc, mt);

        size_t sizeFieldLen = (ft == FrameType::FIRST) ? 6 : 2;

        // Need header + size field
        if (inBuffer_.size() < 2 + sizeFieldLen) return;

        uint32_t totalSize = 0;
        uint16_t frameSize = lite::FrameIO::decodeFrameSize(
            inBuffer_.data() + 2, ft, totalSize);

        // Need header + size field + payload
        size_t fullFrameLen = 2 + sizeFieldLen + frameSize;
        if (inBuffer_.size() < fullFrameLen) return;

        // Extract frame payload
        const uint8_t* payloadPtr = inBuffer_.data() + 2 + sizeFieldLen;

        int chKey = static_cast<int>(ch);

        if (ft == FrameType::BULK) {
            // Single-frame message — complete
            partials_.erase(chKey);

            lite::InMessage msg;
            msg.channelId = ch;
            msg.encryptionType = enc;
            msg.messageType = mt;

            if (enc == EncryptionType::ENCRYPTED && cryptor_) {
                common::Data decrypted;
                common::DataConstBuffer inBuf(payloadPtr, frameSize);
                cryptor_->decrypt(decrypted, inBuf, static_cast<int>(frameSize));
                msg.payload = std::move(decrypted);
            } else {
                msg.payload.assign(payloadPtr, payloadPtr + frameSize);
            }

            // Consume the frame from the buffer before dispatching
            inBuffer_.erase(inBuffer_.begin(), inBuffer_.begin() + fullFrameLen);

            messenger::interceptor::handleMessage(msg);
            continue;
        }

        if (ft == FrameType::FIRST) {
            auto& partial = partials_[chKey];
            partial.encryptionType = enc;
            partial.messageType = mt;
            partial.payload.clear();
            partial.payload.reserve(totalSize);

            if (enc == EncryptionType::ENCRYPTED && cryptor_) {
                common::Data decrypted;
                common::DataConstBuffer inBuf(payloadPtr, frameSize);
                cryptor_->decrypt(decrypted, inBuf, static_cast<int>(frameSize));
                partial.payload.insert(partial.payload.end(), decrypted.begin(), decrypted.end());
            } else {
                partial.payload.insert(partial.payload.end(), payloadPtr, payloadPtr + frameSize);
            }

            inBuffer_.erase(inBuffer_.begin(), inBuffer_.begin() + fullFrameLen);
            continue;
        }

        if (ft == FrameType::MIDDLE || ft == FrameType::LAST) {
            auto it = partials_.find(chKey);
            if (it == partials_.end()) {
                // Orphaned continuation — skip
                inBuffer_.erase(inBuffer_.begin(), inBuffer_.begin() + fullFrameLen);
                continue;
            }
            auto& partial = it->second;

            if (enc == EncryptionType::ENCRYPTED && cryptor_) {
                common::Data decrypted;
                common::DataConstBuffer inBuf(payloadPtr, frameSize);
                cryptor_->decrypt(decrypted, inBuf, static_cast<int>(frameSize));
                partial.payload.insert(partial.payload.end(), decrypted.begin(), decrypted.end());
            } else {
                partial.payload.insert(partial.payload.end(), payloadPtr, payloadPtr + frameSize);
            }

            inBuffer_.erase(inBuffer_.begin(), inBuffer_.begin() + fullFrameLen);

            if (ft == FrameType::LAST) {
                lite::InMessage msg;
                msg.channelId = ch;
                msg.encryptionType = partial.encryptionType;
                msg.messageType = partial.messageType;
                msg.payload = std::move(partial.payload);
                partials_.erase(it);

                messenger::interceptor::handleMessage(msg);
            }
            continue;
        }

        // Unknown frame type — skip
        inBuffer_.erase(inBuffer_.begin(), inBuffer_.begin() + fullFrameLen);
    }
}

// ─── Send path ─────────────────────────────────────────────────────

lite::SendFn FrameRouter::makeSendFn() {
    return [this](ChannelId ch, EncryptionType enc, MessageType mt,
                  const uint8_t* data, size_t size) {
        send(ch, enc, mt, data, size);
    };
}

void FrameRouter::send(ChannelId channelId, EncryptionType enc,
                       MessageType msgType,
                       const uint8_t* payload, size_t size) {
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (stopped_) return;

    if (size <= kMaxFramePayload) {
        sendFrame(channelId, FrameType::BULK, enc, msgType,
                  payload, size, size);
        return;
    }

    // Fragment into FIRST / MIDDLE / LAST
    size_t offset = 0;
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

        if (!sendFrame(channelId, ft, enc, msgType,
                       payload + offset, chunkSize, size)) {
            return;
        }
        offset += chunkSize;
    }
}

bool FrameRouter::sendFrame(ChannelId ch, FrameType ft,
                            EncryptionType enc, MessageType mt,
                            const uint8_t* payload, size_t size,
                            size_t totalMessageSize) {
    size_t sizeFieldLen = (ft == FrameType::FIRST) ? 6 : 2;

    const uint8_t* sendPayload = payload;
    size_t sendSize = size;
    common::Data encrypted;

    if (enc == EncryptionType::ENCRYPTED && cryptor_) {
        common::DataConstBuffer inBuf(payload, size);
        cryptor_->encrypt(encrypted, inBuf);
        sendPayload = encrypted.data();
        sendSize = encrypted.size();
    }

    std::vector<uint8_t> frame(2 + sizeFieldLen + sendSize);

    lite::FrameIO::encodeFrameHeader(frame.data(), ch, ft, enc, mt);
    lite::FrameIO::encodeFrameSize(frame.data() + 2, ft,
                                   static_cast<uint16_t>(sendSize),
                                   static_cast<uint32_t>(totalMessageSize));
    if (sendSize > 0) {
        std::memcpy(frame.data() + 2 + sizeFieldLen, sendPayload, sendSize);
    }

    connection_->send(frame.data(), frame.size());
    return true;
}

} // namespace aasdk
