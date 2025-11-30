#include "buzz/autoapp/MessengerDiagnostics.hpp"

#include <chrono>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>

#include <Messenger/ChannelId.hpp>
#include <Messenger/MessengerInstrumentation.hpp>

#include "buzz/autoapp/DebugGlassMonitor.hpp"
#include "debugglass/debugglass.h"
#include "debugglass/widgets/graph.h"
#include "debugglass/widgets/message_monitor.h"
#include "debugglass/widgets/structure.h"
#include "debugglass/widgets/variable.h"

namespace buzz::autoapp {
namespace {

std::string MessageTypeToString(aasdk::messenger::MessageType type) {
    switch (type) {
    case aasdk::messenger::MessageType::SPECIFIC:
        return "SPECIFIC";
    case aasdk::messenger::MessageType::CONTROL:
        return "CONTROL";
    default:
        return "UNKNOWN";
    }
}

class MessengerDiagnosticsPanel {
public:
    static MessengerDiagnosticsPanel& Instance() {
        static MessengerDiagnosticsPanel instance;
        return instance;
    }

    void Initialize() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (initialized_) {
            return;
        }
        initialized_ = true;
        auto& instrumentation = aasdk::messenger::MessengerInstrumentation::Instance();
        instrumentation.SetQueueSnapshotCallback(
            [this](const aasdk::messenger::MessengerQueueSnapshot& snapshot) { this->OnQueueSnapshot(snapshot); });
        instrumentation.SetLatencySampleCallback(
            [this](const aasdk::messenger::MessengerLatencySample& sample) { this->OnLatencySample(sample); });
    }

private:
    void OnQueueSnapshot(const aasdk::messenger::MessengerQueueSnapshot& snapshot) {
        if (!gDebugGlassMonitor.IsRunning()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!EnsureWidgetsLocked()) {
            return;
        }

        pendingPromisesVar_->SetValue(snapshot.totalPendingPromises);
        queuedMessagesVar_->SetValue(snapshot.totalQueuedMessages);
        pendingSendsVar_->SetValue(snapshot.totalPendingSends);
        oldestPendingSendVar_->SetValue(snapshot.oldestPendingSendAge.count());
        lastUpdateVar_->SetValue(FormatTimestamp());

        pendingSendsGraph_->AddValue(static_cast<float>(snapshot.totalPendingSends));
        oldestPendingSendGraph_->AddValue(static_cast<float>(snapshot.oldestPendingSendAge.count()));

        for (const auto& channel : snapshot.channels) {
            std::ostringstream stream;
            stream << "recv_promises=" << channel.pendingPromises
                   << " recv_messages=" << channel.queuedMessages
                   << " send_queue=" << channel.pendingSends;
            if (channel.pendingSends > 0) {
                stream << " oldest=" << channel.oldestSendAge.count() << "ms";
                if (channel.hasPendingSend) {
                    stream << " next=" << MessageTypeToString(channel.nextSendType)
                           << " (" << channel.nextSendPayloadSize << "B)";
                }
            }

            channelMonitor_->UpsertMessage(aasdk::messenger::channelIdToString(channel.channelId), stream.str());
        }
    }

    void OnLatencySample(const aasdk::messenger::MessengerLatencySample& sample) {
        if (!gDebugGlassMonitor.IsRunning()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!EnsureWidgetsLocked()) {
            return;
        }

        const auto latencyMs = sample.latency.count();
        sendLatencyGraph_->AddValue(static_cast<float>(latencyMs));

        std::ostringstream stream;
        stream << MessageTypeToString(sample.messageType) << " " << latencyMs << "ms";
        latencyMonitor_->UpsertMessage(aasdk::messenger::channelIdToString(sample.channelId), stream.str());
    }

    bool EnsureWidgetsLocked() {
        if (pendingPromisesVar_ != nullptr) {
            return true;
        }

        auto* window = gDebugGlassMonitor.windows.find("Messenger");
        if (window == nullptr) {
            window = &gDebugGlassMonitor.windows.Add("Messenger");
        }

        auto* tab = window->tabs.find("Diagnostics");
        if (tab == nullptr) {
            tab = &window->tabs.Add("Diagnostics");
        }

        overviewStructure_ = &tab->AddStructure("Overview");
        pendingPromisesVar_ = &overviewStructure_->AddVariable("Pending Receive Promises");
        queuedMessagesVar_ = &overviewStructure_->AddVariable("Queued Receive Messages");
        pendingSendsVar_ = &overviewStructure_->AddVariable("Pending Send Frames");
        oldestPendingSendVar_ = &overviewStructure_->AddVariable("Oldest Pending Send (ms)");
        lastUpdateVar_ = &overviewStructure_->AddVariable("Last Update");

        pendingSendsGraph_ = &tab->AddGraph("Pending Sends");
        pendingSendsGraph_->SetRange(0.0F, 32.0F);
        oldestPendingSendGraph_ = &tab->AddGraph("Oldest Pending Send (ms)");
        oldestPendingSendGraph_->SetRange(0.0F, 500.0F);
        sendLatencyGraph_ = &tab->AddGraph("Send Latency (ms)");
        sendLatencyGraph_->SetRange(0.0F, 500.0F);

        channelMonitor_ = &tab->AddMessageMonitor("Channels");
        latencyMonitor_ = &tab->AddMessageMonitor("Latency Samples");
        return true;
    }

    static std::string FormatTimestamp() {
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

    bool initialized_ = false;
    std::mutex mutex_;
    debugglass::Structure* overviewStructure_ = nullptr;
    debugglass::Variable* pendingPromisesVar_ = nullptr;
    debugglass::Variable* queuedMessagesVar_ = nullptr;
    debugglass::Variable* pendingSendsVar_ = nullptr;
    debugglass::Variable* oldestPendingSendVar_ = nullptr;
    debugglass::Variable* lastUpdateVar_ = nullptr;
    debugglass::Graph* pendingSendsGraph_ = nullptr;
    debugglass::Graph* oldestPendingSendGraph_ = nullptr;
    debugglass::Graph* sendLatencyGraph_ = nullptr;
    debugglass::MessageMonitor* channelMonitor_ = nullptr;
    debugglass::MessageMonitor* latencyMonitor_ = nullptr;
};

}  // namespace

void InitializeMessengerDiagnostics() {
    MessengerDiagnosticsPanel::Instance().Initialize();
}

}  // namespace buzz::autoapp
