#include <Messenger/MessageSender.hpp>

#include <utility>

#include <boost/endian/conversion.hpp>
#include <cstring>
#include <IO/PromiseLink.hpp>
#include <Channel/IChannel.hpp>
#include <Common/Log.hpp>

namespace aasdk::messenger {

MessageSender::MessageSender(boost::asio::io_service& ioService,
                             transport::ITransport::Pointer transport,
                             ICryptor::Pointer cryptor)
  : transport_(std::move(transport))
  , cryptor_(std::move(cryptor))
  , strand_(ioService)
  , offset_(0)
  , remainingSize_(0)
  , ioService_(ioService) {
}

void MessageSender::sendRaw(ChannelId channelId,
                            EncryptionType encryptionType,
                            MessageType messageType,
                            uint16_t messageId,
                            const common::DataConstBuffer& buffer) {
  if (!this->canSend()) {
    AASDK_LOG(error) << "[MessageSender] Cannot send, no send path configured.";
    return;
  }

  auto message = std::make_shared<Message>(channelId, encryptionType, messageType);
  message->insertPayload(MessageId(messageId).getData());
  message->insertPayload(buffer);
  this->dispatch(std::move(message));
}

void MessageSender::sendProtobuf(ChannelId channelId,
                                 EncryptionType encryptionType,
                                 MessageType messageType,
                                 uint16_t messageId,
                                 const google::protobuf::Message& payload) {
  if (!this->canSend()) {
    AASDK_LOG(error) << "[MessageSender] Cannot send, no send path configured.";
    return;
  }

  auto message = std::make_shared<Message>(channelId, encryptionType, messageType);
  message->insertPayload(MessageId(messageId).getData());
  message->insertPayload(payload);
  this->dispatch(std::move(message));
}

void MessageSender::dispatch(Message::Pointer message) {
  auto promise = SendPromise::defer(ioService_);
  const auto channelLabel = channelIdToString(message->getChannelId());

  promise->then([]() {}, [channelLabel](const error::Error& e) {
    AASDK_LOG(error) << "[MessageSender] Failed to send on channel " << channelLabel << ": " << e.what();
  });

  this->stream(std::move(message), std::move(promise));
}

bool MessageSender::canSend() const {
  return transport_ != nullptr && cryptor_ != nullptr;
}

void MessageSender::stream(Message::Pointer message, SendPromise::Pointer promise) {
  strand_.dispatch([this, self = this, message = std::move(message), promise = std::move(promise)]() mutable {
    if (promise_ != nullptr) {
      promise->reject(error::Error(error::ErrorCode::OPERATION_IN_PROGRESS));
      return;
    }

    message_ = std::move(message);
    promise_ = std::move(promise);

    if (message_->getPayload().size() >= cMaxFramePayloadSize) {
      offset_ = 0;
      remainingSize_ = message_->getPayload().size();
      this->streamSplittedMessage();
    } else {
      try {
        auto data(this->compoundFrame(FrameType::BULK, common::DataConstBuffer(message_->getPayload())));

        auto transportPromise = transport::ITransport::SendPromise::defer(strand_);
        io::PromiseLink<>::forward(*transportPromise, std::move(promise_));
        transport_->send(std::move(data), std::move(transportPromise));
      }
      catch (const error::Error &e) {
        promise_->reject(e);
        promise_.reset();
      }

      this->reset();
    }
  });
}

void MessageSender::streamSplittedMessage() {
  try {
    const auto &payload = message_->getPayload();
    auto ptr = &payload[offset_];
    auto size = remainingSize_ < cMaxFramePayloadSize ? remainingSize_ : cMaxFramePayloadSize;

    FrameType frameType =
        offset_ == 0 ? FrameType::FIRST : (remainingSize_ - size > 0 ? FrameType::MIDDLE : FrameType::LAST);
    auto data(this->compoundFrame(frameType, common::DataConstBuffer(ptr, size)));

    auto transportPromise = transport::ITransport::SendPromise::defer(strand_);

    if (frameType == FrameType::LAST) {
      this->reset();
      io::PromiseLink<>::forward(*transportPromise, std::move(promise_));
    } else {
      transportPromise->then([this, size]() mutable {
                               offset_ += size;
                               remainingSize_ -= size;
                               this->streamSplittedMessage();
                             },
                             [this](const error::Error &e) mutable {
                               this->reset();
                               promise_->reject(e);
                               promise_.reset();
                             });
    }

    transport_->send(std::move(data), std::move(transportPromise));
  }
  catch (const error::Error &e) {
    this->reset();
    promise_->reject(e);
    promise_.reset();
  }
}

common::Data MessageSender::compoundFrame(FrameType frameType, const common::DataConstBuffer &payloadBuffer) {
  const FrameHeader frameHeader(message_->getChannelId(), frameType, message_->getEncryptionType(),
                                message_->getType());
  common::Data data(frameHeader.getData());
  data.resize(data.size() +
              FrameSize::getSizeOf(frameType == FrameType::FIRST ? FrameSizeType::EXTENDED : FrameSizeType::SHORT));
  size_t payloadSize = 0;

  if (message_->getEncryptionType() == EncryptionType::ENCRYPTED) {
    payloadSize = cryptor_->encrypt(data, payloadBuffer);
  } else {
    data.insert(data.end(), payloadBuffer.cdata, payloadBuffer.cdata + payloadBuffer.size);
    payloadSize = payloadBuffer.size;
  }

  this->setFrameSize(data, frameType, payloadSize, message_->getPayload().size());
  return data;
}

void MessageSender::setFrameSize(common::Data &data, FrameType frameType, size_t payloadSize, size_t totalSize) {
  const auto &frameSize =
      frameType == FrameType::FIRST ? FrameSize(payloadSize, totalSize) : FrameSize(payloadSize);
  const auto &frameSizeData = frameSize.getData();
  memcpy(&data[FrameHeader::getSizeOf()], &frameSizeData[0], frameSizeData.size());
}

void MessageSender::reset() {
  offset_ = 0;
  remainingSize_ = 0;
  message_.reset();
}

void MessageSender::registerChannel(::aasdk::channel::IChannel& channel) {
  std::lock_guard<std::mutex> lock(channelMutex_);
  channels_[channel.getId()] = &channel;
}

void MessageSender::unregisterChannel(ChannelId channelId) {
  std::lock_guard<std::mutex> lock(channelMutex_);
  channels_.erase(channelId);
}

::aasdk::channel::IChannel* MessageSender::getChannel(ChannelId channelId) const {
  std::lock_guard<std::mutex> lock(channelMutex_);
  auto iter = channels_.find(channelId);
  if (iter == channels_.end()) {
    return nullptr;
  }
  return iter->second;
}

}
