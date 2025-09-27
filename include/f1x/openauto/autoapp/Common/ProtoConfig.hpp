#pragma once

#include <string>
#include <google/protobuf/message.h>

namespace f1x::openauto::autoapp::config {

// Load a textproto from `path` into `dst`. Returns true on success.
// `label` is used only for logging context.
bool loadTextProto(const std::string& path,
                   google::protobuf::Message& dst,
                   const char* label = nullptr) noexcept;

// Pointer overload for convenience.
inline bool loadTextProto(const std::string& path,
                          google::protobuf::Message* dst,
                          const char* label = nullptr) noexcept {
  return dst != nullptr && loadTextProto(path, *dst, label);
}

} // namespace f1x::openauto::autoapp::config