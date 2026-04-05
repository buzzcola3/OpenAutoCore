#include <gtest/gtest.h>
#include <Lite/Session.hpp>

using namespace aasdk::lite;
using namespace aasdk::messenger;

TEST(Session, StopBeforeRunIsNotRunning) {
    // We can't call run() without a real USB device, but we can verify
    // stop/isRunning semantics.
    // Session needs a valid USBDevice — construct one with a null handle
    // and just test the accessors.
    USBDevice dev(nullptr, 0x81, 0x01);
    auto session = std::make_unique<Session>(std::move(dev), nullptr);

    EXPECT_FALSE(session->isRunning());
    session->stop(); // should not crash
    EXPECT_FALSE(session->isRunning());
}

TEST(Session, RouterIsAccessible) {
    USBDevice dev(nullptr, 0x81, 0x01);
    Session session(std::move(dev), nullptr);

    bool called = false;
    session.router().setHandler(ChannelId::CONTROL, [&](const InMessage&) {
        called = true;
    });

    InMessage msg;
    msg.channelId = ChannelId::CONTROL;
    msg.payload = {0, 1};
    session.router().dispatch(msg);
    EXPECT_TRUE(called);
}

TEST(Session, IoIsAccessible) {
    USBDevice dev(nullptr, 0x81, 0x01);
    Session session(std::move(dev), nullptr);

    // Just verify we can get a reference to FrameIO
    FrameIO& io = session.io();
    (void)io; // no crash
}
