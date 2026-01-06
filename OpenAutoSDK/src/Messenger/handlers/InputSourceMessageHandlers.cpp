#include <Messenger/InputSourceMessageHandlers.hpp>

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
#include <sstream>
#include <string>
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

namespace {

std::pair<std::uint32_t, std::uint32_t> resolveTouchscreenResolution() {
  constexpr const char* kConfigPath = "configuration/ServiceDiscoveryResponse.textproto";
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  if (std::ifstream file(kConfigPath); file.good()) {
    std::string line;
    bool inTouchscreen = false;
    while (std::getline(file, line)) {
      if (!inTouchscreen && line.find("touchscreen") != std::string::npos) {
        inTouchscreen = true;
      }

      if (!inTouchscreen) {
        continue;
      }

      if (line.find("width:") != std::string::npos) {
        std::istringstream iss(line.substr(line.find("width:") + 6));
        iss >> width;
      }

      if (line.find("height:") != std::string::npos) {
        std::istringstream iss(line.substr(line.find("height:") + 7));
        iss >> height;
      }

      if (width != 0 && height != 0) {
        break;
      }
    }
  }

  return {width, height};
}

}

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

  std::uint32_t kFallbackTouchWidth = touchWidth_;
  std::uint32_t kFallbackTouchHeight = touchHeight_;
  const auto [width, height] = resolveTouchscreenResolution();
  if (width == 0 || height == 0) {
    AASDK_LOG(error) << "[InputSourceMessageHandlers] Failed to resolve touchscreen resolution; using fallback 1920x1080.";
    touchWidth_ = kFallbackTouchWidth;
    touchHeight_ = kFallbackTouchHeight;
  } else {
    touchWidth_ = width;
    touchHeight_ = height;
  }

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

  AASDK_LOG(info) << "[InputSourceMessageHandlers] Decoded TOUCH: ts=" << timestamp
                  << " norm_x=" << normX << " norm_y=" << normY
                  << " px=" << px << " py=" << py
                  << " pointer_id=" << pointerId << " action=" << action
                  << " payload=" << inputReport.ShortDebugString();

}

void InputSourceMessageHandlers::setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

}
