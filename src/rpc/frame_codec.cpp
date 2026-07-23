/// @file frame_codec.cpp
/// @brief Implementation of the RPC frame codec (TASK-012).
///
/// Provides big-endian fixed-header encode/decode, Protobuf wire-format
/// metadata encode/decode, and an incremental FrameParser with support
/// for back-to-back frames without losing trailing bytes.

#include "nexus/rpc/frame_codec.h"

#include <cassert>
#include <cstring>

namespace nexus::rpc {

namespace {

// ==========================================================================
// Big-endian byte-level helpers
// ==========================================================================

/// Write a uint32 as big-endian into @p dst.
inline void writeU32BE(std::uint8_t* dst, std::uint32_t val) noexcept {
  dst[0] = static_cast<std::uint8_t>(val >> 24);
  dst[1] = static_cast<std::uint8_t>(val >> 16);
  dst[2] = static_cast<std::uint8_t>(val >> 8);
  dst[3] = static_cast<std::uint8_t>(val);
}

/// Read a big-endian uint32 from @p src.
inline std::uint32_t readU32BE(const std::uint8_t* src) noexcept {
  return (static_cast<std::uint32_t>(src[0]) << 24) |
         (static_cast<std::uint32_t>(src[1]) << 16) |
         (static_cast<std::uint32_t>(src[2]) << 8) |
         static_cast<std::uint32_t>(src[3]);
}

/// Write a uint64 as big-endian into @p dst (unrolled).
inline void writeU64BE(std::uint8_t* dst, std::uint64_t val) noexcept {
  dst[0] = static_cast<std::uint8_t>(val >> 56);
  dst[1] = static_cast<std::uint8_t>(val >> 48);
  dst[2] = static_cast<std::uint8_t>(val >> 40);
  dst[3] = static_cast<std::uint8_t>(val >> 32);
  dst[4] = static_cast<std::uint8_t>(val >> 24);
  dst[5] = static_cast<std::uint8_t>(val >> 16);
  dst[6] = static_cast<std::uint8_t>(val >> 8);
  dst[7] = static_cast<std::uint8_t>(val);
}

/// Read a big-endian uint64 from @p src (unrolled).
inline std::uint64_t readU64BE(const std::uint8_t* src) noexcept {
  return (static_cast<std::uint64_t>(src[0]) << 56) |
         (static_cast<std::uint64_t>(src[1]) << 48) |
         (static_cast<std::uint64_t>(src[2]) << 40) |
         (static_cast<std::uint64_t>(src[3]) << 32) |
         (static_cast<std::uint64_t>(src[4]) << 24) |
         (static_cast<std::uint64_t>(src[5]) << 16) |
         (static_cast<std::uint64_t>(src[6]) << 8) |
         (static_cast<std::uint64_t>(src[7]));
}

// ==========================================================================
// Protobuf varint helpers
// ==========================================================================

/// Returns the number of bytes a uint32 occupies when varint-encoded.
inline constexpr int varintByteSize(std::uint32_t value) noexcept {
  if (value < 0x80) return 1;
  if (value < 0x4000) return 2;
  if (value < 0x200000) return 3;
  if (value < 0x10000000) return 4;
  return 5;
}

/// Encode a uint32 as a Protobuf varint into @p dst.
/// @return Number of bytes written.
inline int encodeVarint32(std::uint32_t value, std::uint8_t* dst) noexcept {
  int i = 0;
  while (value >= 0x80) {
    dst[i++] = static_cast<std::uint8_t>(value | 0x80);
    value >>= 7;
  }
  dst[i++] = static_cast<std::uint8_t>(value);
  return i;
}

/// Append a varint-encoded uint32 to a vector.
inline void appendVarint32(std::vector<std::uint8_t>& buf,
                           std::uint32_t value) {
  while (value >= 0x80) {
    buf.push_back(static_cast<std::uint8_t>(value | 0x80));
    value >>= 7;
  }
  buf.push_back(static_cast<std::uint8_t>(value));
}

/// Decode a varint from a buffer.
/// @param[in]  data   Start of the varint.
/// @param[in]  end    One past the last readable byte.
/// @param[out] value  Decoded value.
/// @return  Number of bytes consumed (position of next byte), or 0 on error.
inline int decodeVarint(const std::uint8_t* data, const std::uint8_t* end,
                        std::uint64_t* value) noexcept {
  std::uint64_t result = 0;
  int shift = 0;
  int bytes = 0;
  while (data + bytes < end) {
    const std::uint8_t byte = data[bytes];
    result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
    ++bytes;
    if ((byte & 0x80) == 0) {
      *value = result;
      return bytes;
    }
    shift += 7;
    if (shift >= 64) {
      return 0;  // Varint too long (would overflow 64 bits).
    }
  }
  return 0;  // Truncated.
}

// ==========================================================================
// Protobuf wire-format constants for metadata map entries
// ==========================================================================

constexpr std::uint8_t kProtoWireTypeLenDelim = 2;
constexpr std::uint8_t kProtoTagMetadataEntry =
    static_cast<std::uint8_t>(1 << 3) | kProtoWireTypeLenDelim;  // 0x0A
constexpr std::uint8_t kProtoTagEntryKey =
    static_cast<std::uint8_t>(1 << 3) | kProtoWireTypeLenDelim;    // 0x0A
constexpr std::uint8_t kProtoTagEntryValue =
    static_cast<std::uint8_t>(2 << 3) | kProtoWireTypeLenDelim;    // 0x12

// ==========================================================================
// Header validation helpers
// ==========================================================================

/// Validate common header fields that apply to all message types.
inline ParseResult validateCommon(const RpcFrameHeader& hdr) noexcept {
  if (hdr.magic != kMagic) return ParseResult::kInvalidMagic;
  if (hdr.version != kVersion1) return ParseResult::kInvalidVersion;
  if (hdr.compress_type != 0) return ParseResult::kUnsupportedCompression;
  if (hdr.serialize_type != 0) return ParseResult::kInvalidSerializeType;
  if (hdr.reserved != 0) return ParseResult::kReservedFieldNonZero;
  return ParseResult::kOk;
}

/// Validate message-type and length fields.
inline ParseResult validateHeader(const RpcFrameHeader& hdr) noexcept {
  ParseResult common = validateCommon(hdr);
  if (common != ParseResult::kOk) return common;

  // Validate message type.
  switch (hdr.msg_type) {
    case static_cast<std::uint8_t>(MessageType::kRequest):
    case static_cast<std::uint8_t>(MessageType::kResponse):
    case static_cast<std::uint8_t>(MessageType::kStreamData):
    case static_cast<std::uint8_t>(MessageType::kStreamEnd):
    case static_cast<std::uint8_t>(MessageType::kPing):
    case static_cast<std::uint8_t>(MessageType::kPong):
      break;
    default:
      return ParseResult::kInvalidMessageType;
  }

  // Length boundary checks.
  if (hdr.total_length > kMaxTotalLength) {
    return ParseResult::kTotalLengthOverflow;
  }
  if (hdr.meta_length > kMaxMetadataLength) {
    return ParseResult::kMetadataLengthOverflow;
  }
  if (hdr.body_length > kMaxBodyLength) {
    return ParseResult::kBodyLengthOverflow;
  }

  // Verify totalLength == kFixedHeaderSize + metaLength + bodyLength.
  const std::uint64_t expected =
      static_cast<std::uint64_t>(kFixedHeaderSize) +
      static_cast<std::uint64_t>(hdr.meta_length) +
      static_cast<std::uint64_t>(hdr.body_length);
  if (static_cast<std::uint64_t>(hdr.total_length) != expected) {
    return ParseResult::kTotalLengthMismatch;
  }

  return ParseResult::kOk;
}

}  // namespace

// ============================================================================
// encodeHeader / decodeHeader
// ============================================================================

void encodeHeader(const RpcFrameHeader& header, std::uint8_t* output) {
  // Debug-only: validate before encoding so caller bugs are caught early.
  assert(validateHeader(header) == ParseResult::kOk);

  writeU32BE(output + 0, header.magic);
  output[4] = header.version;
  output[5] = header.msg_type;
  output[6] = header.compress_type;
  output[7] = header.serialize_type;
  writeU32BE(output + 8, header.total_length);
  writeU64BE(output + 12, header.request_id);
  writeU32BE(output + 20, header.meta_length);
  writeU32BE(output + 24, header.body_length);
  writeU32BE(output + 28, header.reserved);
}

ParseResult decodeHeader(const std::uint8_t* input, RpcFrameHeader* header) {
  header->magic = readU32BE(input + 0);
  header->version = input[4];
  header->msg_type = input[5];
  header->compress_type = input[6];
  header->serialize_type = input[7];
  header->total_length = readU32BE(input + 8);
  header->request_id = readU64BE(input + 12);
  header->meta_length = readU32BE(input + 20);
  header->body_length = readU32BE(input + 24);
  header->reserved = readU32BE(input + 28);

  return validateHeader(*header);
}

// ============================================================================
// Metadata encode / decode  (Protobuf wire-format map<string, string>)
// ============================================================================

std::vector<std::uint8_t> encodeMetadata(
    const std::map<std::string, std::string>& meta) {
  std::vector<std::uint8_t> buf;
  // Rough reserve: each entry is at least ~8 bytes of framing.
  buf.reserve(meta.size() * 16);

  for (const auto& [key, value] : meta) {
    const std::uint32_t key_len = static_cast<std::uint32_t>(key.size());
    const std::uint32_t val_len = static_cast<std::uint32_t>(value.size());

    // Compute inner entry size: key-tag(1) + key-len-varint + key +
    //                         value-tag(1) + val-len-varint + value
    const std::uint32_t inner_size =
        2 + static_cast<std::uint32_t>(varintByteSize(key_len)) + key_len +
        static_cast<std::uint32_t>(varintByteSize(val_len)) + val_len;

    // --- outer length-delimited wrapper (field 1, wire type 2) ---
    buf.push_back(kProtoTagMetadataEntry);
    appendVarint32(buf, inner_size);

    // --- key field (field 1, wire type 2) ---
    buf.push_back(kProtoTagEntryKey);
    appendVarint32(buf, key_len);
    buf.insert(buf.end(), key.begin(), key.end());

    // --- value field (field 2, wire type 2) ---
    buf.push_back(kProtoTagEntryValue);
    appendVarint32(buf, val_len);
    buf.insert(buf.end(), value.begin(), value.end());
  }

  return buf;
}

ParseResult decodeMetadata(const std::uint8_t* data, std::uint32_t length,
                           std::map<std::string, std::string>* meta) {
  const std::uint8_t* const end = data + length;
  const std::uint8_t* pos = data;

  while (pos < end) {
    // Each map entry is a length-delimited sub-message with tag 0x0A.
    if (*pos != kProtoTagMetadataEntry) {
      return ParseResult::kMetadataParseError;
    }
    ++pos;

    // Read the length of the entry sub-message.
    std::uint64_t entry_len = 0;
    int consumed = decodeVarint(pos, end, &entry_len);
    if (consumed == 0 || entry_len > static_cast<std::uint64_t>(end - pos)) {
      return ParseResult::kMetadataParseError;
    }
    pos += consumed;

    const std::uint8_t* const entry_end = pos + entry_len;
    std::string key;
    std::string value;
    bool has_key = false;

    // Parse fields inside the entry sub-message.
    while (pos < entry_end) {
      const std::uint8_t tag = *pos;
      const std::uint8_t wire_type = tag & 0x07;
      const std::uint8_t field_num = tag >> 3;
      ++pos;

      if (wire_type != kProtoWireTypeLenDelim) {
        return ParseResult::kMetadataParseError;
      }

      std::uint64_t field_len = 0;
      consumed = decodeVarint(pos, entry_end, &field_len);
      if (consumed == 0 ||
          field_len > static_cast<std::uint64_t>(entry_end - pos)) {
        return ParseResult::kMetadataParseError;
      }
      pos += consumed;

      if (field_num == 1) {  // key
        key.assign(reinterpret_cast<const char*>(pos),
                   static_cast<std::size_t>(field_len));
        has_key = true;
      } else if (field_num == 2) {  // value
        value.assign(reinterpret_cast<const char*>(pos),
                     static_cast<std::size_t>(field_len));
      }
      // Unknown fields are silently skipped for forward compatibility.

      pos += field_len;
    }

    if (!has_key) {
      return ParseResult::kMetadataParseError;
    }
    // Value may be empty, which is valid.
    (*meta)[std::move(key)] = std::move(value);
  }

  return ParseResult::kOk;
}

// ============================================================================
// encodeFrame
// ============================================================================

std::vector<std::uint8_t> encodeFrame(
    std::uint64_t request_id, MessageType msg_type,
    const std::map<std::string, std::string>& metadata,
    std::string_view body) {
  std::vector<std::uint8_t> meta_bytes = encodeMetadata(metadata);
  const std::uint32_t meta_len =
      static_cast<std::uint32_t>(meta_bytes.size());
  const std::uint32_t body_len = static_cast<std::uint32_t>(body.size());
  const std::uint32_t total_len =
      static_cast<std::uint32_t>(kFixedHeaderSize) + meta_len + body_len;

  // Debug-only: catch oversized frames before encoding.
  assert(meta_len <= kMaxMetadataLength);
  assert(body_len <= kMaxBodyLength);
  assert(total_len <= kMaxTotalLength);

  RpcFrameHeader header{};
  header.magic = kMagic;
  header.version = kVersion1;
  header.msg_type = static_cast<std::uint8_t>(msg_type);
  header.compress_type = 0;
  header.serialize_type = 0;
  header.total_length = total_len;
  header.request_id = request_id;
  header.meta_length = meta_len;
  header.body_length = body_len;
  header.reserved = 0;

  std::vector<std::uint8_t> frame;
  frame.reserve(total_len);
  frame.resize(kFixedHeaderSize);
  encodeHeader(header, frame.data());

  frame.insert(frame.end(), meta_bytes.begin(), meta_bytes.end());
  frame.insert(frame.end(), body.begin(), body.end());

  return frame;
}

// ============================================================================
// FrameParser
// ============================================================================

ParseResult FrameParser::feed(const std::uint8_t* data, std::size_t length) {
  if (complete_) {
    return ParseResult::kInternalError;
  }

  if (length != 0) {
    if (data == nullptr) {
      return ParseResult::kInternalError;
    }
    buffer_.insert(buffer_.end(), data, data + length);
  }

  if (state_ == ParseState::kNeedHeader) {
    if (buffer_.size() < kFixedHeaderSize) {
      return ParseResult::kNeedMoreData;

    }

    ParseResult result = decodeHeader(buffer_.data(), &header_);
    if (result != ParseResult::kOk) {
      return result;
    }

    // Remove the 32-byte header from the accumulated buffer.
    buffer_.erase(buffer_.begin(), buffer_.begin() + kFixedHeaderSize);

    if (header_.total_length == kFixedHeaderSize) {
      // No payload ? frame is complete immediately.
      state_ = ParseState::kComplete;
      complete_ = true;
      return ParseResult::kOk;
    }

    state_ = ParseState::kNeedPayload;
    // Fall through to try completing the payload in the same call.
  }

  if (state_ == ParseState::kNeedPayload) {
    const std::uint32_t payload_len =
        header_.total_length - static_cast<std::uint32_t>(kFixedHeaderSize);
    if (buffer_.size() < payload_len) {
      return ParseResult::kNeedMoreData;
    }

    // Split metadata and body from the payload.
    const std::uint8_t* payload = buffer_.data();
    const std::uint32_t meta_len = header_.meta_length;
    const std::uint32_t body_len = header_.body_length;

    // Decode metadata.
    if (meta_len > 0) {
      ParseResult meta_result =
          decodeMetadata(payload, meta_len, &metadata_);
      if (meta_result != ParseResult::kOk) {
        return meta_result;
      }
    }

    // Copy body.
    if (body_len > 0) {
      body_.assign(payload + meta_len, payload + meta_len + body_len);
    }

    // Erase consumed payload bytes only ? trailing data stays for next frame.
    buffer_.erase(buffer_.begin(), buffer_.begin() + payload_len);
    state_ = ParseState::kComplete;
    complete_ = true;
    return ParseResult::kOk;
  }

  return ParseResult::kInternalError;
}

void FrameParser::reset() {
  state_ = ParseState::kNeedHeader;
  // buffer_ is NOT cleared ? trailing bytes from a previous
  // concatenated feed() belong to the next frame and must survive.
  metadata_.clear();
  body_.clear();
  header_ = RpcFrameHeader{};
  complete_ = false;
}

}  // namespace nexus::rpc
