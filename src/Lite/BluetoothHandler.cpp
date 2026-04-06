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

#include <Lite/BluetoothHandler.hpp>
#include <Lite/FrameIO.hpp>

#include <Common/MessageType.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/bluetooth/BluetoothMessageId.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingRequest.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingResponse.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothAuthenticationData.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothAuthenticationResult.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingMethod.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <limits>

namespace {
using Control = aap_protobuf::service::control::message::ControlMessageType;
using Bluetooth = aap_protobuf::service::bluetooth::BluetoothMessageId;
constexpr const char* kTag = "[LiteBluetooth]";
}

namespace aasdk::lite {

BluetoothHandler::BluetoothHandler(SendFn sender)
    : send_(std::move(sender)) {}

void BluetoothHandler::operator()(const InMessage& msg) {
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
        case Bluetooth::BLUETOOTH_MESSAGE_PAIRING_REQUEST:
            handleBluetoothPairingRequest(msg, data, size);
            break;
        case Bluetooth::BLUETOOTH_MESSAGE_AUTHENTICATION_RESULT:
            handleBluetoothAuthenticationResult(data, size);
            break;
        default:
            AASDK_LOG(debug) << kTag << " unhandled msgId=" << msgId;
            break;
    }
}

void BluetoothHandler::setIsPairedCallback(std::function<bool(const std::string&)> callback) {
    isPaired_ = std::move(callback);
}

void BluetoothHandler::handleChannelOpenRequest(const InMessage& msg,
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

void BluetoothHandler::handleBluetoothPairingRequest(const InMessage& msg,
                                                     const uint8_t* data, size_t size) {
    aap_protobuf::service::bluetooth::message::BluetoothPairingRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << kTag << " Failed to parse BluetoothPairingRequest";
        return;
    }
    AASDK_LOG(debug) << kTag << " BluetoothPairingRequest: " << request.ShortDebugString();

    bool alreadyPaired = false;
    if (isPaired_) {
        alreadyPaired = isPaired_(request.phone_address());
    }

    aap_protobuf::service::bluetooth::message::BluetoothPairingResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
    response.set_already_paired(alreadyPaired);
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Bluetooth::BLUETOOTH_MESSAGE_PAIRING_RESPONSE, response);

    aap_protobuf::service::bluetooth::message::BluetoothAuthenticationData authData;
    authData.set_auth_data("123456");
    authData.set_pairing_method(request.pairing_method());
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Bluetooth::BLUETOOTH_MESSAGE_AUTHENTICATION_DATA, authData);
}

void BluetoothHandler::handleBluetoothAuthenticationResult(const uint8_t* data, size_t size) {
    aap_protobuf::service::bluetooth::message::BluetoothAuthenticationResult result;
    if (!result.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << kTag << " Failed to parse BluetoothAuthenticationResult";
        return;
    }
    AASDK_LOG(debug) << kTag << " BluetoothAuthenticationResult: " << result.ShortDebugString();
}

void BluetoothHandler::sendProto(messenger::ChannelId ch,
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
