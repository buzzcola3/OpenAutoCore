#include <Lite/ControlHandler.hpp>
#include <Lite/FrameIO.hpp>
#include <Version.hpp>
#include <Common/Log.hpp>
#include <boost/endian/conversion.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>

namespace aasdk::lite {

using CMT = aap_protobuf::service::control::message::ControlMessageType;

ControlHandler::ControlHandler(SendFn sender)
    : send_(std::move(sender)) {}

void ControlHandler::operator()(const InMessage& msg) {
    if (msg.payload.size() < 2) {
        AASDK_LOG(error) << "[ControlHandler] Payload too short";
        return;
    }

    uint16_t msgId = (static_cast<uint16_t>(msg.payload[0]) << 8) | msg.payload[1];
    const uint8_t* data = msg.payload.data() + 2;
    size_t size = msg.payload.size() - 2;

    switch (msgId) {
        case CMT::MESSAGE_VERSION_RESPONSE: {
            if (size < 6) {
                AASDK_LOG(error) << "[ControlHandler] Version response too short";
                return;
            }
            const uint16_t* words = reinterpret_cast<const uint16_t*>(data);
            auto status = static_cast<aap_protobuf::shared::MessageStatus>(
                boost::endian::big_to_native(words[2]));
            AASDK_LOG(info) << "[ControlHandler] Version: " << words[0] << "." << words[1]
                            << " status=" << status;
            if (onVersionResponse) onVersionResponse(words[0], words[1], status);
            break;
        }
        case CMT::MESSAGE_ENCAPSULATED_SSL:
            AASDK_LOG(debug) << "[ControlHandler] SSL handshake data, size=" << size;
            if (onHandshake) onHandshake(common::DataConstBuffer(data, size));
            break;
        case CMT::MESSAGE_SERVICE_DISCOVERY_REQUEST: {
            aap_protobuf::service::control::message::ServiceDiscoveryRequest req;
            if (req.ParseFromArray(data, size)) {
                if (onServiceDiscoveryRequest) onServiceDiscoveryRequest(req);
            } else {
                AASDK_LOG(error) << "[ControlHandler] Failed to parse ServiceDiscoveryRequest";
            }
            break;
        }
        case CMT::MESSAGE_AUDIO_FOCUS_REQUEST: {
            aap_protobuf::service::control::message::AudioFocusRequest req;
            if (req.ParseFromArray(data, size)) {
                if (onAudioFocusRequest) onAudioFocusRequest(req);
            } else {
                AASDK_LOG(error) << "[ControlHandler] Failed to parse AudioFocusRequest";
            }
            break;
        }
        case CMT::MESSAGE_NAV_FOCUS_REQUEST: {
            aap_protobuf::service::control::message::NavFocusRequestNotification req;
            if (req.ParseFromArray(data, size)) {
                if (onNavigationFocusRequest) onNavigationFocusRequest(req);
            } else {
                AASDK_LOG(error) << "[ControlHandler] Failed to parse NavFocusRequest";
            }
            break;
        }
        case CMT::MESSAGE_BYEBYE_REQUEST: {
            aap_protobuf::service::control::message::ByeByeRequest req;
            if (req.ParseFromArray(data, size)) {
                if (onByeByeRequest) onByeByeRequest(req);
            } else {
                AASDK_LOG(error) << "[ControlHandler] Failed to parse ByeByeRequest";
            }
            break;
        }
        case CMT::MESSAGE_BYEBYE_RESPONSE: {
            aap_protobuf::service::control::message::ByeByeResponse resp;
            if (resp.ParseFromArray(data, size)) {
                if (onByeByeResponse) onByeByeResponse(resp);
            } else {
                AASDK_LOG(error) << "[ControlHandler] Failed to parse ByeByeResponse";
            }
            break;
        }
        case CMT::MESSAGE_BATTERY_STATUS_NOTIFICATION: {
            aap_protobuf::service::control::message::BatteryStatusNotification notif;
            if (notif.ParseFromArray(data, size)) {
                if (onBatteryStatusNotification) onBatteryStatusNotification(notif);
            } else {
                AASDK_LOG(error) << "[ControlHandler] Failed to parse BatteryStatusNotification";
            }
            break;
        }
        case CMT::MESSAGE_VOICE_SESSION_NOTIFICATION: {
            aap_protobuf::service::control::message::VoiceSessionNotification req;
            if (req.ParseFromArray(data, size)) {
                if (onVoiceSessionRequest) onVoiceSessionRequest(req);
            } else {
                AASDK_LOG(error) << "[ControlHandler] Failed to parse VoiceSessionNotification";
            }
            break;
        }
        case CMT::MESSAGE_PING_REQUEST: {
            aap_protobuf::service::control::message::PingRequest req;
            if (req.ParseFromArray(data, size)) {
                if (onPingRequest) onPingRequest(req);
            } else {
                AASDK_LOG(error) << "[ControlHandler] Failed to parse PingRequest";
            }
            break;
        }
        case CMT::MESSAGE_PING_RESPONSE: {
            aap_protobuf::service::control::message::PingResponse resp;
            if (resp.ParseFromArray(data, size)) {
                if (onPingResponse) onPingResponse(resp);
            } else {
                AASDK_LOG(error) << "[ControlHandler] Failed to parse PingResponse";
            }
            break;
        }
        case CMT::MESSAGE_CHANNEL_OPEN_REQUEST:
            AASDK_LOG(debug) << "[ControlHandler] ChannelOpenRequest";
            if (onChannelOpenRequest) onChannelOpenRequest(msgId);
            break;
        default:
            AASDK_LOG(warning) << "[ControlHandler] Unhandled message id: " << msgId;
            break;
    }
}

// --- Outbound sends ---

void ControlHandler::sendRaw(uint16_t messageId, messenger::EncryptionType enc,
                              const uint8_t* data, size_t size) {
    std::vector<uint8_t> buf(2 + size);
    buf[0] = static_cast<uint8_t>(messageId >> 8);
    buf[1] = static_cast<uint8_t>(messageId & 0xFF);
    if (size > 0) {
        std::memcpy(buf.data() + 2, data, size);
    }
    send_(messenger::ChannelId::CONTROL, enc,
          messenger::MessageType::SPECIFIC, buf.data(), buf.size());
}

void ControlHandler::sendProto(uint16_t messageId, messenger::EncryptionType enc,
                                const google::protobuf::MessageLite& proto) {
    std::string serialized = proto.SerializeAsString();
    sendRaw(messageId, enc,
            reinterpret_cast<const uint8_t*>(serialized.data()), serialized.size());
}

void ControlHandler::sendVersionRequest() {
    AASDK_LOG(debug) << "[ControlHandler] sendVersionRequest()";
    common::Data versionBuffer(4, 0);
    reinterpret_cast<uint16_t&>(versionBuffer[0]) = boost::endian::native_to_big(AASDK_MAJOR);
    reinterpret_cast<uint16_t&>(versionBuffer[2]) = boost::endian::native_to_big(AASDK_MINOR);
    sendRaw(CMT::MESSAGE_VERSION_REQUEST, messenger::EncryptionType::PLAIN,
            versionBuffer.data(), versionBuffer.size());
}

void ControlHandler::sendHandshake(const common::Data& buffer) {
    AASDK_LOG(debug) << "[ControlHandler] sendHandshake()";
    sendRaw(CMT::MESSAGE_ENCAPSULATED_SSL, messenger::EncryptionType::PLAIN,
            buffer.data(), buffer.size());
}

void ControlHandler::sendAuthComplete(
    const aap_protobuf::service::control::message::AuthResponse& response) {
    AASDK_LOG(debug) << "[ControlHandler] sendAuthComplete()";
    sendProto(CMT::MESSAGE_AUTH_COMPLETE, messenger::EncryptionType::PLAIN, response);
}

void ControlHandler::sendServiceDiscoveryResponse(
    const aap_protobuf::service::control::message::ServiceDiscoveryResponse& response) {
    AASDK_LOG(debug) << "[ControlHandler] sendServiceDiscoveryResponse()";
    sendProto(CMT::MESSAGE_SERVICE_DISCOVERY_RESPONSE, messenger::EncryptionType::ENCRYPTED, response);
}

void ControlHandler::sendAudioFocusResponse(
    const aap_protobuf::service::control::message::AudioFocusNotification& response) {
    AASDK_LOG(debug) << "[ControlHandler] sendAudioFocusResponse()";
    sendProto(CMT::MESSAGE_AUDIO_FOCUS_NOTIFICATION, messenger::EncryptionType::ENCRYPTED, response);
}

void ControlHandler::sendNavigationFocusResponse(
    const aap_protobuf::service::control::message::NavFocusNotification& response) {
    AASDK_LOG(debug) << "[ControlHandler] sendNavigationFocusResponse()";
    sendProto(CMT::MESSAGE_NAV_FOCUS_NOTIFICATION, messenger::EncryptionType::ENCRYPTED, response);
}

void ControlHandler::sendShutdownRequest(
    const aap_protobuf::service::control::message::ByeByeRequest& request) {
    AASDK_LOG(debug) << "[ControlHandler] sendShutdownRequest()";
    sendProto(CMT::MESSAGE_BYEBYE_REQUEST, messenger::EncryptionType::ENCRYPTED, request);
}

void ControlHandler::sendShutdownResponse(
    const aap_protobuf::service::control::message::ByeByeResponse& response) {
    AASDK_LOG(debug) << "[ControlHandler] sendShutdownResponse()";
    sendProto(CMT::MESSAGE_BYEBYE_RESPONSE, messenger::EncryptionType::ENCRYPTED, response);
}

void ControlHandler::sendPingRequest(
    const aap_protobuf::service::control::message::PingRequest& request) {
    AASDK_LOG(debug) << "[ControlHandler] sendPingRequest()";
    sendProto(CMT::MESSAGE_PING_REQUEST, messenger::EncryptionType::PLAIN, request);
}

void ControlHandler::sendPingResponse(
    const aap_protobuf::service::control::message::PingResponse& response) {
    AASDK_LOG(debug) << "[ControlHandler] sendPingResponse()";
    sendProto(CMT::MESSAGE_PING_RESPONSE, messenger::EncryptionType::PLAIN, response);
}

} // namespace aasdk::lite
