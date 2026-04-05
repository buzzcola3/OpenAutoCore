#include <gtest/gtest.h>
#include <Lite/FrameIO.hpp>
#include <boost/endian/conversion.hpp>
#include <cstring>

using namespace aasdk::lite;
using namespace aasdk::messenger;

// ─── encodeFrameHeader / decodeFrameHeader ────────────────────────

TEST(FrameIO_Header, BulkPlainSpecific) {
    uint8_t buf[2]{};
    FrameIO::encodeFrameHeader(buf, ChannelId::CONTROL, FrameType::BULK,
                               EncryptionType::PLAIN, MessageType::SPECIFIC);
    EXPECT_EQ(buf[0], 0); // CONTROL = 0
    EXPECT_EQ(buf[1], 3); // BULK=3 | PLAIN=0 | SPECIFIC=0

    ChannelId ch; FrameType ft; EncryptionType enc; MessageType mt;
    FrameIO::decodeFrameHeader(buf, ch, ft, enc, mt);
    EXPECT_EQ(ch, ChannelId::CONTROL);
    EXPECT_EQ(ft, FrameType::BULK);
    EXPECT_EQ(enc, EncryptionType::PLAIN);
    EXPECT_EQ(mt, MessageType::SPECIFIC);
}

TEST(FrameIO_Header, FirstEncryptedControl) {
    uint8_t buf[2]{};
    FrameIO::encodeFrameHeader(buf, ChannelId::SENSOR, FrameType::FIRST,
                               EncryptionType::ENCRYPTED, MessageType::CONTROL);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(ChannelId::SENSOR));
    // FIRST=1 | ENCRYPTED=8 | CONTROL=4 → 13
    EXPECT_EQ(buf[1], 0x0D);

    ChannelId ch; FrameType ft; EncryptionType enc; MessageType mt;
    FrameIO::decodeFrameHeader(buf, ch, ft, enc, mt);
    EXPECT_EQ(ch, ChannelId::SENSOR);
    EXPECT_EQ(ft, FrameType::FIRST);
    EXPECT_EQ(enc, EncryptionType::ENCRYPTED);
    EXPECT_EQ(mt, MessageType::CONTROL);
}

TEST(FrameIO_Header, MiddlePlainSpecific) {
    uint8_t buf[2]{};
    FrameIO::encodeFrameHeader(buf, ChannelId::MEDIA_SINK, FrameType::MIDDLE,
                               EncryptionType::PLAIN, MessageType::SPECIFIC);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(ChannelId::MEDIA_SINK));
    EXPECT_EQ(buf[1], 0x00); // MIDDLE=0

    ChannelId ch; FrameType ft; EncryptionType enc; MessageType mt;
    FrameIO::decodeFrameHeader(buf, ch, ft, enc, mt);
    EXPECT_EQ(ch, ChannelId::MEDIA_SINK);
    EXPECT_EQ(ft, FrameType::MIDDLE);
    EXPECT_EQ(enc, EncryptionType::PLAIN);
    EXPECT_EQ(mt, MessageType::SPECIFIC);
}

TEST(FrameIO_Header, LastEncryptedSpecific) {
    uint8_t buf[2]{};
    FrameIO::encodeFrameHeader(buf, ChannelId::INPUT_SOURCE, FrameType::LAST,
                               EncryptionType::ENCRYPTED, MessageType::SPECIFIC);
    // LAST=2 | ENCRYPTED=8 = 10 (0x0A)
    EXPECT_EQ(buf[1], 0x0A);

    ChannelId ch; FrameType ft; EncryptionType enc; MessageType mt;
    FrameIO::decodeFrameHeader(buf, ch, ft, enc, mt);
    EXPECT_EQ(ft, FrameType::LAST);
    EXPECT_EQ(enc, EncryptionType::ENCRYPTED);
    EXPECT_EQ(mt, MessageType::SPECIFIC);
}

// ─── encodeFrameSize / decodeFrameSize ────────────────────────────

TEST(FrameIO_Size, ShortBulk) {
    uint8_t buf[6]{};
    size_t written = FrameIO::encodeFrameSize(buf, FrameType::BULK, 0x1234, 0);
    EXPECT_EQ(written, 2u);

    // Should be big-endian
    EXPECT_EQ(buf[0], 0x12);
    EXPECT_EQ(buf[1], 0x34);

    uint32_t totalSize = 0;
    uint16_t frameSize = FrameIO::decodeFrameSize(buf, FrameType::BULK, totalSize);
    EXPECT_EQ(frameSize, 0x1234);
    EXPECT_EQ(totalSize, 0x1234u); // for non-FIRST, totalSize == frameSize
}

TEST(FrameIO_Size, ShortMiddle) {
    uint8_t buf[6]{};
    size_t written = FrameIO::encodeFrameSize(buf, FrameType::MIDDLE, 100, 0);
    EXPECT_EQ(written, 2u);

    uint32_t totalSize = 0;
    uint16_t frameSize = FrameIO::decodeFrameSize(buf, FrameType::MIDDLE, totalSize);
    EXPECT_EQ(frameSize, 100);
}

TEST(FrameIO_Size, ExtendedFirst) {
    uint8_t buf[6]{};
    size_t written = FrameIO::encodeFrameSize(buf, FrameType::FIRST, 0x4000, 0x00054321);
    EXPECT_EQ(written, 6u);

    // First 2 bytes: frame payload size (0x4000)
    EXPECT_EQ(buf[0], 0x40);
    EXPECT_EQ(buf[1], 0x00);

    // Next 4 bytes: total message size (0x00054321)
    EXPECT_EQ(buf[2], 0x00);
    EXPECT_EQ(buf[3], 0x05);
    EXPECT_EQ(buf[4], 0x43);
    EXPECT_EQ(buf[5], 0x21);

    uint32_t totalSize = 0;
    uint16_t frameSize = FrameIO::decodeFrameSize(buf, FrameType::FIRST, totalSize);
    EXPECT_EQ(frameSize, 0x4000);
    EXPECT_EQ(totalSize, 0x00054321u);
}

TEST(FrameIO_Size, ZeroPayload) {
    uint8_t buf[6]{};
    size_t written = FrameIO::encodeFrameSize(buf, FrameType::BULK, 0, 0);
    EXPECT_EQ(written, 2u);
    EXPECT_EQ(buf[0], 0x00);
    EXPECT_EQ(buf[1], 0x00);

    uint32_t totalSize = 0;
    uint16_t frameSize = FrameIO::decodeFrameSize(buf, FrameType::BULK, totalSize);
    EXPECT_EQ(frameSize, 0);
}

// ─── Roundtrip: encode then decode, all frame types ────────────────

TEST(FrameIO_Roundtrip, AllFrameTypes) {
    struct TestCase {
        ChannelId ch;
        FrameType ft;
        EncryptionType enc;
        MessageType mt;
        uint16_t payloadSize;
        uint32_t totalSize;
    };

    TestCase cases[] = {
        {ChannelId::CONTROL,     FrameType::BULK,   EncryptionType::PLAIN,     MessageType::SPECIFIC, 42, 42},
        {ChannelId::SENSOR,      FrameType::FIRST,  EncryptionType::ENCRYPTED, MessageType::CONTROL,  0x4000, 0x00020000},
        {ChannelId::INPUT_SOURCE,FrameType::MIDDLE, EncryptionType::ENCRYPTED, MessageType::SPECIFIC, 0x4000, 0},
        {ChannelId::BLUETOOTH,   FrameType::LAST,   EncryptionType::PLAIN,     MessageType::CONTROL,  100, 0},
    };

    for (auto& tc : cases) {
        uint8_t hdr[2]{};
        FrameIO::encodeFrameHeader(hdr, tc.ch, tc.ft, tc.enc, tc.mt);

        ChannelId ch; FrameType ft; EncryptionType enc; MessageType mt;
        FrameIO::decodeFrameHeader(hdr, ch, ft, enc, mt);
        EXPECT_EQ(ch, tc.ch);
        EXPECT_EQ(ft, tc.ft);
        EXPECT_EQ(enc, tc.enc);
        EXPECT_EQ(mt, tc.mt);

        uint8_t sizeBuf[6]{};
        FrameIO::encodeFrameSize(sizeBuf, tc.ft, tc.payloadSize, tc.totalSize);

        uint32_t totalSize = 0;
        uint16_t frameSize = FrameIO::decodeFrameSize(sizeBuf, tc.ft, totalSize);
        EXPECT_EQ(frameSize, tc.payloadSize);
        if (tc.ft == FrameType::FIRST) {
            EXPECT_EQ(totalSize, tc.totalSize);
        }
    }
}
