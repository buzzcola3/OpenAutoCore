#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <Messenger/ChannelId.hpp>
#include <Messenger/EncryptionType.hpp>

namespace aasdk::messenger {
  class Message;
  class MessageSender;
}

namespace aasdk::messenger::interceptor {

class RadioMessageHandlers {
public:
  bool handle(const ::aasdk::messenger::Message& message) const;
  void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);

private:
  bool handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                const std::uint8_t* data,
                                std::size_t size) const;

  bool handleActiveRadioNotification(const std::uint8_t* data,
                                     std::size_t size) const;
  bool handleSelectActiveRadioRequest(const std::uint8_t* data,
                                      std::size_t size) const;
  bool handleStepChannelRequest(const std::uint8_t* data,
                                std::size_t size) const;
  bool handleStepChannelResponse(const std::uint8_t* data,
                                 std::size_t size) const;
  bool handleSeekStationRequest(const std::uint8_t* data,
                                std::size_t size) const;
  bool handleSeekStationResponse(const std::uint8_t* data,
                                 std::size_t size) const;
  bool handleScanStationsRequest(const std::uint8_t* data,
                                 std::size_t size) const;
  bool handleScanStationsResponse(const std::uint8_t* data,
                                  std::size_t size) const;
  bool handleTuneToStationRequest(const std::uint8_t* data,
                                  std::size_t size) const;
  bool handleTuneToStationResponse(const std::uint8_t* data,
                                   std::size_t size) const;
  bool handleGetProgramListRequest(const std::uint8_t* data,
                                   std::size_t size) const;
  bool handleGetProgramListResponse(const std::uint8_t* data,
                                    std::size_t size) const;
  bool handleStationPresetsNotification(const std::uint8_t* data,
                                        std::size_t size) const;
  bool handleCancelOperationsRequest(const std::uint8_t* data,
                                     std::size_t size) const;
  bool handleCancelOperationsResponse(const std::uint8_t* data,
                                      std::size_t size) const;
  bool handleConfigureChannelSpacingRequest(const std::uint8_t* data,
                                            std::size_t size) const;
  bool handleConfigureChannelSpacingResponse(const std::uint8_t* data,
                                             std::size_t size) const;
  bool handleRadioStationInfoNotification(const std::uint8_t* data,
                                          std::size_t size) const;
  bool handleMuteRadioRequest(const std::uint8_t* data,
                              std::size_t size) const;
  bool handleMuteRadioResponse(const std::uint8_t* data,
                               std::size_t size) const;
  bool handleGetTrafficUpdateRequest(const std::uint8_t* data,
                                     std::size_t size) const;
  bool handleGetTrafficUpdateResponse(const std::uint8_t* data,
                                      std::size_t size) const;
  bool handleRadioSourceRequest(const std::uint8_t* data,
                                std::size_t size) const;
  bool handleRadioSourceResponse(const std::uint8_t* data,
                                 std::size_t size) const;
  bool handleRadioStateNotification(const std::uint8_t* data,
                                    std::size_t size) const;

  mutable std::uint64_t messageCount_{0};
  mutable ::aasdk::messenger::ChannelId radioChannelId_{::aasdk::messenger::ChannelId::NONE};
  mutable ::aasdk::messenger::EncryptionType radioEncryptionType_{::aasdk::messenger::EncryptionType::PLAIN};
  std::shared_ptr<::aasdk::messenger::MessageSender> sender_;
};

}
