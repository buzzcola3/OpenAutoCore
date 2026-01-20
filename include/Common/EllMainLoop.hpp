#pragma once

#include <atomic>
#include <mutex>
#include <thread>

namespace f1x::openauto::common {

class EllMainLoop {
public:
    static EllMainLoop& instance();

    void ensureRunning();
    void shutdown();

private:
    EllMainLoop();
    ~EllMainLoop();

    EllMainLoop(const EllMainLoop&) = delete;
    EllMainLoop& operator=(const EllMainLoop&) = delete;

    std::atomic_bool running_{false};
    std::mutex mutex_;
    std::thread loopThread_;
};

}
