#include <Messenger/handlers/RadioMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageType.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/radio/RadioMessageId.pb.h>
#include <aap_protobuf/service/radio/message/ActiveRadioNotification.pb.h>
#include <aap_protobuf/service/radio/message/CancelRadioOperationsRequest.pb.h>
#include <aap_protobuf/service/radio/message/CancelRadioOperationsResponse.pb.h>
#include <aap_protobuf/service/radio/message/ConfigureChannelSpacingRequest.pb.h>
#include <aap_protobuf/service/radio/message/ConfigureChannelSpacingResponse.pb.h>
#include <aap_protobuf/service/radio/message/GetProgramListRequest.pb.h>
#include <aap_protobuf/service/radio/message/GetProgramListResponse.pb.h>
#include <aap_protobuf/service/radio/message/GetTrafficUpdateRequest.pb.h>
#include <aap_protobuf/service/radio/message/GetTrafficUpdateResponse.pb.h>
#include <aap_protobuf/service/radio/message/MuteRadioRequest.pb.h>
#include <aap_protobuf/service/radio/message/MuteRadioResponse.pb.h>
#include <aap_protobuf/service/radio/message/RadioSourceRequest.pb.h>
#include <aap_protobuf/service/radio/message/RadioSourceResponse.pb.h>
#include <aap_protobuf/service/radio/message/RadioStateNotification.pb.h>
#include <aap_protobuf/service/radio/message/RadioStationInfoNotification.pb.h>
#include <aap_protobuf/service/radio/message/ScanStationsRequest.pb.h>
#include <aap_protobuf/service/radio/message/ScanStationsResponse.pb.h>
#include <aap_protobuf/service/radio/message/SeekStationRequest.pb.h>
#include <aap_protobuf/service/radio/message/SeekStationResponse.pb.h>
#include <aap_protobuf/service/radio/message/SelectActiveRadioRequest.pb.h>
#include <aap_protobuf/service/radio/message/StationPresetsNotification.pb.h>
#include <aap_protobuf/service/radio/message/StepChannelRequest.pb.h>
#include <aap_protobuf/service/radio/message/StepChannelResponse.pb.h>
#include <aap_protobuf/service/radio/message/TuneToStationRequest.pb.h>
#include <aap_protobuf/service/radio/message/TuneToStationResponse.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <limits>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using Radio = aap_protobuf::service::radio::RadioMessageId;
constexpr const char* kLogPrefix = "[RadioMessageHandlers]";

template<typename Proto>
bool parsePayload(const std::uint8_t* data, std::size_t size, Proto& message, const char* label) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    AASDK_LOG(error) << kLogPrefix << " " << label << " payload too large, bytes=" << size;
    return false;
  }

  if (!message.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << kLogPrefix << " Failed to parse " << label << " payload, bytes=" << size;
    return false;
  }

  return true;
}

}

namespace aasdk::messenger::interceptor {

bool RadioMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << kLogPrefix << " radio payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      return handleChannelOpenRequest(message, payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_ACTIVE_RADIO_NOTIFICATION:
      return handleActiveRadioNotification(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_SELECT_ACTIVE_RADIO_REQUEST:
      return handleSelectActiveRadioRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_STEP_CHANNEL_REQUEST:
      return handleStepChannelRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_STEP_CHANNEL_RESPONSE:
      return handleStepChannelResponse(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_SEEK_STATION_REQUEST:
      return handleSeekStationRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_SEEK_STATION_RESPONSE:
      return handleSeekStationResponse(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_SCAN_STATIONS_REQUEST:
      return handleScanStationsRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_SCAN_STATIONS_RESPONSE:
      return handleScanStationsResponse(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_TUNE_TO_STATION_REQUEST:
      return handleTuneToStationRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_TUNE_TO_STATION_RESPONSE:
      return handleTuneToStationResponse(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_GET_PROGRAM_LIST_REQUEST:
      return handleGetProgramListRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_GET_PROGRAM_LIST_RESPONSE:
      return handleGetProgramListResponse(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_STATION_PRESETS_NOTIFICATION:
      return handleStationPresetsNotification(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_CANCEL_OPERATIONS_REQUEST:
      return handleCancelOperationsRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_CANCEL_OPERATIONS_RESPONSE:
      return handleCancelOperationsResponse(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_CONFIGURE_CHANNEL_SPACING_REQUEST:
      return handleConfigureChannelSpacingRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_CONFIGURE_CHANNEL_SPACING_RESPONSE:
      return handleConfigureChannelSpacingResponse(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_RADIO_STATION_INFO_NOTIFICATION:
      return handleRadioStationInfoNotification(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_MUTE_RADIO_REQUEST:
      return handleMuteRadioRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_MUTE_RADIO_RESPONSE:
      return handleMuteRadioResponse(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_GET_TRAFFIC_UPDATE_REQUEST:
      return handleGetTrafficUpdateRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_GET_TRAFFIC_UPDATE_RESPONSE:
      return handleGetTrafficUpdateResponse(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_RADIO_SOURCE_REQUEST:
      return handleRadioSourceRequest(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_RADIO_SOURCE_RESPONSE:
      return handleRadioSourceResponse(payloadData, payloadSize);
    case Radio::RADIO_MESSAGE_STATE_NOTIFICATION:
      return handleRadioStateNotification(payloadData, payloadSize);
    default:
      AASDK_LOG(debug) << kLogPrefix << " message id=" << messageId.getId()
                       << " not explicitly handled.";
      return false;
  }
}

void RadioMessageHandlers::setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

bool RadioMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                    const std::uint8_t* data,
                                                    std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!parsePayload(data, size, request, "ChannelOpenRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ChannelOpenRequest: " << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  radioChannelId_ = message.getChannelId();
  radioEncryptionType_ = message.getEncryptionType();

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::CONTROL,
                          Control::MESSAGE_CHANNEL_OPEN_RESPONSE,
                          response);
    return true;
  }

  AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send channel open response.";
  return false;
}

bool RadioMessageHandlers::handleActiveRadioNotification(const std::uint8_t* data,
                                                         std::size_t size) const {
  aap_protobuf::service::radio::message::ActiveRadioNotification notification;
  if (!parsePayload(data, size, notification, "ActiveRadioNotification")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ActiveRadioNotification: "
                   << notification.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleSelectActiveRadioRequest(const std::uint8_t* data,
                                                          std::size_t size) const {
  aap_protobuf::service::radio::message::SelectActiveRadioRequest request;
  if (!parsePayload(data, size, request, "SelectActiveRadioRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " SelectActiveRadioRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleStepChannelRequest(const std::uint8_t* data,
                                                    std::size_t size) const {
  aap_protobuf::service::radio::message::StepChannelRequest request;
  if (!parsePayload(data, size, request, "StepChannelRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " StepChannelRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleStepChannelResponse(const std::uint8_t* data,
                                                     std::size_t size) const {
  aap_protobuf::service::radio::message::StepChannelResponse response;
  if (!parsePayload(data, size, response, "StepChannelResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " StepChannelResponse: "
                   << response.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleSeekStationRequest(const std::uint8_t* data,
                                                    std::size_t size) const {
  aap_protobuf::service::radio::message::SeekStationRequest request;
  if (!parsePayload(data, size, request, "SeekStationRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " SeekStationRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleSeekStationResponse(const std::uint8_t* data,
                                                     std::size_t size) const {
  aap_protobuf::service::radio::message::SeekStationResponse response;
  if (!parsePayload(data, size, response, "SeekStationResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " SeekStationResponse: "
                   << response.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleScanStationsRequest(const std::uint8_t* data,
                                                     std::size_t size) const {
  aap_protobuf::service::radio::message::ScanStationsRequest request;
  if (!parsePayload(data, size, request, "ScanStationsRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ScanStationsRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleScanStationsResponse(const std::uint8_t* data,
                                                      std::size_t size) const {
  aap_protobuf::service::radio::message::ScanStationsResponse response;
  if (!parsePayload(data, size, response, "ScanStationsResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ScanStationsResponse: "
                   << response.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleTuneToStationRequest(const std::uint8_t* data,
                                                      std::size_t size) const {
  aap_protobuf::service::radio::message::TuneToStationRequest request;
  if (!parsePayload(data, size, request, "TuneToStationRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " TuneToStationRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleTuneToStationResponse(const std::uint8_t* data,
                                                       std::size_t size) const {
  aap_protobuf::service::radio::message::TuneToStationResponse response;
  if (!parsePayload(data, size, response, "TuneToStationResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " TuneToStationResponse: "
                   << response.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleGetProgramListRequest(const std::uint8_t* data,
                                                       std::size_t size) const {
  aap_protobuf::service::radio::message::GetProgramListRequest request;
  if (!parsePayload(data, size, request, "GetProgramListRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " GetProgramListRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleGetProgramListResponse(const std::uint8_t* data,
                                                        std::size_t size) const {
  aap_protobuf::service::radio::message::GetProgramListResponse response;
  if (!parsePayload(data, size, response, "GetProgramListResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " GetProgramListResponse: "
                   << response.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleStationPresetsNotification(const std::uint8_t* data,
                                                            std::size_t size) const {
  aap_protobuf::service::radio::message::StationPresetsNotification notification;
  if (!parsePayload(data, size, notification, "StationPresetsNotification")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " StationPresetsNotification: "
                   << notification.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleCancelOperationsRequest(const std::uint8_t* data,
                                                         std::size_t size) const {
  aap_protobuf::service::radio::message::CancelRadioOperationsRequest request;
  if (!parsePayload(data, size, request, "CancelRadioOperationsRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " CancelRadioOperationsRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleCancelOperationsResponse(const std::uint8_t* data,
                                                          std::size_t size) const {
  aap_protobuf::service::radio::message::CancelRadioOperationsResponse response;
  if (!parsePayload(data, size, response, "CancelRadioOperationsResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " CancelRadioOperationsResponse: "
                   << response.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleConfigureChannelSpacingRequest(const std::uint8_t* data,
                                                                std::size_t size) const {
  aap_protobuf::service::radio::message::ConfigureChannelSpacingRequest request;
  if (!parsePayload(data, size, request, "ConfigureChannelSpacingRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ConfigureChannelSpacingRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleConfigureChannelSpacingResponse(const std::uint8_t* data,
                                                                 std::size_t size) const {
  aap_protobuf::service::radio::message::ConfigureChannelSpacingResponse response;
  if (!parsePayload(data, size, response, "ConfigureChannelSpacingResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ConfigureChannelSpacingResponse: "
                   << response.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleRadioStationInfoNotification(const std::uint8_t* data,
                                                              std::size_t size) const {
  aap_protobuf::service::radio::message::RadioStationInfoNotification notification;
  if (!parsePayload(data, size, notification, "RadioStationInfoNotification")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " RadioStationInfoNotification: "
                   << notification.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleMuteRadioRequest(const std::uint8_t* data,
                                                  std::size_t size) const {
  aap_protobuf::service::radio::message::MuteRadioRequest request;
  if (!parsePayload(data, size, request, "MuteRadioRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MuteRadioRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleMuteRadioResponse(const std::uint8_t* data,
                                                   std::size_t size) const {
  aap_protobuf::service::radio::message::MuteRadioResponse response;
  if (!parsePayload(data, size, response, "MuteRadioResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MuteRadioResponse: "
                   << response.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleGetTrafficUpdateRequest(const std::uint8_t* data,
                                                         std::size_t size) const {
  aap_protobuf::service::radio::message::GetTrafficUpdateRequest request;
  if (!parsePayload(data, size, request, "GetTrafficUpdateRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " GetTrafficUpdateRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleGetTrafficUpdateResponse(const std::uint8_t* data,
                                                          std::size_t size) const {
  aap_protobuf::service::radio::message::GetTrafficUpdateResponse response;
  if (!parsePayload(data, size, response, "GetTrafficUpdateResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " GetTrafficUpdateResponse: "
                   << response.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleRadioSourceRequest(const std::uint8_t* data,
                                                    std::size_t size) const {
  aap_protobuf::service::radio::message::RadioSourceRequest request;
  if (!parsePayload(data, size, request, "RadioSourceRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " RadioSourceRequest: "
                   << request.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleRadioSourceResponse(const std::uint8_t* data,
                                                     std::size_t size) const {
  aap_protobuf::service::radio::message::RadioSourceResponse response;
  if (!parsePayload(data, size, response, "RadioSourceResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " RadioSourceResponse: "
                   << response.ShortDebugString();
  return true;
}

bool RadioMessageHandlers::handleRadioStateNotification(const std::uint8_t* data,
                                                        std::size_t size) const {
  aap_protobuf::service::radio::message::RadioStateNotification notification;
  if (!parsePayload(data, size, notification, "RadioStateNotification")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " RadioStateNotification: "
                   << notification.ShortDebugString();
  return true;
}

}
