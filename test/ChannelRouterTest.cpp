#include <gtest/gtest.h>
#include <Lite/ChannelRouter.hpp>

using namespace aasdk::lite;
using namespace aasdk::messenger;

TEST(ChannelRouter, DispatchToRegisteredHandler) {
    ChannelRouter router;
    InMessage received;
    bool called = false;

    router.setHandler(ChannelId::CONTROL, [&](const InMessage& msg) {
        called = true;
        received = msg;
    });

    InMessage msg;
    msg.channelId = ChannelId::CONTROL;
    msg.payload = {1, 2, 3};

    EXPECT_TRUE(router.dispatch(msg));
    EXPECT_TRUE(called);
    EXPECT_EQ(received.payload, (std::vector<uint8_t>{1, 2, 3}));
}

TEST(ChannelRouter, DispatchUnregisteredReturnsFalse) {
    ChannelRouter router;
    InMessage msg;
    msg.channelId = ChannelId::SENSOR;

    EXPECT_FALSE(router.dispatch(msg));
}

TEST(ChannelRouter, ClearHandlerUnregisters) {
    ChannelRouter router;
    bool called = false;
    router.setHandler(ChannelId::BLUETOOTH, [&](const InMessage&) { called = true; });
    router.clearHandler(ChannelId::BLUETOOTH);

    InMessage msg;
    msg.channelId = ChannelId::BLUETOOTH;
    EXPECT_FALSE(router.dispatch(msg));
    EXPECT_FALSE(called);
}

TEST(ChannelRouter, ReplaceHandler) {
    ChannelRouter router;
    int callCount1 = 0, callCount2 = 0;

    router.setHandler(ChannelId::CONTROL, [&](const InMessage&) { callCount1++; });
    router.setHandler(ChannelId::CONTROL, [&](const InMessage&) { callCount2++; });

    InMessage msg;
    msg.channelId = ChannelId::CONTROL;
    router.dispatch(msg);

    EXPECT_EQ(callCount1, 0);
    EXPECT_EQ(callCount2, 1);
}

TEST(ChannelRouter, MultipleChannelsIndependent) {
    ChannelRouter router;
    int controlCount = 0, sensorCount = 0;

    router.setHandler(ChannelId::CONTROL, [&](const InMessage&) { controlCount++; });
    router.setHandler(ChannelId::SENSOR, [&](const InMessage&) { sensorCount++; });

    InMessage m1; m1.channelId = ChannelId::CONTROL;
    InMessage m2; m2.channelId = ChannelId::SENSOR;
    InMessage m3; m3.channelId = ChannelId::SENSOR;

    router.dispatch(m1);
    router.dispatch(m2);
    router.dispatch(m3);

    EXPECT_EQ(controlCount, 1);
    EXPECT_EQ(sensorCount, 2);
}
