#include <Lite/PhoneStatusHandler.hpp>
#include <Lite/FrameIO.hpp>

#include <Messenger/MessageType.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/phonestatus/PhoneStatusMessageId.pb.h>
#include <aap_protobuf/service/phonestatus/message/PhoneStatus.pb.h>
#include <aap_protobuf/service/phonestatus/message/PhoneStatusInput.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <limits>

namespace {
using Control = aap_protobuf::service::control::message::ControlMessageType;
using PhoneStatus = aap_protobuf::service::phonestatus::PhoneStatusMessageId;
constexpr const char* kTag = "[LitePhoneStatus]";
}

namespace aasdk::lite {

PhoneStatusHandler::PhoneStatusHandler(SendFn sender)
    : send_(std::move(sender)) {}

void PhoneStatusHandler::operator()(const InMessage& msg) {
    ++messageCount_;
    const auto& payload = msg.payload;
    if (payload.size() < 2) {
        AASDK_LOG(error) << kTag << " payload too small";
        return;
    }
    uint16_t msgId = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
    const uint8_t* data = payload.data() + 2;
    size_t size = payload.size() - 2;

    switch (msgId) {
        case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
            handleChannelOpenRequest(msg, data, size);
            break;
        case PhoneStatus::PHONE_STATUS: {
            aap_protobuf::service::phonestatus::message::PhoneStatus status;
            if (status.ParseFromArray(data, static_cast<int>(size)))
                AASDK_LOG(debug) << kTag << " PhoneStatus: " << status.ShortDebugString();
            break;
        }
        case PhoneStatus::PHONE_STATUS_INPUT: {
            aap_protobuf::service::phonestatus::message::PhoneStatusInput input;
            if (input.ParseFromArray(data, static_cast<int>(size)))
                AASDK_LOG(debug) << kTag << " PhoneStatusInput: " << input.ShortDebugString();
            break;
        }
        default:
            AASDK_LOG(debug) << kTag << " unhandled msgId=" << msgId;
            break;
    }
}

void PhoneStatusHandler::handleChannelOpenRequest(const InMessage& msg,
                                                  const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::ChannelOpenRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << kTag << " Failed to parse ChannelOpenRequest";
        return;
    }
    AASDK_LOG(debug) << kTag << " ChannelOpenRequest: " << request.ShortDebugString();

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::CONTROL,
              Control::MESSAGE_CHANNEL_OPEN_RESPONSE, response);
}

void PhoneStatusHandler::sendProto(messenger::ChannelId ch,
                                   messenger::EncryptionType enc,
                                   messenger::MessageType mt,
                                   uint16_t messageId,
                                   const google::protobuf::MessageLite& proto) {
    size_t protoSize = proto.ByteSizeLong();
    std::vector<uint8_t> buf(2 + protoSize);
    buf[0] = static_cast<uint8_t>(messageId >> 8);
    buf[1] = static_cast<uint8_t>(messageId & 0xFF);
    proto.SerializeToArray(buf.data() + 2, static_cast<int>(protoSize));
    send_(ch, enc, mt, buf.data(), buf.size());
}

} // namespace aasdk::lite
