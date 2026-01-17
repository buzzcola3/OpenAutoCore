#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <Messenger/ChannelId.hpp>
#include <Messenger/EncryptionType.hpp>

namespace aasdk::messenger {
  class Message;
  class MessageSender;
}

namespace aasdk::messenger::interceptor {

class SensorMessageHandlers {
public:
  bool handle(const ::aasdk::messenger::Message& message) const;
  void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);

  void onSensorEvent(std::uint64_t timestamp,
                     const void* data,
                     std::size_t size) const;

private:
  bool handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                const std::uint8_t* data,
                                std::size_t size) const;

  bool handleSensorStartRequest(const ::aasdk::messenger::Message& message,
                                const std::uint8_t* data,
                                std::size_t size) const;

  bool handleSensorStopRequest(const ::aasdk::messenger::Message& message,
                               const std::uint8_t* data,
                               std::size_t size) const;

  bool sendDrivingStatusIndication(const ::aasdk::messenger::Message& message) const;
  bool sendNightModeIndication(const ::aasdk::messenger::Message& message) const;
  bool sendLocationIndication(const nlohmann::json& location) const;

  mutable std::uint64_t messageCount_{0};
  mutable ::aasdk::messenger::ChannelId sensorChannelId_{::aasdk::messenger::ChannelId::NONE};
  mutable ::aasdk::messenger::EncryptionType sensorEncryptionType_{::aasdk::messenger::EncryptionType::PLAIN};
  std::shared_ptr<::aasdk::messenger::MessageSender> sender_;
};

}
