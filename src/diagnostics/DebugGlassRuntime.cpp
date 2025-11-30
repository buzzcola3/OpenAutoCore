#include "buzz/autoapp/DebugGlassMonitor.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>

#include <libusb-1.0/libusb.h>

#include "debugglass/debugglass.h"
#include "debugglass/widgets/structure.h"
#include "debugglass/widgets/variable.h"

namespace buzz::autoapp {
namespace {

constexpr std::chrono::milliseconds kPublishInterval{250};

class DebugGlassUsbMonitor {
public:
    void Initialize(libusb_context* context, std::size_t worker_count) {
        std::lock_guard<std::mutex> lock(mutex_);
        context_ = context;
        worker_count_ = worker_count;
    }

    void RecordEventResult(int result) {
        total_event_calls_.fetch_add(1, std::memory_order_relaxed);
        if (result < 0) {
            last_error_code_.store(result, std::memory_order_relaxed);
        }

        if (!gDebugGlassMonitor.IsRunning()) {
            return;
        }

        const auto now = Clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        if (now - last_publish_time_ < kPublishInterval) {
            return;
        }

        if (!EnsureWidgetsLocked()) {
            return;
        }

        last_publish_time_ = now;
        total_events_var_->SetValue(total_event_calls_.load());
        last_result_var_->SetValue(result);
        last_error_var_->SetValue(last_error_code_.load());
        worker_count_var_->SetValue(worker_count_);
        devices_var_->SetValue(QueryDeviceSummaryLocked());
        last_update_var_->SetValue(FormatTimestampLocked());
    }

private:
    using Clock = std::chrono::steady_clock;

    bool EnsureWidgetsLocked() {
        if (total_events_var_ != nullptr) {
            return true;
        }

        auto& window = gDebugGlassMonitor.windows["USB"];
        auto* tab = window.tabs.find("Event Loop");
        if (tab == nullptr) {
            tab = &window.tabs.Add("Event Loop");
        }

        structure_ = &tab->AddStructure("libusb");
        total_events_var_ = &structure_->AddVariable("Total Event Calls");
        last_result_var_ = &structure_->AddVariable("Last Result");
        last_error_var_ = &structure_->AddVariable("Last Error Code");
        devices_var_ = &structure_->AddVariable("Enumerated Devices");
        worker_count_var_ = &structure_->AddVariable("Worker Threads");
        last_update_var_ = &structure_->AddVariable("Last Update");
        return true;
    }

    std::string QueryDeviceSummaryLocked() {
        if (context_ == nullptr) {
            return "n/a";
        }

        libusb_device** list = nullptr;
        const ssize_t count = libusb_get_device_list(context_, &list);
        std::string summary;
        if (count < 0) {
            summary = std::string("error ") + std::to_string(count);
        } else {
            summary = std::to_string(count);
        }

        if (list != nullptr) {
            libusb_free_device_list(list, 1);
        }

        return summary;
    }

    std::string FormatTimestampLocked() const {
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm {};
        localtime_r(&tt, &tm);
        char buffer[32];
        if (std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm) == 0) {
            return {};
        }
        return buffer;
    }

    std::atomic<uint64_t> total_event_calls_{0};
    std::atomic<int> last_error_code_{0};
    libusb_context* context_ = nullptr;
    std::size_t worker_count_ = 0;
    Clock::time_point last_publish_time_{};
    mutable std::mutex mutex_;
    debugglass::Structure* structure_ = nullptr;
    debugglass::Variable* total_events_var_ = nullptr;
    debugglass::Variable* last_result_var_ = nullptr;
    debugglass::Variable* last_error_var_ = nullptr;
    debugglass::Variable* devices_var_ = nullptr;
    debugglass::Variable* worker_count_var_ = nullptr;
    debugglass::Variable* last_update_var_ = nullptr;
};

DebugGlassUsbMonitor& GetUsbMonitor() {
    static DebugGlassUsbMonitor monitor;
    return monitor;
}

}  // namespace

debugglass::DebugGlass gDebugGlassMonitor;

void InitializeDebugGlassUsbMonitor(libusb_context* context, std::size_t worker_count) {
    GetUsbMonitor().Initialize(context, worker_count);
}

void RecordDebugGlassUsbEvent(int result) {
    GetUsbMonitor().RecordEventResult(result);
}

}  // namespace buzz::autoapp
