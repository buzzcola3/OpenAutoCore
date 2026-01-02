#pragma once

#include <google/protobuf/message.h>

namespace f1x::openauto::autoapp::config {

// Load a text-format protobuf from disk into dst; logs on error.
bool loadTextProto(const std::string& path,
				   google::protobuf::Message& dst,
				   const char* label = nullptr) noexcept;

}
