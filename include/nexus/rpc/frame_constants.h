#ifndef NEXUS_RPC_FRAME_CONSTANTS_H_
#define NEXUS_RPC_FRAME_CONSTANTS_H_

#include <cstddef>
#include <cstdint>

namespace nexus::rpc {

// ============================================================================
// Fixed header layout constants
// ============================================================================

/// Size of the fixed header in bytes.
constexpr std::uint32_t kFixedHeaderSize = 32;

/// Magic number in big-endian: ASCII "NRPC" -> 0x4E525043.
constexpr std::uint32_t kMagic = 0x4E525043;

/// Current protocol version.
constexpr std::uint8_t kVersion1 = 1;

/// Maximum metadata payload in bytes (64 KiB).
constexpr std::uint32_t kMaxMetadataLength = 64 * 1024;

/// Maximum body payload in bytes (16 MiB).
constexpr std::uint32_t kMaxBodyLength = 16 * 1024 * 1024;

/// Maximum total frame length: header + metadata + body.
constexpr std::uint32_t kMaxTotalLength =
    static_cast<std::uint32_t>(kFixedHeaderSize) + kMaxMetadataLength +
    kMaxBodyLength;

// ============================================================================
// Enumerations
// ============================================================================

/// Message type carried in the fixed header.
enum class MessageType : std::uint8_t {
  kRequest = 0,     ///< Unary RPC request.
  kResponse = 1,    ///< Unary RPC response.
  kStreamData = 2,  ///< Stream data chunk (reserved for v1.1+).
  kStreamEnd = 3,   ///< Stream end marker (reserved for v1.1+).
  kPing = 4,        ///< Keep-alive ping.
  kPong = 5,        ///< Keep-alive pong.
};

/// Serialization type carried in the fixed header.
enum class SerializeType : std::uint8_t {
  kProtobuf = 0,  ///< Protocol Buffers (only supported type in v1).
};

/// Compression type carried in the fixed header.
enum class CompressType : std::uint8_t {
  kNone = 0,  ///< No compression (only supported type in v1).
};

/// Result of a frame parsing/validation operation.
enum class ParseResult {
  kOk = 0,                    ///< Success.
  kNeedMoreData,              ///< Incomplete frame; more bytes expected.
  kInvalidMagic,              ///< Magic field does not equal NRPC.
  kInvalidVersion,            ///< Unsupported protocol version.
  kInvalidMessageType,        ///< Unknown message type value.
  kUnsupportedCompression,    ///< Non-zero compress type in v1.
  kInvalidSerializeType,      ///< Non-protobuf serialize type.
  kTotalLengthOverflow,       ///< Total length exceeds kMaxTotalLength.
  kTotalLengthMismatch,       ///< TotalLength != 32 + metaLength + bodyLength.
  kMetadataLengthOverflow,    ///< Metadata length exceeds kMaxMetadataLength.
  kBodyLengthOverflow,        ///< Body length exceeds kMaxBodyLength.
  kReservedFieldNonZero,      ///< Reserved field is non-zero.
  kMetadataParseError,        ///< Metadata protobuf wire-format error.
  kInternalError,             ///< Unexpected internal error.
};

/// Parser state-machine states.
enum class ParseState {
  kNeedHeader,   ///< Accumulating the 32-byte fixed header.
  kNeedPayload,  ///< Accumulating metadata + body.
  kComplete,     ///< Full frame available; call releaseFrame().
};

// ============================================================================
// RpcFrameHeader ? logical struct (NOT packed)
// ============================================================================

/**
 * Decoded view of the 32-byte fixed header.
 *
 * Each field corresponds directly to the on-wire layout described in
 * docs/protocol.md.  Instances of this struct are never transmitted
 * directly ? use encodeHeader() / decodeHeader() instead.
 */
struct RpcFrameHeader {
  std::uint32_t magic = kMagic;
  std::uint8_t version = kVersion1;
  std::uint8_t msg_type = 0;
  std::uint8_t compress_type = 0;
  std::uint8_t serialize_type = 0;
  std::uint32_t total_length = 0;
  std::uint64_t request_id = 0;
  std::uint32_t meta_length = 0;
  std::uint32_t body_length = 0;
  std::uint32_t reserved = 0;
};

}  // namespace nexus::rpc

#endif  // NEXUS_RPC_FRAME_CONSTANTS_H_
