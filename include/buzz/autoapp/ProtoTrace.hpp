#pragma once

namespace aasdk::messenger {
enum class ChannelId;
class Message;
}

namespace buzz::autoapp::debug {

enum class ProtoTraceDirection {
  kTx,
  kRx,
};

// Attempts to decode the protobuf payload carried by an aasdk messenger message
// and record it inside DebugGlass. No-op if DebugGlass is disabled or the
// payload is not a protobuf.
void TraceMessengerMessage(ProtoTraceDirection direction,
                           aasdk::messenger::ChannelId channel_id,
                           const aasdk::messenger::Message& message);

}
