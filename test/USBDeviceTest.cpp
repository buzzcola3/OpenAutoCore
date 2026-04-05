#include <gtest/gtest.h>
#include <Lite/USBDevice.hpp>

namespace {

using namespace aasdk::lite;

TEST(USBDeviceTest, ConstructWithEndpoints) {
    // Construct with a null handle — we can't do real USB in unit tests,
    // but we can verify the state machine.
    USBDevice dev(nullptr, 0x81, 0x01);
    EXPECT_TRUE(dev.isOpen());
    EXPECT_EQ(dev.rawHandle(), nullptr);
}

TEST(USBDeviceTest, CloseMarksNotOpen) {
    USBDevice dev(nullptr, 0x81, 0x01);
    EXPECT_TRUE(dev.isOpen());
    dev.close();
    EXPECT_FALSE(dev.isOpen());
}

TEST(USBDeviceTest, ReadOnClosedDeviceFails) {
    USBDevice dev(nullptr, 0x81, 0x01);
    dev.close();
    uint8_t buf[16];
    EXPECT_FALSE(dev.read(buf, sizeof(buf)));
}

TEST(USBDeviceTest, WriteOnClosedDeviceFails) {
    USBDevice dev(nullptr, 0x81, 0x01);
    dev.close();
    uint8_t buf[16] = {};
    EXPECT_FALSE(dev.write(buf, sizeof(buf)));
}

TEST(USBDeviceTest, DoubleCloseIsSafe) {
    USBDevice dev(nullptr, 0x81, 0x01);
    dev.close();
    dev.close(); // should not crash
    EXPECT_FALSE(dev.isOpen());
}

} // namespace
