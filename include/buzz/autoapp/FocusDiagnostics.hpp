#pragma once

#include <aap_protobuf/service/control/message/AudioFocusRequestType.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusStateType.pb.h>
#include <aap_protobuf/service/media/video/message/VideoFocusMode.pb.h>
#include <aap_protobuf/service/media/video/message/VideoFocusReason.pb.h>

namespace buzz::autoapp {

void RecordAudioFocusRequest(aap_protobuf::service::control::message::AudioFocusRequestType type);
void RecordAudioFocusResponse(aap_protobuf::service::control::message::AudioFocusStateType state);
void RecordVideoFocusRequest(aap_protobuf::service::media::video::message::VideoFocusMode mode,
                             aap_protobuf::service::media::video::message::VideoFocusReason reason);
void RecordVideoFocusNotification(aap_protobuf::service::media::video::message::VideoFocusMode focus,
                                  bool unsolicited);

}
