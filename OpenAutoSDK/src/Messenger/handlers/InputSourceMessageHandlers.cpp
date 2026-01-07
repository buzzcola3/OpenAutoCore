#include <Messenger/handlers/InputSourceMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageType.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/inputsource/InputMessageId.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyBindingRequest.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyBindingResponse.pb.h>
#include <aap_protobuf/service/inputsource/message/InputReport.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using Input = aap_protobuf::service::inputsource::InputMessageId;

template<typename Proto>
bool parsePayload(const std::uint8_t* data, std::size_t size, Proto& message, const char* label) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    AASDK_LOG(error) << "[InputSourceMessageHandlers] " << label
                     << " payload too large for ParseFromArray, bytes=" << size;
    return false;
  }

  if (!message.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << "[InputSourceMessageHandlers] Failed to parse " << label
                     << " payload, bytes=" << size;
    return false;
  }

  return true;
}

}

namespace aasdk::messenger::interceptor {

bool InputSourceMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << "[InputSourceMessageHandlers] input source payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      return handleChannelOpenRequest(message, payloadData, payloadSize);
    case Input::INPUT_MESSAGE_KEY_BINDING_REQUEST:
      return handleKeyBindingRequest(message, payloadData, payloadSize);
    default:
      AASDK_LOG(debug) << "[InputSourceMessageHandlers] message id=" << messageId.getId()
                       << " not explicitly handled.";
      return false;
  }
}

void InputSourceMessageHandlers::resolveTouchscreenResolution() const {
  constexpr const char* kConfigPath = "configuration/ServiceDiscoveryResponse.textproto";

  std::uint32_t width = 0;
  std::uint32_t height = 0;

  const auto trim = [](std::string value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
      return std::string{};
    }
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
  };

  if (std::ifstream file(kConfigPath); file.good()) {
    std::string line;
    while (std::getline(file, line)) {
      const auto pos = line.find("codec_resolution");
      if (pos == std::string::npos) {
        continue;
      }

      const auto colonPos = line.find(':', pos);
      if (colonPos == std::string::npos) {
        continue;
      }

      const auto token = trim(line.substr(colonPos + 1));

      static const std::unordered_map<std::string, std::pair<std::uint32_t, std::uint32_t>> kResolutionLookup = {
        {"VIDEO_800x480", {800, 480}},
        {"VIDEO_1280x720", {1280, 720}},
        {"VIDEO_1920x1080", {1920, 1080}},
        {"VIDEO_2560x1440", {2560, 1440}},
        {"VIDEO_3840x2160", {3840, 2160}},
        {"VIDEO_720x1280", {720, 1280}},
        {"VIDEO_1080x1920", {1080, 1920}},
        {"VIDEO_1440x2560", {1440, 2560}},
        {"VIDEO_2160x3840", {2160, 3840}},
      };

      const auto it = kResolutionLookup.find(token);
      if (it != kResolutionLookup.end()) {
        width = it->second.first;
        height = it->second.second;
        break;
      }

      AASDK_LOG(error) << "[InputSourceMessageHandlers] Unknown codec_resolution value '" << token
                       << "'; using fallback " << touchWidth_ << "x" << touchHeight_ << ".";
      break;
    }
  }

  if (width == 0 || height == 0) {
    AASDK_LOG(error) << "[InputSourceMessageHandlers] Failed to resolve touchscreen resolution; using fallback "
                     << touchWidth_ << "x" << touchHeight_ << ".";
    return;
  }

  touchWidth_ = width;
  touchHeight_ = height;
  AASDK_LOG(debug) << "[InputSourceMessageHandlers] Using codec resolution for touchscreen scaling: "
                   << touchWidth_ << "x" << touchHeight_ << ".";
}

bool InputSourceMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                          const std::uint8_t* data,
                                                          std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!parsePayload(data, size, request, "ChannelOpenRequest")) {
    return false;
  }

  AASDK_LOG(debug) << "[InputSourceMessageHandlers] ChannelOpenRequest: "
                   << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  touchChannelId_ = message.getChannelId();
  touchEncryptionType_ = message.getEncryptionType();
  resolveTouchscreenResolution();

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::CONTROL,
                          Control::MESSAGE_CHANNEL_OPEN_RESPONSE,
                          response);
    return true;
  }

  AASDK_LOG(error) << "[InputSourceMessageHandlers] MessageSender not configured; cannot send channel open response.";
  return false;
}

bool InputSourceMessageHandlers::handleKeyBindingRequest(const ::aasdk::messenger::Message& message,
                                                         const std::uint8_t* data,
                                                         std::size_t size) const {
  aap_protobuf::service::media::sink::message::KeyBindingRequest request;
  if (!parsePayload(data, size, request, "KeyBindingRequest")) {
    return false;
  }

  AASDK_LOG(debug) << "[InputSourceMessageHandlers] KeyBindingRequest: "
                   << request.ShortDebugString();

  aap_protobuf::service::media::sink::message::KeyBindingResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::SPECIFIC,
                          Input::INPUT_MESSAGE_KEY_BINDING_RESPONSE,
                          response);
    return true;
  }

  AASDK_LOG(error) << "[InputSourceMessageHandlers] MessageSender not configured; cannot send key binding response.";
  return false;
}

void InputSourceMessageHandlers::onTouchEvent(std::uint64_t timestamp,
                                              const void* data,
                                              std::size_t size) const {
  constexpr std::size_t kExpectedSize = sizeof(float) * 2 + sizeof(std::uint32_t) * 2; // 16 bytes

  if (data == nullptr) {
    AASDK_LOG(error) << "[InputSourceMessageHandlers] TOUCH payload nullptr";
    return;
  }

  if (size != kExpectedSize) {
    AASDK_LOG(error) << "[InputSourceMessageHandlers] TOUCH payload size mismatch, expected="
                     << kExpectedSize << " bytes got=" << size;
    return;
  }

  float x = 0.0f;
  float y = 0.0f;
  std::uint32_t pointerId = 0;
  std::uint32_t action = 0;

  const auto* bytes = static_cast<const std::uint8_t*>(data);
  std::memcpy(&x, bytes, sizeof(float));
  std::memcpy(&y, bytes + sizeof(float), sizeof(float));
  std::memcpy(&pointerId, bytes + sizeof(float) * 2, sizeof(std::uint32_t));
  std::memcpy(&action, bytes + sizeof(float) * 2 + sizeof(std::uint32_t), sizeof(std::uint32_t));

  const std::uint32_t touchWidth = touchWidth_;
  const std::uint32_t touchHeight = touchHeight_;

  const auto clamp01 = [](float v) {
    if (std::isnan(v)) {
      return 0.0f;
    }
    if (v < 0.0f) {
      return 0.0f;
    }
    if (v > 1.0f) {
      return 1.0f;
    }
    return v;
  };

  const float normX = clamp01(x);
  const float normY = clamp01(y);

  const auto toPixel = [](float norm, std::uint32_t dim) {
    const float scaled = norm * static_cast<float>(dim - 1);
    const auto rounded = static_cast<std::int64_t>(std::lround(scaled));
    return static_cast<std::uint32_t>(std::max<std::int64_t>(0, std::min<std::int64_t>(rounded, dim - 1)));
  };

  const std::uint32_t px = toPixel(normX, touchWidth);
  const std::uint32_t py = toPixel(normY, touchHeight);

  aap_protobuf::service::inputsource::message::InputReport inputReport;
  inputReport.set_timestamp(timestamp);

  auto touchEvent = inputReport.mutable_touch_event();
  touchEvent->set_action(static_cast<aap_protobuf::service::inputsource::message::PointerAction>(action));
  auto touchLocation = touchEvent->add_pointer_data();
  touchLocation->set_x(px);
  touchLocation->set_y(py);
  touchLocation->set_pointer_id(pointerId);

  if (sender_ != nullptr && touchChannelId_ != ::aasdk::messenger::ChannelId::NONE) {
    sender_->sendProtobuf(touchChannelId_,
                          touchEncryptionType_,
                          ::aasdk::messenger::MessageType::SPECIFIC,
                          Input::INPUT_MESSAGE_INPUT_REPORT,
                          inputReport);
  } else {
    AASDK_LOG(error) << "[InputSourceMessageHandlers] Cannot send touch InputReport: sender or channel unavailable.";
  }
}

void InputSourceMessageHandlers::setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

}
