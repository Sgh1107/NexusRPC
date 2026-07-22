#ifndef NEXUS_RPC_FRAME_CODEC_H_
#define NEXUS_RPC_FRAME_CODEC_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/rpc/frame_constants.h"

namespace nexus::rpc {

// ============================================================================
// Fixed-header encoding / decoding
// ============================================================================

/**
 * Encode an RpcFrameHeader into a 32-byte output buffer.
 *
 * All multi-byte integers are written in big-endian byte order.
 * The caller must ensure that @p output points to at least
 * kFixedHeaderSize writable bytes.
 *
 * @param header  Populated header fields.
 * @param output  Destination buffer (32 bytes minimum).
 */
void encodeHeader(const RpcFrameHeader& header, std::uint8_t* output);

/**
 * Decode a big-endian 32-byte buffer into an RpcFrameHeader.
 *
 * After decoding, the header fields are validated:
 * - magic must equal kMagic
 * - version must equal kVersion1
 * - msg_type must be a known value
 * - compress_type must be zero (v1)
 * - serialize_type must be zero (protobuf)
 * - total_length must be within [kFixedHeaderSize, kMaxTotalLength]
 * - meta_length must not exceed kMaxMetadataLength
 * - body_length must not exceed kMaxBodyLength
 * - reserved must be zero
 * - total_length must equal kFixedHeaderSize + meta_length + body_length
 *
 * @param input   32-byte source buffer.
 * @param header  [out] Populated on success.
 * @return        ParseResult::kOk or the first validation failure code.
 */
ParseResult decodeHeader(const std::uint8_t* input, RpcFrameHeader* header);

// ============================================================================
// Metadata encoding / decoding (Protobuf wire-format map<string, string>)
// ============================================================================

/**
 * Encode a metadata key-value map into the Protobuf wire format for
 * `map<string, string>`.
 *
 * Each entry is encoded as an embedded message:
 *   0A <varint entry-len>
 *       0A <varint key-len>   <key-bytes>
 *       12 <varint value-len> <value-bytes>
 *
 * @param meta  Map of metadata key-value pairs.
 * @return      Encoded byte vector.
 */
std::vector<std::uint8_t> encodeMetadata(
    const std::map<std::string, std::string>& meta);

/**
 * Decode a Protobuf wire-format `map<string, string>` payload.
 *
 * Unknown keys are preserved and returned in the output map so callers
 * can pass them through transparently.
 *
 * @param data    Pointer to the metadata payload.
 * @param length  Number of bytes to decode.
 * @param meta    [out] Populated on success.
 * @return        ParseResult::kOk or ParseResult::kMetadataParseError.
 */
ParseResult decodeMetadata(const std::uint8_t* data, std::uint32_t length,
                           std::map<std::string, std::string>* meta);

// ============================================================================
// Full-frame encoding
// ============================================================================

/**
 * Encode a complete RPC frame.
 *
 * The frame layout is:
 *   32-byte fixed header + encoded metadata bytes + body bytes
 *
 * @param request_id  Monotonic request identifier.
 * @param msg_type    Message type (kRequest or kResponse in v1).
 * @param metadata    Key-value metadata map.
 * @param body        Raw Protobuf body payload.
 * @return            Complete frame as a byte vector.
 */
std::vector<std::uint8_t> encodeFrame(
    std::uint64_t request_id, MessageType msg_type,
    const std::map<std::string, std::string>& metadata,
    std::string_view body);

// ============================================================================
// FrameParser ? incremental byte-stream parser
// ============================================================================

/**
 * Incrementally parses RPC frames from a byte stream (e.g., TCP).
 *
 * Usage:
 *   FrameParser parser;
 *   while (true) {
 *       auto result = parser.feed(data, len);
 *       if (result == ParseResult::kOk) {
 *           // header(), metadata(), body() are now available
 *           parser.reset();  // ready for next frame
 *       } else if (result != ParseResult::kNeedMoreData) {
 *           // protocol error ? close connection
 *       }
 *   }
 *
 * The parser accumulates partial bytes internally until a complete
 * frame is available.
 */
class FrameParser {
 public:
  FrameParser() = default;

  /**
   * Feed raw bytes into the parser.
   *
   * @param data    Pointer to incoming bytes.
   * @param length  Number of bytes to feed.
   * @return        kOk when a complete frame has been parsed,
   *                kNeedMoreData when waiting for more bytes,
   *                or an error code on protocol violation.
   */
  ParseResult feed(const std::uint8_t* data, std::size_t length);

  /// True when a complete frame is available.
  bool isComplete() const noexcept { return complete_; }

  /// Current parser state-machine state.
  ParseState state() const noexcept { return state_; }

  /// Number of bytes accumulated so far.
  std::size_t bufferSize() const noexcept { return buffer_.size(); }

  /// Access the parsed header (valid when isComplete()).
  const RpcFrameHeader& header() const noexcept { return header_; }

  /// Access the parsed metadata (valid when isComplete()).
  const std::map<std::string, std::string>& metadata() const noexcept {
    return metadata_;
  }

  /// Access the parsed body (valid when isComplete()).
  const std::vector<std::uint8_t>& body() const noexcept { return body_; }

  /// Reset the parser for the next frame.
  void reset();

 private:
  ParseState state_ = ParseState::kNeedHeader;
  RpcFrameHeader header_;
  std::vector<std::uint8_t> buffer_;
  std::map<std::string, std::string> metadata_;
  std::vector<std::uint8_t> body_;
  bool complete_ = false;
};

}  // namespace nexus::rpc

#endif  // NEXUS_RPC_FRAME_CODEC_H_
