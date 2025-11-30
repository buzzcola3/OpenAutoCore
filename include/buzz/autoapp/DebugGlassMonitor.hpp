#pragma once

#include <cstddef>

struct libusb_context;

namespace debugglass {
class DebugGlass;
}  // namespace debugglass

namespace buzz::autoapp {

extern debugglass::DebugGlass gDebugGlassMonitor;

void InitializeDebugGlassUsbMonitor(libusb_context* context, std::size_t worker_count);
void RecordDebugGlassUsbEvent(int result);

}  // namespace buzz::autoapp
