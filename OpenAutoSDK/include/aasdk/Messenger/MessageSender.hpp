#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <google/protobuf/message.h>
#include <boost/asio/io_service.hpp>
#include <cstdint>

#include <Messenger/ChannelId.hpp>
#include <Messenger/EncryptionType.hpp>
#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageType.hpp>
#include <Messenger/Promise.hpp>
#include <Messenger/ICryptor.hpp>
#include <Messenger/FrameHeader.hpp>
#include <Messenger/FrameSize.hpp>
#include <Transport/ITransport.hpp>
#include <Common/Data.hpp>
#include <Error/Error.hpp>

namespace aasdk {
namespace channel {
class IChannel;
}
}

namespace aasdk::messenger {

class MessageSender {
public:
  MessageSender(boost::asio::io_service& ioService,
                transport::ITransport::Pointer transport,
                ICryptor::Pointer cryptor);

  void sendRaw(ChannelId channelId,
               EncryptionType encryptionType,
               MessageType messageType,
               uint16_t messageId,
               const common::DataConstBuffer& buffer);

  void sendProtobuf(ChannelId channelId,
                    EncryptionType encryptionType,
                    MessageType messageType,
                    uint16_t messageId,
                    const google::protobuf::Message& payload);

  void registerChannel(::aasdk::channel::IChannel& channel);
  void unregisterChannel(ChannelId channelId);
  ::aasdk::channel::IChannel* getChannel(ChannelId channelId) const;

private:
  void dispatch(Message::Pointer message);
  bool canSend() const;

  void stream(Message::Pointer message, SendPromise::Pointer promise);
  void streamSplittedMessage();
  common::Data compoundFrame(FrameType frameType, const common::DataConstBuffer& payloadBuffer);
  void setFrameSize(common::Data& data, FrameType frameType, size_t payloadSize, size_t totalSize);
  void reset();

  transport::ITransport::Pointer transport_;
  ICryptor::Pointer cryptor_;
  mutable boost::asio::io_service::strand strand_;
  Message::Pointer message_;
  size_t offset_;
  size_t remainingSize_;
  SendPromise::Pointer promise_;
  boost::asio::io_service& ioService_;
  mutable std::mutex channelMutex_;
  std::unordered_map<ChannelId, ::aasdk::channel::IChannel*> channels_;

  static constexpr size_t cMaxFramePayloadSize = 0x4000;
};

}
