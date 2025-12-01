#pragma once

#include <string>

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace buzz::autoapp::debug {

// Records up to a limited number of protobuf DebugString outputs into
// per-channel DebugGlass windows.
void RecordProtoMessage(const std::string& channel,
						const std::string& label,
						const google::protobuf::Message& message);

// Returns true if DebugGlass is running and the channel-specific Proto window
// still has capacity for additional entries.
bool ShouldTraceProtoMessages(const std::string& channel);

}  // namespace buzz::autoapp::debug
