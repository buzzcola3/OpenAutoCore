@0xb41c8c4c1b2a49c8;

const protocolVersion :UInt16 = 1;

enum MsgType {
  video   @0;
  audio   @1;
  touch   @2;
  status  @3;
  control @4;
}

struct Envelope {
  version        @0 :UInt16;    # set to ProtocolVersion
  msgType        @1 :MsgType;   # what the payload is
  timestampUsec  @2 :UInt64;    # capture / emit time (microseconds)
  data           @3 :Data;      # raw payload bytes (your choice of format)
}