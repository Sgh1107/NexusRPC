/// @file frame_codec_test.cpp
/// @brief Unit tests for the RPC frame codec (TASK-012).

#include <cstring>
#include <vector>

#include "gtest/gtest.h"

#include "nexus/rpc/frame_codec.h"

namespace nexus::rpc {
namespace {

// ============================================================================
// encodeHeader / decodeHeader ? basic round-trip
// ============================================================================

TEST(FrameCodecTest, EncodeDecodeHeaderRoundTrip) {
  RpcFrameHeader original{};
  original.magic = kMagic;
  original.version = kVersion1;
  original.msg_type = 0;  // kRequest
  original.compress_type = 0;
  original.serialize_type = 0;
  original.total_length = 100;
  original.request_id = 42;
  original.meta_length = 10;
  original.body_length = 58;
  original.reserved = 0;

  std::uint8_t wire[kFixedHeaderSize];
  encodeHeader(original, wire);

  RpcFrameHeader decoded{};
  ParseResult result = decodeHeader(wire, &decoded);

  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(decoded.magic, original.magic);
  EXPECT_EQ(decoded.version, original.version);
  EXPECT_EQ(decoded.msg_type, original.msg_type);
  EXPECT_EQ(decoded.compress_type, original.compress_type);
  EXPECT_EQ(decoded.serialize_type, original.serialize_type);
  EXPECT_EQ(decoded.total_length, original.total_length);
  EXPECT_EQ(decoded.request_id, original.request_id);
  EXPECT_EQ(decoded.meta_length, original.meta_length);
  EXPECT_EQ(decoded.body_length, original.body_length);
  EXPECT_EQ(decoded.reserved, original.reserved);
}

// ============================================================================
// Big-endian verification
// ============================================================================

TEST(FrameCodecTest, BigEndianEncoding) {
  // Verify that multi-byte fields are encoded in network byte order
  // using values that satisfy all protocol length constraints.
  RpcFrameHeader hdr{};
  hdr.magic = kMagic;
  hdr.version = 1;
  hdr.msg_type = 0;
  hdr.compress_type = 0;
  hdr.serialize_type = 0;
  hdr.request_id = 0x0102030405060708ULL;
  hdr.meta_length = 0x0000A0B0;  // 41136 (< kMaxMetadataLength)
  hdr.body_length = 0x00C0D0E0;  // 12632288 (< kMaxBodyLength)
  hdr.total_length = kFixedHeaderSize + hdr.meta_length + hdr.body_length;
  hdr.reserved = 0;

  std::uint8_t wire[kFixedHeaderSize];
  encodeHeader(hdr, wire);

  // magic at offset 0: "NRPC"
  EXPECT_EQ(wire[0], 'N');
  EXPECT_EQ(wire[1], 'R');
  EXPECT_EQ(wire[2], 'P');
  EXPECT_EQ(wire[3], 'C');

  // version at offset 4
  EXPECT_EQ(wire[4], 1);

  // total_length = 32 + 41136 + 12632288 = 12673456 = 0x00C171B0
  EXPECT_EQ(wire[8],  0x00);
  EXPECT_EQ(wire[9],  0xC1);
  EXPECT_EQ(wire[10], 0x71);
  EXPECT_EQ(wire[11], 0xB0);

  // request_id at offset 12
  EXPECT_EQ(wire[12], 0x01);
  EXPECT_EQ(wire[13], 0x02);
  EXPECT_EQ(wire[14], 0x03);
  EXPECT_EQ(wire[15], 0x04);
  EXPECT_EQ(wire[16], 0x05);
  EXPECT_EQ(wire[17], 0x06);
  EXPECT_EQ(wire[18], 0x07);
  EXPECT_EQ(wire[19], 0x08);

  // meta_length = 0x0000A0B0
  EXPECT_EQ(wire[20], 0x00);
  EXPECT_EQ(wire[21], 0x00);
  EXPECT_EQ(wire[22], 0xA0);
  EXPECT_EQ(wire[23], 0xB0);

  // body_length = 0x00C0D0E0
  EXPECT_EQ(wire[24], 0x00);
  EXPECT_EQ(wire[25], 0xC0);
  EXPECT_EQ(wire[26], 0xD0);
  EXPECT_EQ(wire[27], 0xE0);
}

// ============================================================================
// Header validation ? error paths
// ============================================================================

TEST(FrameCodecTest, DecodeHeaderInvalidMagic) {
  std::uint8_t wire[kFixedHeaderSize] = {};
  RpcFrameHeader hdr{};
  // Magic defaults to 0, which is invalid.

  ParseResult result = decodeHeader(wire, &hdr);
  EXPECT_EQ(result, ParseResult::kInvalidMagic);
}

TEST(FrameCodecTest, DecodeHeaderInvalidVersion) {
  std::uint8_t wire[kFixedHeaderSize] = {};
  wire[0] = 'N'; wire[1] = 'R'; wire[2] = 'P'; wire[3] = 'C';
  wire[4] = 99;           // version = 99 (invalid)
  wire[11] = 32;          // total_length = 32

  RpcFrameHeader decoded{};
  ParseResult result = decodeHeader(wire, &decoded);
  EXPECT_EQ(result, ParseResult::kInvalidVersion);
}

TEST(FrameCodecTest, DecodeHeaderInvalidMessageType) {
  std::uint8_t wire[kFixedHeaderSize] = {};
  wire[0] = 'N'; wire[1] = 'R'; wire[2] = 'P'; wire[3] = 'C';
  wire[4] = 1;            // version = 1
  wire[5] = 99;           // msg_type = 99 (invalid)
  wire[11] = 32;          // total_length = 32

  RpcFrameHeader decoded{};
  ParseResult result = decodeHeader(wire, &decoded);
  EXPECT_EQ(result, ParseResult::kInvalidMessageType);
}

TEST(FrameCodecTest, DecodeHeaderUnsupportedCompression) {
  std::uint8_t wire[kFixedHeaderSize] = {};
  wire[0] = 'N'; wire[1] = 'R'; wire[2] = 'P'; wire[3] = 'C';
  wire[4] = 1;            // version = 1
  wire[6] = 5;            // compress_type = 5 (invalid for v1)
  wire[11] = 32;          // total_length = 32

  RpcFrameHeader decoded{};
  ParseResult result = decodeHeader(wire, &decoded);
  EXPECT_EQ(result, ParseResult::kUnsupportedCompression);
}

TEST(FrameCodecTest, DecodeHeaderInvalidSerializeType) {
  std::uint8_t wire[kFixedHeaderSize] = {};
  wire[0] = 'N'; wire[1] = 'R'; wire[2] = 'P'; wire[3] = 'C';
  wire[4] = 1;            // version = 1
  wire[7] = 9;            // serialize_type = 9 (invalid for v1)
  wire[11] = 32;          // total_length = 32

  RpcFrameHeader decoded{};
  ParseResult result = decodeHeader(wire, &decoded);
  EXPECT_EQ(result, ParseResult::kInvalidSerializeType);
}

TEST(FrameCodecTest, DecodeHeaderReservedNonZero) {
  std::uint8_t wire[kFixedHeaderSize] = {};
  wire[0] = 'N'; wire[1] = 'R'; wire[2] = 'P'; wire[3] = 'C';
  wire[4] = 1;            // version
  wire[11] = 32;          // total_length = 32 (BE, low byte)
  wire[28] = 1;           // reserved = 1

  RpcFrameHeader decoded{};
  ParseResult result = decodeHeader(wire, &decoded);
  EXPECT_EQ(result, ParseResult::kReservedFieldNonZero);
}

// ============================================================================
// Length boundary checks
// ============================================================================

TEST(FrameCodecTest, DecodeHeaderTotalLengthOverflow) {
  std::uint8_t wire[kFixedHeaderSize] = {};
  wire[0] = 'N'; wire[1] = 'R'; wire[2] = 'P'; wire[3] = 'C';
  wire[4] = 1;            // version
  const uint32_t overflow = kMaxTotalLength + 1;
  wire[8]  = static_cast<uint8_t>(overflow >> 24);
  wire[9]  = static_cast<uint8_t>(overflow >> 16);
  wire[10] = static_cast<uint8_t>(overflow >> 8);
  wire[11] = static_cast<uint8_t>(overflow);

  RpcFrameHeader decoded{};
  ParseResult result = decodeHeader(wire, &decoded);
  EXPECT_EQ(result, ParseResult::kTotalLengthOverflow);
}

TEST(FrameCodecTest, DecodeHeaderMetadataLengthOverflow) {
  std::uint8_t wire[kFixedHeaderSize] = {};
  wire[0] = 'N'; wire[1] = 'R'; wire[2] = 'P'; wire[3] = 'C';
  wire[4] = 1;            // version
  const uint32_t overflow = kMaxMetadataLength + 1;
  wire[20] = static_cast<uint8_t>(overflow >> 24);
  wire[21] = static_cast<uint8_t>(overflow >> 16);
  wire[22] = static_cast<uint8_t>(overflow >> 8);
  wire[23] = static_cast<uint8_t>(overflow);
  // total_length = 32 + overflow (for consistency)
  const uint64_t total = static_cast<uint64_t>(kFixedHeaderSize) + overflow;
  wire[8]  = static_cast<uint8_t>((total >> 24) & 0xFF);
  wire[9]  = static_cast<uint8_t>((total >> 16) & 0xFF);
  wire[10] = static_cast<uint8_t>((total >> 8) & 0xFF);
  wire[11] = static_cast<uint8_t>(total & 0xFF);

  RpcFrameHeader decoded{};
  ParseResult result = decodeHeader(wire, &decoded);
  EXPECT_EQ(result, ParseResult::kMetadataLengthOverflow);
}

TEST(FrameCodecTest, DecodeHeaderBodyLengthOverflow) {
  std::uint8_t wire[kFixedHeaderSize] = {};
  wire[0] = 'N'; wire[1] = 'R'; wire[2] = 'P'; wire[3] = 'C';
  wire[4] = 1;            // version
  const uint32_t overflow = kMaxBodyLength + 1;
  wire[24] = static_cast<uint8_t>(overflow >> 24);
  wire[25] = static_cast<uint8_t>(overflow >> 16);
  wire[26] = static_cast<uint8_t>(overflow >> 8);
  wire[27] = static_cast<uint8_t>(overflow);
  // total_length = 32 + overflow (for consistency)
  const uint64_t total = static_cast<uint64_t>(kFixedHeaderSize) + overflow;
  wire[8]  = static_cast<uint8_t>((total >> 24) & 0xFF);
  wire[9]  = static_cast<uint8_t>((total >> 16) & 0xFF);
  wire[10] = static_cast<uint8_t>((total >> 8) & 0xFF);
  wire[11] = static_cast<uint8_t>(total & 0xFF);

  RpcFrameHeader decoded{};
  ParseResult result = decodeHeader(wire, &decoded);
  EXPECT_EQ(result, ParseResult::kBodyLengthOverflow);
}

TEST(FrameCodecTest, DecodeHeaderTotalLengthMismatch) {
  std::uint8_t wire[kFixedHeaderSize] = {};
  wire[0] = 'N'; wire[1] = 'R'; wire[2] = 'P'; wire[3] = 'C';
  wire[4] = 1;            // version
  // meta_length = 10, body_length = 20, total_length = 100 (not 32+10+20=62)
  wire[23] = 10;   // meta_length = 10
  wire[27] = 20;   // body_length = 20
  wire[11] = 100;  // total_length = 100

  RpcFrameHeader decoded{};
  ParseResult result = decodeHeader(wire, &decoded);
  EXPECT_EQ(result, ParseResult::kTotalLengthMismatch);
}

// ============================================================================
// Metadata encode/decode round-trip
// ============================================================================

TEST(FrameCodecTest, MetadataEncodeDecodeRoundTrip) {
  std::map<std::string, std::string> original;
  original["trace_id"] = "abc123";
  original["deadline_unix_ms"] = "1700000000000";
  original["client_id"] = "client-001";
  original["tenant"] = "default";

  std::vector<std::uint8_t> encoded = encodeMetadata(original);

  std::map<std::string, std::string> decoded;
  ParseResult result =
      decodeMetadata(encoded.data(),
                     static_cast<std::uint32_t>(encoded.size()), &decoded);

  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(decoded["trace_id"], "abc123");
  EXPECT_EQ(decoded["deadline_unix_ms"], "1700000000000");
  EXPECT_EQ(decoded["client_id"], "client-001");
  EXPECT_EQ(decoded["tenant"], "default");
}

TEST(FrameCodecTest, MetadataEmpty) {
  std::map<std::string, std::string> empty;
  std::vector<std::uint8_t> encoded = encodeMetadata(empty);

  EXPECT_EQ(encoded.size(), 0u);

  std::map<std::string, std::string> decoded;
  ParseResult result =
      decodeMetadata(encoded.data(),
                     static_cast<std::uint32_t>(encoded.size()), &decoded);

  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(decoded.empty());
}

TEST(FrameCodecTest, MetadataDecodeTruncated) {
  // Manually craft a truncated metadata payload.
  std::vector<std::uint8_t> data = {
      0x0A,  // tag for entry
      0x10,  // varint: 16 bytes entry length
      0x0A, 0x03, 'k', 'e', 'y',  // key field: len 3, "key"
      0x12, 0x05, 'v', 'a',       // value field: len 5, "val..."
      // Truncated ? missing bytes.
  };

  std::map<std::string, std::string> meta;
  ParseResult result =
      decodeMetadata(data.data(), static_cast<std::uint32_t>(data.size()),
                     &meta);

  EXPECT_EQ(result, ParseResult::kMetadataParseError);
}

// ============================================================================
// encodeFrame / FrameParser ? round-trip
// ============================================================================

TEST(FrameCodecTest, FrameEncodeDecodeRoundTrip) {
  std::map<std::string, std::string> meta;
  meta["trace_id"] = "trace-123";
  std::string body = "hello protobuf body";

  auto frame = encodeFrame(1, MessageType::kRequest, meta, body);

  // Parse the encoded frame.
  FrameParser parser;
  ParseResult result = parser.feed(frame.data(), frame.size());

  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(parser.isComplete());
  EXPECT_EQ(parser.header().request_id, 1u);
  EXPECT_EQ(parser.header().msg_type, 0u);  // kRequest
  EXPECT_EQ(parser.metadata().at("trace_id"), "trace-123");
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(parser.body().data()),
                        parser.body().size()),
            body);
}

TEST(FrameCodecTest, FrameEncodeDecodeEmptyMetadata) {
  std::map<std::string, std::string> meta;
  std::string body = "body only";

  auto frame = encodeFrame(5, MessageType::kResponse, meta, body);

  FrameParser parser;
  ParseResult result = parser.feed(frame.data(), frame.size());

  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(parser.metadata().empty());
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(parser.body().data()),
                        parser.body().size()),
            body);
}

TEST(FrameCodecTest, FrameEncodeDecodeEmptyBody) {
  std::map<std::string, std::string> meta;
  meta["key"] = "value";
  std::string_view body;

  auto frame = encodeFrame(0, MessageType::kRequest, meta, body);

  FrameParser parser;
  ParseResult result = parser.feed(frame.data(), frame.size());

  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(parser.metadata().at("key"), "value");
  EXPECT_TRUE(parser.body().empty());
}

TEST(FrameCodecTest, FrameEncodeDecodeEmptyAll) {
  std::map<std::string, std::string> meta;
  std::string_view body;

  auto frame = encodeFrame(7, MessageType::kRequest, meta, body);

  // Frame should be exactly 32 bytes (header only).
  EXPECT_EQ(frame.size(), kFixedHeaderSize);

  FrameParser parser;
  ParseResult result = parser.feed(frame.data(), frame.size());

  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(parser.header().total_length, kFixedHeaderSize);
  EXPECT_EQ(parser.header().meta_length, 0u);
  EXPECT_EQ(parser.header().body_length, 0u);
  EXPECT_TRUE(parser.metadata().empty());
  EXPECT_TRUE(parser.body().empty());
}

// ============================================================================
// FrameParser ? incremental feeding
// ============================================================================

TEST(FrameCodecTest, FrameParserByteByByte) {
  std::map<std::string, std::string> meta;
  meta["k"] = "v";
  std::string body = "payload";

  auto frame = encodeFrame(42, MessageType::kResponse, meta, body);

  FrameParser parser;
  for (std::size_t i = 0; i < frame.size() - 1; ++i) {
    ParseResult result = parser.feed(&frame[i], 1);
    EXPECT_EQ(result, ParseResult::kNeedMoreData)
        << "at byte " << i << " of " << frame.size();
    EXPECT_FALSE(parser.isComplete());
  }

  // Last byte completes the frame.
  ParseResult result = parser.feed(&frame.back(), 1);
  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_TRUE(parser.isComplete());
}

TEST(FrameCodecTest, FrameParserMultipleChunks) {
  std::map<std::string, std::string> meta;
  meta["a"] = "b";
  std::string body = "data";

  auto frame = encodeFrame(3, MessageType::kRequest, meta, body);

  // Feed in 3 chunks: 10 bytes, then 10 bytes, then rest.
  FrameParser parser;
  std::size_t offset = 0;

  std::size_t chunk1 = std::min<std::size_t>(10, frame.size());
  EXPECT_EQ(parser.feed(frame.data() + offset, chunk1),
            ParseResult::kNeedMoreData);
  offset += chunk1;

  std::size_t chunk2 = std::min<std::size_t>(10, frame.size() - offset);
  if (chunk2 > 0) {
    ParseResult r2 = parser.feed(frame.data() + offset, chunk2);
    offset += chunk2;
    if (offset == frame.size()) {
      EXPECT_EQ(r2, ParseResult::kOk);
    }
  }

  if (offset < frame.size()) {
    EXPECT_EQ(parser.feed(frame.data() + offset, frame.size() - offset),
              ParseResult::kOk);
  }

  EXPECT_TRUE(parser.isComplete());
}

TEST(FrameCodecTest, FrameParserReset) {
  std::string body = "first";
  auto frame1 = encodeFrame(1, MessageType::kRequest, {}, body);

  FrameParser parser;
  EXPECT_EQ(parser.feed(frame1.data(), frame1.size()), ParseResult::kOk);
  EXPECT_TRUE(parser.isComplete());
  EXPECT_EQ(parser.header().request_id, 1u);

  parser.reset();
  EXPECT_FALSE(parser.isComplete());
  EXPECT_EQ(parser.state(), ParseState::kNeedHeader);
  EXPECT_EQ(parser.bufferSize(), 0u);

  body = "second";
  auto frame2 = encodeFrame(2, MessageType::kResponse, {}, body);
  EXPECT_EQ(parser.feed(frame2.data(), frame2.size()), ParseResult::kOk);
  EXPECT_TRUE(parser.isComplete());
  EXPECT_EQ(parser.header().request_id, 2u);
  EXPECT_EQ(parser.header().msg_type, 1u);  // Response
}

TEST(FrameCodecTest, FrameParserTwoFramesBackToBack) {
  auto frame1 = encodeFrame(10, MessageType::kRequest, {}, "first");
  auto frame2 = encodeFrame(20, MessageType::kResponse, {}, "second");

  // Concatenate two frames into one byte stream.
  std::vector<std::uint8_t> combined;
  combined.insert(combined.end(), frame1.begin(), frame1.end());
  combined.insert(combined.end(), frame2.begin(), frame2.end());

  FrameParser parser;

  // Feed all at once ? should parse the first frame and retain trailing
  // bytes in the internal buffer.
  ParseResult r = parser.feed(combined.data(), combined.size());
  EXPECT_EQ(r, ParseResult::kOk);
  EXPECT_TRUE(parser.isComplete());
  EXPECT_EQ(parser.header().request_id, 10u);

  // Verify the parser still holds the second frame's bytes.
  EXPECT_EQ(parser.bufferSize(), frame2.size());

  // Reset and feed zero new bytes ? the parser should complete the
  // second frame from its retained buffer without any external offset
  // computation.
  parser.reset();
  EXPECT_FALSE(parser.isComplete());
  EXPECT_EQ(parser.state(), ParseState::kNeedHeader);
  EXPECT_EQ(parser.bufferSize(), frame2.size());

  r = parser.feed(nullptr, 0);
  EXPECT_EQ(r, ParseResult::kOk);
  EXPECT_TRUE(parser.isComplete());
  EXPECT_EQ(parser.header().request_id, 20u);
  EXPECT_EQ(parser.header().msg_type, static_cast<std::uint8_t>(MessageType::kResponse));
}

// ============================================================================
// FrameParser ? error handling
// ============================================================================

TEST(FrameCodecTest, FrameParserInvalidMagicViaParser) {
  // Feed bytes that do not spell "NRPC".
  std::vector<std::uint8_t> bad(32, 0x00);
  FrameParser parser;
  ParseResult result = parser.feed(bad.data(), bad.size());
  EXPECT_EQ(result, ParseResult::kInvalidMagic);
}

TEST(FrameCodecTest, FrameParserTruncatedHeader) {
  // Feed fewer than 32 bytes.
  std::vector<std::uint8_t> partial(20, 0x00);
  FrameParser parser;
  ParseResult result = parser.feed(partial.data(), partial.size());
  EXPECT_EQ(result, ParseResult::kNeedMoreData);
  EXPECT_EQ(parser.state(), ParseState::kNeedHeader);
}

TEST(FrameCodecTest, FrameParserTruncatedPayload) {
  auto frame = encodeFrame(5, MessageType::kRequest,
                           {{"k","v"}}, "somebody");

  // Feed only header + partial payload.
  std::size_t partial_len = kFixedHeaderSize + 2;
  ASSERT_GT(frame.size(), partial_len);

  FrameParser parser;
  ParseResult result = parser.feed(frame.data(), partial_len);
  EXPECT_EQ(result, ParseResult::kNeedMoreData);
  EXPECT_EQ(parser.state(), ParseState::kNeedPayload);
}

// ============================================================================
// Known message type values (Ping/Pong)
// ============================================================================

TEST(FrameCodecTest, EncodeDecodePingFrame) {
  auto frame = encodeFrame(0, MessageType::kPing, {}, "");

  FrameParser parser;
  ParseResult result = parser.feed(frame.data(), frame.size());
  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(parser.header().msg_type, 4u);  // kPing
  EXPECT_EQ(parser.header().total_length, kFixedHeaderSize);
}

TEST(FrameCodecTest, EncodeDecodePongFrame) {
  auto frame = encodeFrame(0, MessageType::kPong, {}, "");

  FrameParser parser;
  ParseResult result = parser.feed(frame.data(), frame.size());
  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(parser.header().msg_type, 5u);  // kPong
}

// ============================================================================
// Large frame boundary test
// ============================================================================

TEST(FrameCodecTest, FrameNearMaxMetadata) {
  // Encode metadata close to the 64 KiB limit.
  const std::string large_value(std::uint32_t(kMaxMetadataLength) - 20, 'x');
  std::map<std::string, std::string> meta;
  meta["k"] = large_value;

  auto frame = encodeFrame(1, MessageType::kRequest, meta, "");
  EXPECT_LE(frame.size(), std::size_t(kMaxTotalLength));

  FrameParser parser;
  ParseResult result = parser.feed(frame.data(), frame.size());
  ASSERT_EQ(result, ParseResult::kOk);
  EXPECT_EQ(parser.metadata().at("k"), large_value);
}

}  // namespace
}  // namespace nexus::rpc
